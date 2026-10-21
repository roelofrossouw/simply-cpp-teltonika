//
// Created by Roelof Rossouw on 2025/11/21.
//

#include "BinaryParser.h"
#include "byte_order.h"

#include <sstream>
#include <utility>

using namespace std;

class BinaryParser::implementation {
public:
    implementation(istream &input, const Endian endian) : stream(input), endian(endian) {
    }

    implementation(const string &input, const Endian endian)
        : owned(make_unique<stringstream>(input)), stream(*owned), endian(endian) {
    }

    unique_ptr<stringstream> owned; // the string constructor's stream
    istream &stream;
    size_t position = 0;
    Endian endian;

    template<typename T>
    T read() {
        T value;
        if (!stream.read(reinterpret_cast<char *>(&value), sizeof(T))) {
            throw runtime_error("Unexpected EOF while reading primitive");
        }
        position += sizeof(T);
        return value;
    }

    template<typename T>
    T read_ordered() {
        T value = read<T>();
        if (endian == Endian::Big) value = sc::teltonika::byteswap(value);
        return value;
    }
};

BinaryParser::BinaryParser(istream &input, Endian endian) : impl_(make_unique<implementation>(input, endian)) {
}

BinaryParser::BinaryParser(const string &input, Endian endian) : impl_(make_unique<implementation>(input, endian)) {
}

// The implementation stays where it is, so its stream reference stays good after a move.
BinaryParser::BinaryParser(BinaryParser &&other) noexcept = default;
BinaryParser &BinaryParser::operator=(BinaryParser &&other) noexcept = default;
BinaryParser::~BinaryParser() = default;

uint8_t BinaryParser::readUInt8() {
    return impl_->read<uint8_t>();
}

uint16_t BinaryParser::readUInt16() {
    return impl_->read_ordered<uint16_t>();
}

uint32_t BinaryParser::readUInt32() {
    return impl_->read_ordered<uint32_t>();
}

uint64_t BinaryParser::readUInt64() {
    return impl_->read_ordered<uint64_t>();
}

int8_t BinaryParser::readInt8() {
    return impl_->read<int8_t>();
}

int16_t BinaryParser::readInt16() {
    return impl_->read_ordered<int16_t>();
}

int32_t BinaryParser::readInt32() {
    return impl_->read_ordered<int32_t>();
}

int64_t BinaryParser::readInt64() {
    return impl_->read_ordered<int64_t>();
}

double BinaryParser::readNumber(int bytes, bool is_signed) {
    if (is_signed) {
        switch (bytes) {
            case 1: return readInt8();
            case 2: return readInt16();
            case 4: return readInt32();
            case 8: return readInt64();
            default: return 0;
        }
    }
    switch (bytes) {
        case 1: return readUInt8();
        case 2: return readUInt16();
        case 4: return readUInt32();
        case 8: return readUInt64();
        default: return 0;
    }
}

string BinaryParser::readString(size_t length) {
    auto bytes = readBytes(length);
    return {bytes.begin(), bytes.end()};
}

vector<uint8_t> BinaryParser::readBytes(const size_t count) {
    vector<uint8_t> buffer(count);
    if (!impl_->stream.read(reinterpret_cast<char *>(buffer.data()), count)) {
        throw runtime_error("Unexpected EOF while reading bytes");
    }
    impl_->position += count;
    return buffer;
}

int BinaryParser::position() const {
    return static_cast<int>(impl_->position);
}

void BinaryParser::seek(const size_t pos) {
    impl_->stream.clear();
    impl_->stream.seekg(pos, ios::beg);
    if (!impl_->stream) throw runtime_error("Seek failed");
    impl_->position = pos;
}

void BinaryParser::skip(size_t count) {
    seek(impl_->position + count);
}
