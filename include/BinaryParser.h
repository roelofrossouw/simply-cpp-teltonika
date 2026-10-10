//
// Created by Roelof Rossouw on 2025/11/21.
//

#ifndef BINARYPARSER_H
#define BINARYPARSER_H

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <string>
#include <istream>
#include <sstream>
#include <bit>
#include <utility>

enum class Endian {
    Little,
    Big
};


class BinaryParser {
public:
    // Constructor: accepts a stream or raw buffer
    explicit BinaryParser(std::istream &input, Endian endian = Endian::Little);

    // Constructor: accepts a string
    explicit BinaryParser(const std::string &input, Endian endian = Endian::Little);

    ~BinaryParser();

    // A parser can be moved (it keeps reading where it was) but not copied: two parsers can't
    // share one read position, and a copy would free the string's stream twice.
    BinaryParser(const BinaryParser &) = delete;
    BinaryParser &operator=(const BinaryParser &) = delete;
    BinaryParser(BinaryParser &&other) noexcept;
    BinaryParser &operator=(BinaryParser &&) = delete; // it holds a reference to its stream

    // Core API: read primitive types
    uint8_t readUInt8();

    uint16_t readUInt16();

    uint32_t readUInt32();

    uint64_t readUInt64();

    int8_t readInt8();

    int16_t readInt16();

    int32_t readInt32();

    int64_t readInt64();

    double readNumber(int bytes, bool is_signed);

    // Read Complex types
    float readFloat();

    double readDouble();

    std::string readString(std::size_t length);

    std::vector<uint8_t> readBytes(std::size_t count);

    template<typename T>
    T parseStruct();

    int position() const;

    void seek(std::size_t pos);

    void skip(std::size_t count);

    void setEndian(Endian endian) { endian_ = endian; }

private:
    std::stringstream *stringStream_ = nullptr;
    std::istream &stream_;
    std::size_t currentPos_;
    Endian endian_;

    template<typename T>
    T readPrimitive();

    template<class T>
    T adjustEndian(T value);
};


#endif //BINARYPARSER_H
