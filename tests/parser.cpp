#include <BinaryParser.h>
#include <Codec12.h>
#include <Teltonika.h>
#include <sc_test.h>

#include <string>
#include <type_traits>
#include <utility>

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

    TEST_SUMMARY();
}
