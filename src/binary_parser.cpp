#include "binary_parser.h"
#include "byte_order.h"

#include <sstream>
#include <utility>

using namespace std;

namespace sc::teltonika {

class binary_parser::implementation {
public:
    implementation(istream &input, const endian order) : stream(input), order(order) {
    }

    implementation(const string &input, const endian order)
        : owned(make_unique<stringstream>(input)), stream(*owned), order(order) {
    }

    unique_ptr<stringstream> owned; // the string constructor's stream
    istream &stream;
    size_t position = 0;
    endian order;

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
        if (order == endian::big) value = byteswap(value);
        return value;
    }
};

binary_parser::binary_parser(istream &input, const endian order) : impl_(make_unique<implementation>(input, order)) {
}

binary_parser::binary_parser(const string &input, const endian order) : impl_(make_unique<implementation>(input, order)) {
}

// The implementation stays where it is, so its stream reference stays good after a move.
binary_parser::binary_parser(binary_parser &&other) noexcept = default;
binary_parser &binary_parser::operator=(binary_parser &&other) noexcept = default;
binary_parser::~binary_parser() = default;

uint8_t binary_parser::read_uint8() {
    return impl_->read<uint8_t>();
}

uint16_t binary_parser::read_uint16() {
    return impl_->read_ordered<uint16_t>();
}

uint32_t binary_parser::read_uint32() {
    return impl_->read_ordered<uint32_t>();
}

uint64_t binary_parser::read_uint64() {
    return impl_->read_ordered<uint64_t>();
}

int8_t binary_parser::read_int8() {
    return impl_->read<int8_t>();
}

int16_t binary_parser::read_int16() {
    return impl_->read_ordered<int16_t>();
}

int32_t binary_parser::read_int32() {
    return impl_->read_ordered<int32_t>();
}

int64_t binary_parser::read_int64() {
    return impl_->read_ordered<int64_t>();
}

double binary_parser::read_number(int bytes, bool is_signed) {
    if (is_signed) {
        switch (bytes) {
            case 1: return read_int8();
            case 2: return read_int16();
            case 4: return read_int32();
            case 8: return read_int64();
            default: return 0;
        }
    }
    switch (bytes) {
        case 1: return read_uint8();
        case 2: return read_uint16();
        case 4: return read_uint32();
        case 8: return read_uint64();
        default: return 0;
    }
}

string binary_parser::read_string(size_t length) {
    auto bytes = read_bytes(length);
    return {bytes.begin(), bytes.end()};
}

vector<uint8_t> binary_parser::read_bytes(const size_t count) {
    vector<uint8_t> buffer(count);
    if (!impl_->stream.read(reinterpret_cast<char *>(buffer.data()), count)) {
        throw runtime_error("Unexpected EOF while reading bytes");
    }
    impl_->position += count;
    return buffer;
}

int binary_parser::position() const {
    return static_cast<int>(impl_->position);
}

void binary_parser::seek(const size_t pos) {
    impl_->stream.clear();
    impl_->stream.seekg(pos, ios::beg);
    if (!impl_->stream) throw runtime_error("Seek failed");
    impl_->position = pos;
}

void binary_parser::skip(size_t count) {
    seek(impl_->position + count);
}
}
