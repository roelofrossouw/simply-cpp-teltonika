#ifndef AVLTYPE_H
#define AVLTYPE_H

#include <string_view>
#include <unordered_map>

#include "BinaryParser.h"

enum class AVLType
{
    Unsigned,
    Signed,
    Ascii,
    HEX
};

struct avl
{
    std::string_view name;
    AVLType type;
    float multiplier;
    std::string_view unit;
    [[nodiscard]] std::string Format(double value) const;
    const std::string ReadValue(BinaryParser& parser, int size) const;
    operator std::string() const { return std::string(name); }
    friend std::ostream& operator<<(std::ostream& os, const avl& avl);
};

// constexpr std::array<avl_descriptor, 1149> lookup{
//     {
//         {"Digital Input 1", AVLType::Unsigned, 1, ""},
//         {"Digital Input 2", AVLType::Unsigned, 1, ""}
//     }
// };

struct avlTypes
{
    static const std::unordered_map<int, const avl> types_;
    static const avl& get(int type);
};


#endif //AVLTYPE_H
