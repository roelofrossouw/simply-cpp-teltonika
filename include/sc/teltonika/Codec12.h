#ifndef CODEC12_H
#define CODEC12_H
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>

class Codec12 {
public:
    explicit Codec12(const std::string &command);
    ~Codec12();

    Codec12(const Codec12 &) = delete;
    Codec12 &operator=(const Codec12 &) = delete;
    Codec12(Codec12 &&) noexcept;
    Codec12 &operator=(Codec12 &&) noexcept;

    static uint32_t CalcCRC(const std::string &data);

    friend std::ostream &operator<<(std::ostream &lhs, const Codec12 &c12);

    std::string Data() const;

private:
    // Kept out of the header, so the class stays one pointer in size whatever is added.
    class implementation;
    std::unique_ptr<implementation> impl_;
};


#endif //CODEC12_H
