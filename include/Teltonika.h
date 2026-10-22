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
// AVLType.cpp for each ID's name, multiplier and unit). Keyed by ID because some IDs share a
// name: 60 and 110 are both "Fuel Rate" (0.01 L/h and 0.1 l/h).
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

// One IO element as the device sent it (since 3.3.0): its AVL ID, its size in bytes (1, 2, 4 or 8,
// or 0 for a variable-length one), and its value: `number` holds a fixed-size one's bits, unsigned
// (sign-extend from `size` for a signed AVL type), `bytes` a variable-length one's bytes.
struct avl_io_value {
    std::uint16_t id = 0;
    std::uint8_t size = 0;
    std::uint64_t number = 0;
    std::string bytes;
};

// One record as the device sent it, nothing scaled or formatted (since 3.3.0): the IDs are kept
// whether avlTypes knows them or not, as devices can use their own for user-defined inputs.
struct avl_raw_record {
    timepoint time;               // to the millisecond
    std::uint8_t priority = 0;    // 0 low, 1 high, 2 panic
    std::int32_t lng_e7 = 0;      // degrees x 10^7, signed
    std::int32_t lat_e7 = 0;
    std::int16_t alt = 0;         // metres
    std::uint16_t ang = 0;        // heading, degrees
    std::uint8_t sat = 0;         // satellites in use; 0 means no fix
    std::uint16_t speed = 0;      // km/h
    std::uint16_t event_io = 0;   // the IO that caused the record; 0 when none
    std::vector<avl_io_value> io;
};

// A Codec 12 message's command or answer (since 3.3.0): type 5 is a command to the device, 6 its
// answer; text as sent.
struct codec12_message {
    std::uint8_t type = 0;
    std::string text;
};

// The record keyed by name instead (avlTypes' names). Where IDs share a name, the highest ID's
// value is kept; unknown IDs are all "Unknown".
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

    Teltonika(const Teltonika&) = delete;
    Teltonika& operator=(const Teltonika&) = delete;
    Teltonika(Teltonika&&) noexcept;
    Teltonika& operator=(Teltonika&&) noexcept;

    [[nodiscard]] bool IsValid() const;

    [[nodiscard]] uint8_t Items() const;

    [[nodiscard]] std::string CRC() const;

    [[nodiscard]] nlohmann::ordered_json GPSInfo() const;
    [[nodiscard]] std::vector<avl_record>& AVLData();

    // Every record's position, in the order they were in the frame (devices send their oldest
    // first, but buffered records can make that vary: compare the times). GPSInfo() is the last.
    [[nodiscard]] const std::vector<avl_position>& Positions() const;

    // The frame's codec: 142 for Codec 8 Extended, 12 for Codec 12, ...; 0 when it wasn't read (a
    // bad CRC or length). Since 3.3.0.
    [[nodiscard]] std::uint8_t Codec() const;
    // Every record as the device sent it: raw numbers, every IO by its ID (Codec 8 Extended).
    // Since 3.3.0.
    [[nodiscard]] const std::vector<avl_raw_record>& Records() const;
    // A Codec 12 frame's commands or answers. Since 3.3.0.
    [[nodiscard]] const std::vector<codec12_message>& Messages() const;

private:
    // Everything else lives here, so the class stays one pointer in size whatever is added: a
    // program built against one version keeps working with the next.
    class implementation;
    std::unique_ptr<implementation> impl_;
};
