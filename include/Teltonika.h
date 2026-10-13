#pragma once
#include "BinaryParser.h"
#include <memory>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <nlohmann/json_fwd.hpp>

using avl_record = std::unordered_map<std::string, std::string>;
using timepoint = std::chrono::system_clock::time_point;

// One record's position, as the device recorded it.
struct avl_position {
    timepoint time; // when the device recorded it, to the millisecond
    double lat = 0;
    double lng = 0;
    int alt = 0;   // metres
    int ang = 0;   // heading, degrees
    int sat = 0;   // satellites in use; 0 means no fix
    int speed = 0; // km/h
};

enum DataType
{
    Connect,
    Ping,
    Data,
    Disconnect
};

class Teltonika
{
public:
    explicit Teltonika(const std::string& input);
    explicit Teltonika(const std::vector<std::uint8_t>& input);
    ~Teltonika();

    // A parsed frame can be moved but not copied.
    Teltonika(const Teltonika&) = delete;
    Teltonika& operator=(const Teltonika&) = delete;
    Teltonika(Teltonika&&) noexcept;
    Teltonika& operator=(Teltonika&&) noexcept;

    [[nodiscard]] bool IsValid() const;

    [[nodiscard]] uint8_t Items() const;

    [[nodiscard]] std::string CRC() const;

    static std::string Show(const std::string& s);

    static std::string Hex(const std::string& s);

    [[nodiscard]] nlohmann::ordered_json GPSInfo() const;
    [[nodiscard]] std::vector<avl_record>& AVLData();

    // Every record's position, in the order they were in the frame (devices send their oldest
    // first, but buffered records can make that vary: compare the times). GPSInfo() is the last.
    [[nodiscard]] const std::vector<avl_position>& Positions() const;

private:
    // Everything else lives here, so the class stays one pointer in size whatever is added: a
    // program built against one version keeps working with the next.
    class implementation;
    std::unique_ptr<implementation> impl_;
};
