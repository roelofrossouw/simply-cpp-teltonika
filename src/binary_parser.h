#pragma once
// Private: reads the numbers and bytes a frame is made of. Not installed.
#include <cstdint>
#include <istream>
#include <memory>
#include <string>
#include <vector>

namespace sc::teltonika {
    enum class endian {
        little,
        big
    };

    class binary_parser {
    public:
        explicit binary_parser(std::istream &input, endian order = endian::little);

        explicit binary_parser(const std::string &input, endian order = endian::little);

        ~binary_parser();

        // A parser can be moved (it keeps reading where it was) but not copied: two parsers can't
        // share one read position.
        binary_parser(const binary_parser &) = delete;
        binary_parser &operator=(const binary_parser &) = delete;
        binary_parser(binary_parser &&other) noexcept;
        binary_parser &operator=(binary_parser &&other) noexcept;

        std::uint8_t read_uint8();

        std::uint16_t read_uint16();

        std::uint32_t read_uint32();

        std::uint64_t read_uint64();

        std::int8_t read_int8();

        std::int16_t read_int16();

        std::int32_t read_int32();

        std::int64_t read_int64();

        double read_number(int bytes, bool is_signed);

        std::string read_string(std::size_t length);

        std::vector<std::uint8_t> read_bytes(std::size_t count);

        int position() const;

        void seek(std::size_t pos);

        void skip(std::size_t count);

    private:
        class implementation;
        std::unique_ptr<implementation> impl_;
    };
}
