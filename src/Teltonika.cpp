#include "Teltonika.h"
#include "AVLType.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <cstring>
#include <random>
#include <absl/numeric/bits.h>
#include <nlohmann/json.hpp>
#include "base64.h"
#include "utf8.h"


using namespace std;

namespace {
    std::string format_timestamp(const timepoint timestamp)
    {
        const auto seconds = std::chrono::floor<std::chrono::seconds>(timestamp);
        const auto value = std::chrono::system_clock::to_time_t(seconds);
        std::tm utc{};
        gmtime_r(&value, &utc);

        std::ostringstream output;
        output << std::put_time(&utc, "%FT%TZ");
        return output.str();
    }
}

void Teltonika::DumpFile(const std::string& input)
{
    // Generate a random filename
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;

    std::string filename = "file_" + std::to_string(dist(gen)) + ".txt";

    // Ensure uniqueness in current folder
    std::filesystem::path filepath = std::filesystem::current_path() / filename;
    while (std::filesystem::exists(filepath))
    {
        filename = "file_" + std::to_string(dist(gen)) + ".txt";
        filepath = std::filesystem::current_path() / filename;
    }

    // Write string to file
    std::ofstream outfile(filepath, std::ios::binary);
    outfile << input;
    outfile.close();
}

Teltonika::Teltonika(const string& input) : parser(input, Endian::Big)
{
    try
    {
        if (input.size() < 12) return;

        crc_string = input.substr(input.size() - 4, 4);

        uint32_t read_crc = 0;
        std::memcpy(&read_crc, crc_string.data(), sizeof(read_crc));
        read_crc = absl::byteswap(read_crc);

        parser.skip(4); // Preamble...
        const uint32_t data_length = parser.readUInt32();
        if (input.size() != data_length + 12)
        {
            cerr << "Invalid data length: " << input.size() << " vs " << data_length << endl;
            return;
        }

        if (read_crc != CalcCRC(input.substr(8, input.size() - 12))) return;

        valid_ = true;
        ParseCodec();
    }
    catch (const std::exception& e)
    {
        cerr << "Error reading Teltonika data: " << e.what() << endl;
        // DumpFile(input);
    }
}

Teltonika::Teltonika(const std::vector<std::uint8_t>& input) :
    Teltonika(std::string(input.begin(), input.end()))
{
}

std::string Teltonika::CRC() const
{
    return valid_ ? crc_string : crc_zeroes;
}

std::string Teltonika::Show(const std::string& s)
{
    stringstream ss;
    ss << hex << uppercase << setfill('0');
    for (uint8_t let : s)
        if (isprint(static_cast<char>(let)))
        {
            ss << static_cast<char>(let);
        }
        else
        {
            ss << " x" << setw(2) << static_cast<int>(let) << " ";
        }
    return ss.str();
}


void Teltonika::ParseCodec()
{
    int codec = parser.readUInt8();
    if (codec == 12)
    {
        Codec12();
        return;
    }
    if (codec == 142)
    {
        Codec8e();
        return;
    }
    cerr << "Unknown Codec: " << (int)codec << endl;
}


void Teltonika::Codec8e()
{
    uint8_t items1 = parser.readUInt8();
    for (int i = 0; i < items1; i++)
    {
        auto priority = ReadGPSData();
        avl_data.push_back(ReadAVLData(priority)); // Must be read to get to end and check crc.
    }
    uint8_t items2 = parser.readUInt8();
    items = (items1 == items2) ? items1 : 0; // Item count must match.
}

void Teltonika::Codec12()
{
    uint8_t items1 = parser.readUInt8();
    uint8_t type;
    uint32_t size;
    string command;
    for (int i = 0; i < items1; i++)
    {
        type = parser.readUInt8();
        // cout << "Command type " << type << endl;
        size = parser.readUInt32();
        command = parser.readString(size);
        // cout << "Got command " << command << endl;
    }
    uint8_t items2 = parser.readUInt8();
    items = (items1 == items2) ? items1 : 0; // Item count must match...
}


timepoint Teltonika::TimeStamp()
{
    auto tp = chrono::system_clock::from_time_t(static_cast<long>(parser.readUInt64()) / 1000);
    return tp;
}

int Teltonika::ReadGPSData()
{
    timestamp = TimeStamp();
    const int priority = parser.readUInt8();
    lng = parser.readInt32() / 10000000.0;
    lat = parser.readInt32() / 10000000.0;
    alt = parser.readUInt16();
    ang = parser.readUInt16();
    sat = parser.readUInt8();
    speed = parser.readUInt16();
    return priority;
}

avl_record Teltonika::ReadAVLData(int priority)
{
    avl_record data{};
    int evtio = parser.readUInt16();
    // Handle priority events differently? (priority > 0)
    int ntot = parser.readUInt16();
    int maxoutput = 0;

    int datasizes[] = {1, 2, 4, 8, -1};
    for_each(datasizes, datasizes + 5, [&](int size)
             {
                 int nitems = parser.readUInt16();
                 for (int i = 0; i < nitems; ++i)
                 {
                     int io = parser.readUInt16();
                     avl type = avlTypes::get(io);
                     auto val = type.ReadValue(parser, size);

                     if (io == 548 || io == 385 || !sc::utf8::is_valid(val))
                     {
                         // Encode binary data to base64
                         // 548 -> Advanced BLE Beacon data
                         // 385 -> BLE Beacon data
                         val = sc::base64::encode(val);
                     }
                     data[type] = val;
                 }
             }

    );
    return data;
}

uint32_t Teltonika::CalcCRC(const string& data)
{
    uint32_t crc = 0;
    for (uint8_t byte : data)
    {
        crc ^= byte;
        for (int i = 0; i < 8; ++i)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001; // reversed polynomial
            else
                crc >>= 1;
        }
    }
    return crc;
}


string Teltonika::Hex(const string& s)
{
    stringstream ss;
    ss << hex << uppercase;
    for (uint8_t let : s)
        ss << setw(2) << setfill('0') << static_cast<int>(let);
    return ss.str();
}

nlohmann::ordered_json Teltonika::GPSInfo() const
{
    if (lat == 0) return {};
    return {
        {"gpstime", format_timestamp(timestamp)},
        {"lat", lat},
        {"lng", lng},
        {"alt", alt},
        {"ang", ang},
        {"sat", sat},
        {"speed", speed}
    };
}
