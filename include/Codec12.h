//
// Created by Roelof Rossouw on 2025/12/01.
//

#ifndef CODEC12_H
#define CODEC12_H
#include <sstream>
#include <string>
#include <cstdint>

class Codec12 {
public:
    explicit Codec12(const std::string &command);

    static uint32_t CalcCRC(const std::string &data);

    friend std::ostream &operator<<(std::ostream &lhs, const Codec12 &c12);

    std::string Data() const;

private:
    std::string command_;
    std::stringstream stream;
};


#endif //CODEC12_H
