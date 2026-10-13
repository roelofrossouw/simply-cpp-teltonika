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
    };

    // A Codec 8 Extended frame holding records, without IO values.
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
            for (int i = 0; i < 7; ++i) put<std::uint16_t>(data, 0); // event IO, total, and no 1/2/4/8/X-byte values
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

    SECTION("Parsers and messages move but don't copy");
    {
        // A copy would free the string's stream twice.
        static_assert(!std::is_copy_constructible_v<BinaryParser>);
        static_assert(!std::is_copy_constructible_v<Teltonika>);
        static_assert(std::is_nothrow_move_constructible_v<BinaryParser>);
        static_assert(std::is_move_constructible_v<Teltonika>);

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

    TEST_SUMMARY();
}
