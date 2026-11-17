#pragma once
// Private: byte swapping for the frames' big-endian numbers. Not installed.

#include <bit>
#include <climits>
#include <concepts>
#include <cstddef>
#include <type_traits>

namespace sc::teltonika {
    template<std::integral T>
    constexpr T byteswap(T value) noexcept {
        using unsigned_type = std::make_unsigned_t<T>;

        auto input = std::bit_cast<unsigned_type>(value);
        unsigned_type output = 0;
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            output = static_cast<unsigned_type>((output << CHAR_BIT) | (input & 0xff));
            input >>= CHAR_BIT;
        }
        return std::bit_cast<T>(output);
    }
}

