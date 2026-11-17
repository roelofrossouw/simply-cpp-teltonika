#pragma once
// Private: what each AVL ID is called, how its value is stored and scaled, and its unit. Not installed.
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>

#include "binary_parser.h"

namespace sc::teltonika {
    // How an AVL value is stored.
    enum class avl_type {
        unsigned_number,
        signed_number,
        ascii,
        hex
    };

    struct avl_info {
        std::string_view name;
        avl_type type;
        float multiplier;
        std::string_view unit;
        [[nodiscard]] std::string format(double value) const;
        std::string read_value(binary_parser &parser, int size) const;
        operator std::string() const { return std::string(name); }
        friend std::ostream &operator<<(std::ostream &os, const avl_info &info);
    };

    struct avl_types {
        static const std::unordered_map<int, const avl_info> types_;
        static const avl_info &get(int type);
    };
}
