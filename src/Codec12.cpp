//
// Created by Roelof Rossouw on 2025/12/01.
//

#include "Codec12.h"

#include <absl/numeric/bits.h>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace std;

Codec12::Codec12(const string &command) : command_(command) {
    uint32_t num = 0;
    stream.write(reinterpret_cast<char *>(&num), sizeof(num));

    num = command.size() + 8;
    num = absl::byteswap(num);
    stream.write(reinterpret_cast<char *>(&num), sizeof(num));

    uint8_t byte = 12;
    stream.write(reinterpret_cast<char *>(&byte), sizeof(byte));

    byte = 1; // Sending 1 command
    stream.write(reinterpret_cast<char *>(&byte), sizeof(byte));

    byte = 5; // Command (6 -> response)
    stream.write(reinterpret_cast<char *>(&byte), sizeof(byte));

    num = command.size();
    num = absl::byteswap(num);
    stream.write(reinterpret_cast<char *>(&num), sizeof(num));

    stream << command;

    byte = 1; // Repeat Sending 1 command
    stream.write(reinterpret_cast<char *>(&byte), sizeof(byte));

    num = CalcCRC(stream.str().substr(8));
    num = absl::byteswap(num);
    stream.write(reinterpret_cast<char *>(&num), 4);
}

uint32_t Codec12::CalcCRC(const string &data) {
    vector<uint8_t> bytes(data.begin(), data.end());
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

std::string Codec12::Data() const {
    return stream.str();
}

ostream &operator<<(ostream &lhs, const Codec12 &c12) {
    stringstream ss;
    ss << hex << uppercase;
    for (uint8_t let: c12.stream.str())
        ss << setw(2) << setfill('0') << static_cast<int>(let);
    lhs << ss.str();
    return lhs;
}
