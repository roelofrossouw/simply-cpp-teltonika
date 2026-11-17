#include <sc/teltonika/command.h>
#include "byte_order.h"

#include <sstream>

using namespace std;

namespace sc::teltonika {

class command::implementation {
public:
    stringstream stream;
};

command::command(const string &text) : impl_(make_unique<implementation>()) {
    auto &stream = impl_->stream;
    uint32_t num = 0;
    stream.write(reinterpret_cast<char *>(&num), sizeof(num));

    num = text.size() + 8;
    num = byteswap(num);
    stream.write(reinterpret_cast<char *>(&num), sizeof(num));

    uint8_t byte = 12;
    stream.write(reinterpret_cast<char *>(&byte), sizeof(byte));

    byte = 1; // Sending 1 command
    stream.write(reinterpret_cast<char *>(&byte), sizeof(byte));

    byte = 5; // Command (6 -> response)
    stream.write(reinterpret_cast<char *>(&byte), sizeof(byte));

    num = text.size();
    num = byteswap(num);
    stream.write(reinterpret_cast<char *>(&num), sizeof(num));

    stream << text;

    byte = 1; // Repeat Sending 1 command
    stream.write(reinterpret_cast<char *>(&byte), sizeof(byte));

    num = crc(stream.str().substr(8));
    num = byteswap(num);
    stream.write(reinterpret_cast<char *>(&num), 4);
}

uint32_t command::crc(const string &data) {
    uint32_t crc = 0;
    for (uint8_t byte: data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001; // reversed polynomial
            else
                crc >>= 1;
        }
    }
    return crc;
}

command::~command() = default;
command::command(command &&) noexcept = default;
command &command::operator=(command &&) noexcept = default;

std::string command::bytes() const {
    return impl_->stream.str();
}

}
