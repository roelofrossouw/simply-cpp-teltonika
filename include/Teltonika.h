#pragma once
#include "BinaryParser.h"
#include <log_level.h>
#include <memory>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>

// One record's IO values: AVL ID to its value, formatted with its unit (83 -> "1234.5l"; see
// AVLType.cpp for each ID's name, multiplier and unit). Keyed by ID since 3.0.0, because some IDs
// share a name: 60 and 110 are both "Fuel Rate" (0.01 L/h and 0.1 l/h).
using avl_record = std::map<std::uint16_t, std::string>;
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

// The record keyed by name instead (avlTypes' names), as AVLData() was before 3.0.0. Where IDs
// share a name, the highest ID's value is kept; unknown IDs are all "Unknown".
[[nodiscard]] nlohmann::ordered_json avl_names(const avl_record& record);

enum DataType
{
    Connect,
    Ping,
    Data,
    Disconnect
};

namespace sc::teltonika {
    // What Teltonika writes to stderr: frames it can't read (a wrong length, an unknown codec, a
    // read past the end) are warnings, written at warning (the default) and above, so
    // log_level(sc::log_level::error) keeps them quiet.
    void log_level(sc::log_level level);
    [[nodiscard]] sc::log_level log_level();
}

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
