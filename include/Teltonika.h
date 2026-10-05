#pragma once
#include "BinaryParser.h"
#include <chrono>
#include <vector>
#include <unordered_map>
#include <nlohmann/json_fwd.hpp>

using avl_record = std::unordered_map<std::string, std::string>;
using timepoint = std::chrono::system_clock::time_point;

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

    void ParseCodec();

    void Codec8e();

    void Codec12();

    timepoint TimeStamp();

    int ReadGPSData();

    avl_record ReadAVLData(int priority);

    static uint32_t CalcCRC(const std::string& data);

    static void DumpFile(const std::string& input);
};
