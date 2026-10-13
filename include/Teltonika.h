#pragma once
#include "BinaryParser.h"
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

    [[nodiscard]] bool IsValid() const { return valid_; }

    [[nodiscard]] uint8_t Items() const { return items; }

    [[nodiscard]] std::string CRC() const;

    static std::string Show(const std::string& s);

    static std::string Hex(const std::string& s);

    [[nodiscard]] nlohmann::ordered_json GPSInfo() const;
    [[nodiscard]] std::vector<avl_record>& AVLData() { return avl_data; }

    // Every record's position, in the order they were in the frame (devices send their oldest
    // first, but buffered records can make that vary: compare the times). GPSInfo() is the last.
    [[nodiscard]] const std::vector<avl_position>& Positions() const { return positions; }

private:
    BinaryParser parser;
    std::string crc_string;
    std::string crc_zeroes{'\0', '\0', '\0', '\0'};
    bool valid_ = false;
    uint8_t items = 0;
    timepoint timestamp{};
    double lng = 0;
    double lat = 0;
    int alt = 0;
    int ang = 0;
    int sat = 0;
    int speed = 0;
    std::vector<avl_record> avl_data{};
    std::vector<avl_position> positions{};

    void ParseCodec();

    void Codec8e();

    void Codec12();

    timepoint TimeStamp();

    int ReadGPSData();

    avl_record ReadAVLData(int priority);

    static uint32_t CalcCRC(const std::string& data);

    static void DumpFile(const std::string& input);
};
