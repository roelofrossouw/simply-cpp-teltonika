#include <BinaryParser.h>
#include <Codec12.h>
#include <Teltonika.h>
#include <sc_test.h>

#include <string>

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

    TEST_SUMMARY();
}
