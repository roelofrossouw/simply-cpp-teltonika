#include "../src/binary_parser.h" // private, tested here
#include <sc/teltonika/command.h>
#include <sc/teltonika/teltonika.h>
#include <sc_test.h>

#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace sc::teltonika;

namespace {
    // Big-endian, as Teltonika sends it.
    template<typename T>
    void put(std::string &out, const T value) {
        for (int shift = (sizeof(T) - 1) * 8; shift >= 0; shift -= 8) out += static_cast<char>((static_cast<std::uint64_t>(value) >> shift) & 0xff);
    }

    struct record {
        std::uint64_t time_ms;
        std::int32_t lng, lat; // degrees * 10^7
        std::uint16_t alt, ang;
        std::uint8_t sat;
        std::uint16_t speed;
        std::vector<std::pair<std::uint16_t, std::uint16_t>> two_byte{};  // IO ID and value
        std::vector<std::pair<std::uint16_t, std::uint32_t>> four_byte{};
    };

    // A Codec 8 Extended frame holding records and their 2- and 4-byte IO values.
    std::string codec8e_frame(const std::vector<record> &records) {
        std::string data;
        put<std::uint8_t>(data, 0x8E);
        put<std::uint8_t>(data, records.size());
        for (const auto &r: records) {
            put(data, r.time_ms);
            put<std::uint8_t>(data, 0); // priority
            put(data, r.lng);
            put(data, r.lat);
            put(data, r.alt);
            put(data, r.ang);
            put(data, r.sat);
            put(data, r.speed);
            put<std::uint16_t>(data, 0); // event IO
            put<std::uint16_t>(data, r.two_byte.size() + r.four_byte.size());
            put<std::uint16_t>(data, 0); // no 1-byte values
            put<std::uint16_t>(data, r.two_byte.size());
            for (const auto &[id, value]: r.two_byte) {
                put(data, id);
                put(data, value);
            }
            put<std::uint16_t>(data, r.four_byte.size());
            for (const auto &[id, value]: r.four_byte) {
                put(data, id);
                put(data, value);
            }
            for (int i = 0; i < 2; ++i) put<std::uint16_t>(data, 0); // no 8 or X-byte values
        }
        put<std::uint8_t>(data, records.size());

        std::uint32_t crc = 0; // CRC-16/IBM
        for (const unsigned char byte: data) {
            crc ^= byte;
            for (int i = 0; i < 8; ++i) crc = crc & 1 ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
        std::string out(4, '\0');
        put<std::uint32_t>(out, data.size());
        out += data;
        put(out, crc);
        return out;
    }
}

int main() {
    SECTION("Binary parser reads big-endian values");
    const std::string bytes{"\x12\x34\x56\x78", 4};
    binary_parser parser{bytes, endian::big};
    CHECK_EQ(parser.read_uint16(), 0x1234);
    CHECK_EQ(parser.read_uint16(), 0x5678);
    CHECK_EQ(parser.position(), 4);

    SECTION("Codec 12 frames are parsed");
    const command getinfo{"getinfo"};
    const auto getinfo_bytes = getinfo.bytes();
    frame message{getinfo_bytes};

    CHECK_EQ(getinfo_bytes.substr(0, 4), std::string(4, '\0'));
    CHECK_EQ(static_cast<unsigned char>(getinfo_bytes[8]), 12);
    CHECK_EQ(message.is_valid(), true);
    CHECK_EQ(message.items(), 1);
    CHECK_EQ(message.crc(), getinfo_bytes.substr(getinfo_bytes.size() - 4));

    SECTION("Invalid frames are rejected");
    frame empty{std::string{}};
    CHECK_EQ(empty.is_valid(), false);
    CHECK_EQ(empty.crc(), std::string(4, '\0'));

    auto corrupt = getinfo_bytes;
    corrupt.back() ^= 1;
    frame invalid{corrupt};
    CHECK_EQ(invalid.is_valid(), false);

    SECTION("A frame it can't read is a warning: written at warning (the default), not at error");
    {
        auto too_long = getinfo_bytes;
        too_long += "extra"; // its length no longer matches the one it states
        std::ostringstream captured;
        auto *const previous = std::cerr.rdbuf(captured.rdbuf());
        CHECK(sc::teltonika::log_level() == sc::log_level::warning);
        CHECK(!frame{too_long}.is_valid());
        const auto at_warning = captured.str();
        captured.str("");
        sc::teltonika::log_level(sc::log_level::error);
        CHECK(!frame{too_long}.is_valid());
        const auto at_error = captured.str();
        sc::teltonika::log_level(sc::log_level::warning);
        std::cerr.rdbuf(previous);
        CHECK(at_warning.find("Invalid data length") != std::string::npos);
        CHECK(at_error.empty());
    }

    SECTION("Parsers and messages move but don't copy");
    {
        // A copy would free the string's stream twice.
        static_assert(!std::is_copy_constructible_v<binary_parser>);
        static_assert(!std::is_copy_constructible_v<frame>);
        static_assert(std::is_nothrow_move_constructible_v<binary_parser>);
        static_assert(std::is_nothrow_move_constructible_v<frame>);
        static_assert(std::is_nothrow_move_assignable_v<frame>);
        // One pointer each: what they hold can change without breaking programs built earlier.
        static_assert(sizeof(frame) == sizeof(void *));
        static_assert(sizeof(binary_parser) == sizeof(void *));
        static_assert(sizeof(command) == sizeof(void *));

        binary_parser first{bytes, endian::big};
        CHECK_EQ(first.read_uint16(), 0x1234);
        binary_parser moved{std::move(first)};
        CHECK_EQ(moved.position(), 2);           // carries on where it was
        CHECK_EQ(moved.read_uint16(), 0x5678);

        frame original{getinfo_bytes};
        frame taken{std::move(original)};
        CHECK_EQ(taken.is_valid(), true);
        CHECK_EQ(taken.items(), 1);
    }

    SECTION("Every record's position, to the millisecond");
    {
        // The newer record first, as a device emptying its buffer may send them.
        const frame avl{codec8e_frame({
            {1'760'000'060'250, 281234567, -259876543, 1500, 90, 9, 62},
            {1'760'000'000'000, 281200000, -259800000, 1490, 85, 0, 0},
        })};
        CHECK(avl.is_valid());
        CHECK_EQ(avl.items(), 2);
        const auto &positions = avl.positions();
        CHECK_EQ(positions.size(), std::size_t{2});
        if (positions.size() == 2) {
            CHECK_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(positions[0].time.time_since_epoch()).count(), 1'760'000'060'250LL);
            CHECK_NEAR(positions[0].lat, -25.9876543, 1e-9);
            CHECK_NEAR(positions[0].lng, 28.1234567, 1e-9);
            CHECK_EQ(positions[0].alt, 1500);
            CHECK_EQ(positions[0].ang, 90);
            CHECK_EQ(positions[0].sat, 9);
            CHECK_EQ(positions[0].speed, 62);
            CHECK_EQ(positions[1].sat, 0); // no fix
        }
        // gps() is still the last record in the frame.
        CHECK_EQ(avl.gps()["sat"].get<int>(), 0);
    }

    SECTION("IO values by AVL ID, so IDs sharing a name stay apart");
    {
        frame avl{codec8e_frame({
            {1'760'000'000'000, 281200000, -259800000, 1490, 85, 9, 0,
             {{60, 450}, {110, 45}}, {{83, 12345}, {107, 2500}}},
        })};
        CHECK(avl.is_valid());
        const auto &records = avl.avl_data();
        CHECK_EQ(records.size(), std::size_t{1});
        if (records.size() == 1) {
            const auto &record = records[0];
            CHECK_EQ(record.at(60), std::string{"4.50L/h"});   // "Fuel Rate", 0.01 L/h
            CHECK_EQ(record.at(110), std::string{"4.5l/h"});   // "Fuel Rate" too, 0.1 l/h
            CHECK_EQ(record.at(83), std::string{"1234.5l"});   // "Fuel Consumed"
            CHECK_EQ(record.at(107), std::string{"250.0l"});   // "Fuel Consumed (counted)"
            CHECK(!record.contains(84));

            const auto named = avl_names(record); // the name-keyed form: the higher ID wins a shared name
            CHECK_EQ(named.size(), std::size_t{3});
            CHECK_EQ(named["Fuel Rate"].get<std::string>(), std::string{"4.5l/h"});
            CHECK_EQ(named["Fuel Consumed"].get<std::string>(), std::string{"1234.5l"});
            CHECK_EQ(named["Fuel Consumed (counted)"].get<std::string>(), std::string{"250.0l"});
        }
    }

    SECTION("Raw records: every IO by ID with its size and bits, known or not; codec; Codec 12 texts");
    {
        const frame avl{codec8e_frame({
            {1'760'000'060'250, 281234567, -259876543, 1500, 90, 9, 62,
             {{60, 450}, {10825, 65535}}, {{83, 12345}, {65000, 0xFFFFFFFF}}},
        })};
        CHECK(avl.is_valid());
        CHECK_EQ(int{avl.codec()}, 142);
        const auto &records = avl.records();
        CHECK_EQ(records.size(), std::size_t{1});
        if (records.size() == 1) {
            const auto &r = records[0];
            CHECK_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(r.time.time_since_epoch()).count(), 1'760'000'060'250LL);
            CHECK_EQ(r.lat_e7, -259876543);
            CHECK_EQ(r.lng_e7, 281234567);
            CHECK_EQ(int{r.alt}, 1500);
            CHECK_EQ(int{r.sat}, 9);
            CHECK_EQ(int{r.speed}, 62);
            CHECK_EQ(r.io.size(), std::size_t{4});
            if (r.io.size() == 4) {
                CHECK_EQ(int{r.io[0].id}, 60);
                CHECK_EQ(int{r.io[0].size}, 2);
                CHECK_EQ(r.io[0].number, std::uint64_t{450});
                CHECK_EQ(int{r.io[1].id}, 10825); // an ID the table doesn't know: kept, raw
                CHECK_EQ(r.io[1].number, std::uint64_t{65535});
                CHECK_EQ(int{r.io[3].id}, 65000);
                CHECK_EQ(int{r.io[3].size}, 4);
                CHECK_EQ(r.io[3].number, std::uint64_t{0xFFFFFFFF}); // bits, unsigned
            }
        }
        CHECK_EQ(const_cast<frame &>(avl).avl_data()[0].at(60), std::string{"4.50L/h"}); // the formatted ones as before

        const command getinfo{"getinfo"};
        const frame message{getinfo.bytes()};
        CHECK_EQ(int{message.codec()}, 12);
        CHECK_EQ(message.messages().size(), std::size_t{1});
        if (!message.messages().empty()) {
            CHECK_EQ(int{message.messages()[0].type}, 5);
            CHECK_EQ(message.messages()[0].text, std::string{"getinfo"});
        }
        CHECK_EQ(int{frame{std::string(12, '\0')}.codec()}, 0); // not read
    }

    SECTION("From raw records, the same as from the frame: to_position, to_record, gps_info");
    {
        const frame avl{codec8e_frame({
            {1'760'000'060'250, 281234567, -259876543, 1500, 90, 9, 62, {{60, 450}, {17, 0xFFF6}}, {{83, 12345}, {10825, 7}}},
            {1'760'000'000'000, 0, 0, 0, 0, 0, 0},
        })};
        auto &formatted = const_cast<frame &>(avl).avl_data();
        CHECK_EQ(avl.records().size(), std::size_t{2});
        for (std::size_t i = 0; i < avl.records().size(); ++i) {
            CHECK(to_record(avl.records()[i]) == formatted[i]);
            const auto p = to_position(avl.records()[i]);
            CHECK(p.time == avl.positions()[i].time && p.lat == avl.positions()[i].lat && p.lng == avl.positions()[i].lng &&
                  p.alt == avl.positions()[i].alt && p.speed == avl.positions()[i].speed && p.sat == avl.positions()[i].sat);
        }
        CHECK_EQ(formatted[0].at(17), std::string{"-10mG"}); // signed (Axis X), from its raw bits
        CHECK(gps_info(avl.positions()[0]) == nlohmann::ordered_json(frame{codec8e_frame({
            {1'760'000'060'250, 281234567, -259876543, 1500, 90, 9, 62}})}.gps()));
        CHECK(gps_info(avl.positions()[1]).is_null()); // latitude 0: no fix
    }

    SECTION("Event names: the triggering IOs by meaning, others by AVL name, unknown by number; priorities");
    {
        CHECK_EQ(event_name(0), std::string{"Device Movement Event"}); // the periodic position update
        CHECK_EQ(event_name(3), std::string{"Panic"});
        CHECK_EQ(event_name(236), std::string{"Panic"});
        CHECK_EQ(event_name(239), std::string{"Ignition Event"});
        CHECK_EQ(event_name(247), std::string{"Crash"});
        CHECK_EQ(event_name(10831), std::string{"Lost Beacon Record"});
        CHECK_EQ(event_name(66), std::string{"External Voltage"}); // not in the list: its AVL name
        CHECK_EQ(event_name(65000), std::string{"IO 65000"});      // nowhere
        CHECK_EQ(priority_name(0), std::string{"low"});
        CHECK_EQ(priority_name(2), std::string{"panic"});
        CHECK_EQ(priority_name(9), std::string{"priority 9"});
    }

    SECTION("BLE beacons: the simple mode (385), Teltonika's own example");
    {
        const auto bytes = [](const std::string& hex) {
            std::string out;
            for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
            return out;
        };
        const avl_io_value simple{385, 0, 0, bytes(
            "11214B5C049F515341FCA950D2C264414E1000050006BA21E2C56DB5DFFB48D2B060D0F5A71096E000000000A92131A74BB76A7942"
            "3196C916CFB9FAED45002D00159F0700112233445566778899ABCDE0810047AE0BE80015210F86676BEC91420A94409110029AFAC4"
            "15B31A0AA101DE9C18E92CA5AA689697365434663222BA21EBBBDE835D7F4965B5F06C2EDCB3A55300010080A501736B79686F7374"
            "2E646B000010000128AD")};
        const auto beacons = decode_beacons(simple);
        CHECK_EQ(beacons.size(), std::size_t{8});
        CHECK_EQ(beacons[0].protocol, std::string{"iBeacon"});
        CHECK_EQ(beacons[0].uuid, std::string{"4b5c049f515341fca950d2c264414e10"});
        CHECK_EQ(*beacons[0].major, std::uint16_t{5});
        CHECK_EQ(*beacons[0].minor, std::uint16_t{6});
        CHECK_EQ(*beacons[0].rssi, -70);
        CHECK(beacons[0].mac.empty()); // the simple mode sends no MAC
        CHECK_EQ(beacons[3].protocol, std::string{"Eddystone"});
        CHECK_EQ(beacons[3].eddystone_namespace, std::string{"00112233445566778899"});
        CHECK_EQ(beacons[3].instance, std::string{"abcde0810047"});
        CHECK_EQ(*beacons[3].rssi, -82);
        CHECK_EQ(*beacons[3].battery_mv, 3048);
        CHECK_EQ(*beacons[3].temperature_c, 21.0);
        CHECK(!beacons[0].battery_mv.has_value());
        CHECK_EQ(beacons[7].instance, std::string{"000010000128"});
        CHECK_EQ(*beacons[7].rssi, -83);
        CHECK(decode_beacons({385, 0, 0, bytes("11")}).empty()); // none seen
        CHECK_EQ(decode_beacons({385, 0, 0, bytes("1121") + bytes("4B5C049F")}).size(), std::size_t{0}); // cut short
        CHECK(decode_beacons({66, 2, 12800, {}}).empty()); // not a beacon IO
    }

    SECTION("BLE beacons: the advanced mode (548 and the Near/Away/Lost records)");
    {
        const auto bytes = [](const std::string& hex) {
            std::string out;
            for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
            return out;
        };
        // Two beacons as the fleet's devices send them: RSSI, MAC and the beacon's advertisement, an
        // Eddystone UID (power -24 dBm at 0 m) and an iBeacon (-59 dBm at 1 m).
        const std::string eddystone = "0001C6" "0106001122334455" "021F" "020106" "0303AAFE" "1716AAFE00E8"
                                      "0102030405060708090A" "0B0C0D0E0F10" "0000";
        const std::string ibeacon = "0001A3" "0106665544332211" "021E" "020106" "1AFF4C000215"
                                    "E2C56DB5DFFB48D2B060D0F5A71096E0" "0007" "0009" "C5";
        const auto advanced = decode_beacons({548, 0, 0, bytes("01" "2C" + eddystone + "2B" + ibeacon)});
        CHECK_EQ(advanced.size(), std::size_t{2});
        CHECK_EQ(advanced[0].mac, std::string{"00:11:22:33:44:55"});
        CHECK_EQ(*advanced[0].rssi, -58);
        CHECK_EQ(advanced[0].protocol, std::string{"Eddystone"});
        CHECK_EQ(advanced[0].eddystone_namespace, std::string{"0102030405060708090a"});
        CHECK_EQ(advanced[0].instance, std::string{"0b0c0d0e0f10"});
        CHECK_EQ(*advanced[0].rssi_at_1m, -65); // -24 at 0 m, less 41 dB to 1 m
        CHECK_EQ(advanced[1].mac, std::string{"66:55:44:33:22:11"});
        CHECK_EQ(advanced[1].protocol, std::string{"iBeacon"});
        CHECK_EQ(advanced[1].uuid, std::string{"e2c56db5dffb48d2b060d0f5a71096e0"});
        CHECK_EQ(*advanced[1].major, std::uint16_t{7});
        CHECK_EQ(*advanced[1].minor, std::uint16_t{9});
        CHECK_EQ(*advanced[1].rssi_at_1m, -59);

        // A Near Beacon Record: MAC (15), low battery (13), battery voltage (14).
        const auto near = decode_beacons({10828, 0, 0, bytes("01" "12" "0001D5" "0F06001122334466" "0D0100" "0E020C12")});
        CHECK_EQ(near.size(), std::size_t{1});
        CHECK_EQ(near[0].mac, std::string{"00:11:22:33:44:66"});
        CHECK_EQ(*near[0].rssi, -43);
        CHECK_EQ(*near[0].low_battery, false);
        CHECK_EQ(*near[0].battery_mv, 3090);

        // Older firmware's beacon data: flags, 0x80 a voltage byte (x 10 + 2000 mV), as Traccar's test has it.
        const auto older = decode_beacons({548, 0, 0, bytes("01" "0F" "0001C6" "0106BABBF3630055" "0202806D")});
        CHECK_EQ(older.size(), std::size_t{1});
        CHECK_EQ(*older[0].battery_mv, 3090);
        CHECK_EQ(older[0].mac, std::string{"ba:bb:f3:63:00:55"});

        // An Eddystone TLM (battery, temperature in 8.8) and URL, and a name; sensor fields.
        const auto telemetry = decode_beacons({548, 0, 0, bytes(
            "01" "40" "0001B0" "0106AABBCCDDEEFF" "0212" "1116AAFE20000BB81580000000010000000A"
            "0507" "736D6F6B6579" "21" "06020898" "070137" "080101" "09010A" "0A020003" "0B01FB" "0C02FFF6")});
        CHECK_EQ(telemetry.size(), std::size_t{1});
        CHECK_EQ(*telemetry[0].battery_mv, 3000);
        CHECK_EQ(*telemetry[0].temperature_c, 22.0);    // field 6: 0x0898 = 2200 x 0.01 °C (after the TLM's 21.5)
        CHECK_EQ(telemetry[0].name, std::string{"smokey!"});
        CHECK_EQ(*telemetry[0].humidity, 55);
        CHECK_EQ(*telemetry[0].magnet, true);
        CHECK_EQ(*telemetry[0].motion, true);
        CHECK_EQ(*telemetry[0].motion_count, 3);
        CHECK_EQ(*telemetry[0].pitch, -5);
        CHECK_EQ(*telemetry[0].roll, -10);
        const auto url = decode_beacons({548, 0, 0, bytes("01" "19" "0106AABBCCDDEEF0" "020F" "0E16AAFE10EB03" "6578616D706C6507")});
        CHECK_EQ(url.size(), std::size_t{1});
        CHECK_EQ(url[0].url, std::string{"https://example.com"});
        CHECK_EQ(*url[0].rssi_at_1m, -62);
    }

    SECTION("About how far a beacon is, from its RSSI");
    CHECK_EQ(beacon_distance_m(-59), 1.0);
    CHECK_EQ(beacon_distance_m(-79), 10.0);
    CHECK_EQ(beacon_distance_m(-65, -65), 1.0);
    CHECK_EQ(beacon_distance_m(-71, -65), 2.0);
    CHECK_EQ(beacon_distance_m(-20), 0.1); // closer than 0.1 m reads 0.1

    TEST_SUMMARY();
}
