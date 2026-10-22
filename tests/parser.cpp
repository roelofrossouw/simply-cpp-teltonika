#include <BinaryParser.h>
#include <Codec12.h>
#include <Teltonika.h>
#include <sc_test.h>

#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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
        std::string frame(4, '\0');
        put<std::uint32_t>(frame, data.size());
        frame += data;
        put(frame, crc);
        return frame;
    }
}

int main() {
    SECTION("Binary parser reads big-endian values");
    const std::string bytes{"\x12\x34\x56\x78", 4};
    BinaryParser parser{bytes, Endian::Big};
    CHECK_EQ(parser.readUInt16(), 0x1234);
    CHECK_EQ(parser.readUInt16(), 0x5678);
    CHECK_EQ(parser.position(), 4);

    SECTION("Codec 12 frames are parsed");
    Codec12 command{"getinfo"};
    const auto frame = command.Data();
    Teltonika message{frame};

    CHECK_EQ(frame.substr(0, 4), std::string(4, '\0'));
    CHECK_EQ(static_cast<unsigned char>(frame[8]), 12);
    CHECK_EQ(message.IsValid(), true);
    CHECK_EQ(message.Items(), 1);
    CHECK_EQ(message.CRC(), frame.substr(frame.size() - 4));

    SECTION("Invalid frames are rejected");
    Teltonika empty{std::string{}};
    CHECK_EQ(empty.IsValid(), false);
    CHECK_EQ(empty.CRC(), std::string(4, '\0'));

    auto corrupt = frame;
    corrupt.back() ^= 1;
    Teltonika invalid{corrupt};
    CHECK_EQ(invalid.IsValid(), false);

    SECTION("A frame it can't read is a warning: written at warning (the default), not at error");
    {
        auto too_long = frame;
        too_long += "extra"; // its length no longer matches the one it states
        std::ostringstream captured;
        auto *const previous = std::cerr.rdbuf(captured.rdbuf());
        CHECK(sc::teltonika::log_level() == sc::log_level::warning);
        CHECK(!Teltonika{too_long}.IsValid());
        const auto at_warning = captured.str();
        captured.str("");
        sc::teltonika::log_level(sc::log_level::error);
        CHECK(!Teltonika{too_long}.IsValid());
        const auto at_error = captured.str();
        sc::teltonika::log_level(sc::log_level::warning);
        std::cerr.rdbuf(previous);
        CHECK(at_warning.find("Invalid data length") != std::string::npos);
        CHECK(at_error.empty());
    }

    SECTION("Parsers and messages move but don't copy");
    {
        // A copy would free the string's stream twice.
        static_assert(!std::is_copy_constructible_v<BinaryParser>);
        static_assert(!std::is_copy_constructible_v<Teltonika>);
        static_assert(std::is_nothrow_move_constructible_v<BinaryParser>);
        static_assert(std::is_nothrow_move_constructible_v<Teltonika>);
        static_assert(std::is_nothrow_move_assignable_v<Teltonika>);
        // One pointer each: what they hold can change without breaking programs built earlier.
        static_assert(sizeof(Teltonika) == sizeof(void *));
        static_assert(sizeof(BinaryParser) == sizeof(void *));
        static_assert(sizeof(Codec12) == sizeof(void *));

        BinaryParser first{bytes, Endian::Big};
        CHECK_EQ(first.readUInt16(), 0x1234);
        BinaryParser moved{std::move(first)};
        CHECK_EQ(moved.position(), 2);           // carries on where it was
        CHECK_EQ(moved.readUInt16(), 0x5678);

        Teltonika original{frame};
        Teltonika taken{std::move(original)};
        CHECK_EQ(taken.IsValid(), true);
        CHECK_EQ(taken.Items(), 1);
    }

    SECTION("Every record's position, to the millisecond");
    {
        // The newer record first, as a device emptying its buffer may send them.
        const Teltonika avl{codec8e_frame({
            {1'760'000'060'250, 281234567, -259876543, 1500, 90, 9, 62},
            {1'760'000'000'000, 281200000, -259800000, 1490, 85, 0, 0},
        })};
        CHECK(avl.IsValid());
        CHECK_EQ(avl.Items(), 2);
        const auto &positions = avl.Positions();
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
        // GPSInfo() is still the last record in the frame.
        CHECK_EQ(avl.GPSInfo()["sat"].get<int>(), 0);
    }

    SECTION("IO values by AVL ID, so IDs sharing a name stay apart");
    {
        Teltonika avl{codec8e_frame({
            {1'760'000'000'000, 281200000, -259800000, 1490, 85, 9, 0,
             {{60, 450}, {110, 45}}, {{83, 12345}, {107, 2500}}},
        })};
        CHECK(avl.IsValid());
        const auto &records = avl.AVLData();
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
        const Teltonika avl{codec8e_frame({
            {1'760'000'060'250, 281234567, -259876543, 1500, 90, 9, 62,
             {{60, 450}, {10825, 65535}}, {{83, 12345}, {65000, 0xFFFFFFFF}}},
        })};
        CHECK(avl.IsValid());
        CHECK_EQ(int{avl.Codec()}, 142);
        const auto &records = avl.Records();
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
        CHECK_EQ(const_cast<Teltonika &>(avl).AVLData()[0].at(60), std::string{"4.50L/h"}); // the formatted ones as before

        Codec12 command{"getinfo"};
        const Teltonika message{command.Data()};
        CHECK_EQ(int{message.Codec()}, 12);
        CHECK_EQ(message.Messages().size(), std::size_t{1});
        if (!message.Messages().empty()) {
            CHECK_EQ(int{message.Messages()[0].type}, 5);
            CHECK_EQ(message.Messages()[0].text, std::string{"getinfo"});
        }
        CHECK_EQ(int{Teltonika{std::string(12, '\0')}.Codec()}, 0); // not read
    }

    TEST_SUMMARY();
}
