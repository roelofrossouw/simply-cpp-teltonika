// Everything the public API gives, written out and hashed: whole frames (records, positions, GPS,
// IO values formatted and by name), every AVL ID formatted at every value size, event and priority
// names, beacons and Codec 12 commands. The hash was taken from sc-teltonika 4.0.1 before 5.0.0
// renamed the API, on macOS and on Ubuntu resolute, so 5.0.0 must parse and format exactly as 4.0.1
// did. `golden --dump` prints the text instead, to see what changed.
#include <sc/teltonika/command.h>
#include <sc/teltonika/teltonika.h>
#include <sc_test.h>

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

using namespace sc::teltonika;

namespace {
    template<typename T>
    void put(std::string &out, const T value) {
        for (int shift = (sizeof(T) - 1) * 8; shift >= 0; shift -= 8) out += static_cast<char>((static_cast<std::uint64_t>(value) >> shift) & 0xff);
    }

    std::string bytes(const std::string &hex) {
        std::string out;
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
        return out;
    }

    std::string hex(const std::string &data) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out;
        for (const unsigned char c: data) {
            out += digits[c >> 4];
            out += digits[c & 15];
        }
        return out;
    }

    struct io {
        std::uint16_t id;
        std::uint8_t size; // 1, 2, 4, 8 or 0 (variable)
        std::uint64_t number;
        std::string data;  // size 0
    };

    struct record {
        std::uint64_t time_ms;
        std::uint8_t priority;
        std::int32_t lng, lat;
        std::int16_t alt;
        std::uint16_t ang;
        std::uint8_t sat;
        std::uint16_t speed;
        std::uint16_t event_io;
        std::vector<io> values;
    };

    // A Codec 8 Extended frame with IO values of every size.
    std::string codec8e_frame(const std::vector<record> &records) {
        std::string data;
        put<std::uint8_t>(data, 0x8E);
        put<std::uint8_t>(data, records.size());
        for (const auto &r: records) {
            put(data, r.time_ms);
            put(data, r.priority);
            put(data, r.lng);
            put(data, r.lat);
            put(data, r.alt);
            put(data, r.ang);
            put(data, r.sat);
            put(data, r.speed);
            put(data, r.event_io);
            put<std::uint16_t>(data, r.values.size());
            for (const std::uint8_t size: {1, 2, 4, 8}) {
                std::uint16_t count = 0;
                for (const auto &v: r.values) count += v.size == size;
                put(data, count);
                for (const auto &v: r.values) {
                    if (v.size != size) continue;
                    put(data, v.id);
                    for (int shift = (size - 1) * 8; shift >= 0; shift -= 8) data += static_cast<char>((v.number >> shift) & 0xff);
                }
            }
            std::uint16_t variable = 0;
            for (const auto &v: r.values) variable += v.size == 0;
            put(data, variable);
            for (const auto &v: r.values) {
                if (v.size != 0) continue;
                put(data, v.id);
                put<std::uint16_t>(data, v.data.size());
                data += v.data;
            }
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

    void dump_beacons(std::ostream &out, const std::vector<ble_beacon> &beacons) {
        for (const auto &b: beacons) {
            out << "  beacon mac=" << b.mac << " protocol=" << b.protocol << " uuid=" << b.uuid << " ns=" << b.eddystone_namespace
                << " instance=" << b.instance << " url=" << b.url << " name=" << b.name;
            const auto opt = [&out](const char *name, const auto &value) {
                out << ' ' << name << '=';
                if (value) out << *value; else out << '-';
            };
            opt("major", b.major);
            opt("minor", b.minor);
            opt("rssi", b.rssi);
            opt("rssi1m", b.rssi_at_1m);
            opt("battery", b.battery_mv);
            opt("temp", b.temperature_c);
            opt("lowbat", b.low_battery);
            opt("humidity", b.humidity);
            opt("magnet", b.magnet);
            opt("motion", b.motion);
            opt("motions", b.motion_count);
            opt("pitch", b.pitch);
            opt("roll", b.roll);
            out << '\n';
        }
    }

    void dump_frame(std::ostream &out, const std::string &data) {
        frame parsed{data};
        out << "frame " << hex(data).substr(0, 64) << " valid=" << parsed.is_valid() << " items=" << int{parsed.items()}
            << " crc=" << parsed.crc() << " codec=" << int{parsed.codec()} << '\n';
        out << " gps " << parsed.gps().dump() << '\n';
        for (const auto &p: parsed.positions())
            out << " position " << std::chrono::duration_cast<std::chrono::milliseconds>(p.time.time_since_epoch()).count() << ' ' << p.lat
                << ' ' << p.lng << ' ' << p.alt << ' ' << p.ang << ' ' << p.sat << ' ' << p.speed << '\n';
        for (const auto &record: parsed.avl_data()) {
            out << " record";
            for (const auto &[id, value]: record) out << ' ' << id << '=' << value;
            out << '\n' << " names " << avl_names(record).dump() << '\n';
        }
        for (const auto &r: parsed.records()) {
            out << " raw " << std::chrono::duration_cast<std::chrono::milliseconds>(r.time.time_since_epoch()).count() << ' '
                << int{r.priority} << ' ' << r.lng_e7 << ' ' << r.lat_e7 << ' ' << r.alt << ' ' << r.ang << ' ' << int{r.sat} << ' '
                << r.speed << ' ' << r.event_io << " event=" << event_name(r.event_io) << " priority=" << priority_name(r.priority) << '\n';
            const auto p = to_position(r);
            out << "  to_position " << p.lat << ' ' << p.lng << " gps " << gps_info(p).dump() << '\n';
            out << "  to_record";
            for (const auto &[id, value]: to_record(r)) out << ' ' << id << '=' << value;
            out << '\n';
            for (const auto &v: r.io) {
                out << "  io " << v.id << ' ' << int{v.size} << ' ' << v.number << ' ' << hex(v.bytes) << " -> " << format_io(v) << '\n';
                dump_beacons(out, decode_beacons(v));
            }
        }
        for (const auto &m: parsed.messages()) out << " message " << int{m.type} << ' ' << m.text << '\n';
    }

    std::string dump() {
        std::ostringstream out;

        // Every AVL ID, at every fixed size, with values that show signs, scaling and units.
        for (std::uint32_t id = 0; id <= 12000; ++id)
            for (const std::uint8_t size: {1, 2, 4, 8})
                for (const std::uint64_t number: {0ull, 1ull, 7ull, 12345ull, 0xFFull, 0xFFF6ull, 0xFFFFFFF6ull, 0xFFFFFFFFFFFFFFF6ull}) {
                    const std::uint64_t mask = size == 8 ? ~0ull : (1ull << (size * 8)) - 1;
                    out << id << ' ' << int{size} << ' ' << (number & mask) << ' '
                        << format_io({static_cast<std::uint16_t>(id), size, number & mask, {}}) << '\n';
                }
        for (std::uint32_t id = 0; id <= 12000; ++id) out << "event " << id << ' ' << event_name(static_cast<std::uint16_t>(id)) << '\n';
        for (int p = 0; p < 256; ++p) out << "priority " << p << ' ' << priority_name(static_cast<std::uint8_t>(p)) << '\n';
        // Variable-length values of every ID that might take one.
        for (const std::uint16_t id: {385, 548, 10828, 10829, 10831, 256, 264, 281, 385, 1148, 11000})
            out << "variable " << id << ' ' << format_io({id, 0, 0, bytes("0102030405060708090A0B0C")}) << '\n';

        // Frames: a full record, a zero position, signed and big values, variable-length beacons.
        const std::string simple_beacons = bytes(
            "11214B5C049F515341FCA950D2C264414E1000050006BA21E2C56DB5DFFB48D2B060D0F5A71096E000000000A92131A74BB76A7942"
            "3196C916CFB9FAED45002D00159F0700112233445566778899ABCDE0810047AE0BE80015210F86676BEC91420A94409110029AFAC4"
            "15B31A0AA101DE9C18E92CA5AA689697365434663222BA21EBBBDE835D7F4965B5F06C2EDCB3A55300010080A501736B79686F7374"
            "2E646B000010000128AD");
        const std::string eddystone = "0001C6" "0106001122334455" "021F" "020106" "0303AAFE" "1716AAFE00E8"
                                      "0102030405060708090A" "0B0C0D0E0F10" "0000";
        const std::string ibeacon = "0001A3" "0106665544332211" "021E" "020106" "1AFF4C000215"
                                    "E2C56DB5DFFB48D2B060D0F5A71096E0" "0007" "0009" "C5";
        const std::string telemetry = "01" "40" "0001B0" "0106AABBCCDDEEFF" "0212" "1116AAFE20000BB81580000000010000000A"
                                      "0507" "736D6F6B6579" "21" "06020898" "070137" "080101" "09010A" "0A020003" "0B01FB" "0C02FFF6";
        std::vector<std::string> frames;
        frames.push_back(codec8e_frame({
            {1'760'000'060'250, 1, 281234567, -259876543, 1500, 90, 9, 62, 239,
             {{239, 1, 1, {}}, {21, 1, 4, {}}, {66, 2, 12800, {}}, {17, 2, 0xFFF6, {}}, {60, 2, 450, {}}, {83, 4, 12345, {}},
              {10825, 4, 7, {}}, {16, 4, 98765, {}}, {11, 8, 0x0123456789ABCDEFull, {}}, {78, 8, 0xFFFFFFFFFFFFFFFFull, {}},
              {385, 0, 0, simple_beacons}, {548, 0, 0, bytes("01" "2C" + eddystone + "2B" + ibeacon)}, {548, 0, 0, bytes(telemetry)},
              {10828, 0, 0, bytes("01" "12" "0001D5" "0F06001122334466" "0D0100" "0E020C12")}, {65000, 0, 0, bytes("CAFE")}}},
            {1'760'000'000'000, 0, 0, 0, 0, 0, 0, 0, 0, {}},
            {1'760'000'120'999, 2, -1800000000, 900000000, -42, 359, 255, 250, 247, {{247, 1, 2, {}}, {12, 4, 0xFFFFFFF6, {}}}},
        }));
        frames.push_back(codec8e_frame({}));
        frames.push_back(command{"getinfo"}.bytes());
        frames.push_back(command{"setdigout 1 60"}.bytes());
        frames.push_back(std::string{});
        frames.push_back(std::string(12, '\0'));
        std::string corrupt = frames[0];
        corrupt[corrupt.size() - 1] ^= 1;
        frames.push_back(corrupt);
        for (const auto &f: frames) dump_frame(out, f);

        for (const auto &text: {"getinfo", "getver", "setdigout 1 60", ""})
            out << "codec12 " << text << ' ' << hex(command{text}.bytes()) << ' ' << command::crc(text) << '\n';
        for (const int rssi: {-20, -59, -65, -71, -79, -100})
            for (const int at: {-59, -65})
                out << "distance " << rssi << ' ' << at << ' ' << beacon_distance_m(rssi, at) << ' ' << beacon_distance_m(rssi) << '\n';
        return out.str();
    }

    // FNV-1a, 64-bit: enough to tell two dumps apart.
    std::uint64_t fnv1a(const std::string &text) {
        std::uint64_t hash = 0xcbf29ce484222325ull;
        for (const unsigned char c: text) {
            hash ^= c;
            hash *= 0x100000001b3ull;
        }
        return hash;
    }
}

int main(const int argc, char **argv) {
    const auto text = dump();
    if (argc > 1 && std::string{argv[1]} == "--dump") {
        std::cout << text;
        return 0;
    }
    char hash[17];
    std::snprintf(hash, sizeof hash, "%016llx", static_cast<unsigned long long>(fnv1a(text)));
    std::cout << "golden " << hash << " (" << text.size() << " bytes)\n";

    SECTION("The whole public API gives what 4.0.1 gave");
    CHECK_EQ(std::string{hash}, std::string{"1896811d05b39b50"});

    TEST_SUMMARY();
}
