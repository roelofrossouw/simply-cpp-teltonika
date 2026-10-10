//
// Created by Roelof Rossouw on 2025/11/21.
//

#include "BinaryParser.h"
#include "byte_order.h"

#include <sstream>
#include <cstring>
#include <algorithm>
#include <utility>

using namespace std;

BinaryParser::BinaryParser(istream &input, Endian endian) : stream_(input), currentPos_(0), endian_(endian) {
}

BinaryParser::BinaryParser(const string &input, Endian endian) : stringStream_(new stringstream(input)),
                                                                 stream_(*stringStream_), currentPos_(0),
                                                                 endian_(endian) {
}

// The string's stream is on the heap, so stream_ still refers to it once it changes owner.
BinaryParser::BinaryParser(BinaryParser &&other) noexcept
    : stringStream_(std::exchange(other.stringStream_, nullptr)), stream_(other.stream_),
      currentPos_(other.currentPos_), endian_(other.endian_) {
}

BinaryParser::~BinaryParser() {
    delete stringStream_;
}

uint8_t BinaryParser::readUInt8() {
    return readPrimitive<uint8_t>();
}

uint16_t BinaryParser::readUInt16() {
    return adjustEndian(readPrimitive<uint16_t>());
}

uint32_t BinaryParser::readUInt32() {
    return adjustEndian(readPrimitive<uint32_t>());
}

uint64_t BinaryParser::readUInt64() {
    return adjustEndian(readPrimitive<uint64_t>());
}


int8_t BinaryParser::readInt8() {
    return readPrimitive<int8_t>();
}

int16_t BinaryParser::readInt16() {
    return adjustEndian(readPrimitive<int16_t>());
}

int32_t BinaryParser::readInt32() {
    return adjustEndian(readPrimitive<int32_t>());
}

int64_t BinaryParser::readInt64() {
    return adjustEndian(readPrimitive<int64_t>());
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
    return 0;
}

float BinaryParser::readFloat() {
    uint32_t raw = adjustEndian(readPrimitive<uint32_t>());
    float value;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

double BinaryParser::readDouble() {
    uint64_t raw = adjustEndian(readPrimitive<uint64_t>());
    double value;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

string BinaryParser::readString(size_t length) {
    auto bytes = readBytes(length);
    return {bytes.begin(), bytes.end()};
}

vector<uint8_t> BinaryParser::readBytes(const size_t count) {
    vector<uint8_t> buffer(count);
    if (!stream_.read(reinterpret_cast<char *>(buffer.data()), count)) {
        throw runtime_error("Unexpected EOF while reading bytes");
    }
    currentPos_ += count;
    return buffer;
}

int BinaryParser::position() const {
    return static_cast<int>(currentPos_);
}

void BinaryParser::seek(const size_t pos) {
    stream_.clear();
    stream_.seekg(pos, ios::beg);
    if (!stream_) throw runtime_error("Seek failed");
    currentPos_ = pos;
}

void BinaryParser::skip(size_t count) {
    seek(currentPos_ + count);
}


template<typename T>
T BinaryParser::parseStruct() {
    T result;
    return result;
}

template<typename T>
T BinaryParser::readPrimitive() {
    T value;
    if (!stream_.read(reinterpret_cast<char *>(&value), sizeof(T))) {
        throw runtime_error("Unexpected EOF while reading primitive");
    }
    currentPos_ += sizeof(T);
    return value;
}

template<typename T>
T BinaryParser::adjustEndian(T value) {
    if (endian_ == Endian::Big) value = sc::teltonika::byteswap(value);
    return value;
}
