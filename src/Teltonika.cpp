#include "Teltonika.h"
#include "AVLType.h"
#include "Codec12.h"
#include "byte_order.h"

#include <atomic>
#include <iomanip>
#include <iostream>
#include <ctime>
#include <cstring>
#include <nlohmann/json.hpp>
#include "base64.h"
#include "utf8.h"


using namespace std;

namespace {
    std::atomic<int> current_level{static_cast<int>(sc::log_level::warning)};

    bool warnings() {
        return current_level.load(std::memory_order_relaxed) >= static_cast<int>(sc::log_level::warning);
    }
}

void sc::teltonika::log_level(const sc::log_level level) {
    current_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

sc::log_level sc::teltonika::log_level() {
    return static_cast<sc::log_level>(current_level.load(std::memory_order_relaxed));
}

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

class Teltonika::implementation
{
public:
    explicit implementation(const std::string& input);

    BinaryParser parser;
    std::string crc_string;
    bool valid_ = false;
    uint8_t items = 0;
    std::vector<avl_record> avl_data{};
    std::vector<avl_position> positions{};

    void ParseCodec();
    void Codec8e();
    void Codec12();
    timepoint TimeStamp();
    void ReadGPSData();
    avl_record ReadAVLData();
};

Teltonika::implementation::implementation(const string& input) : parser(input, Endian::Big)
{
    try
    {
        if (input.size() < 12) return;

        crc_string = input.substr(input.size() - 4, 4);

        uint32_t read_crc = 0;
        std::memcpy(&read_crc, crc_string.data(), sizeof(read_crc));
        read_crc = sc::teltonika::byteswap(read_crc);

        parser.skip(4); // Preamble...
        const uint32_t data_length = parser.readUInt32();
        if (input.size() != data_length + 12)
        {
            if (warnings()) cerr << "Invalid data length: " << input.size() << " vs " << data_length << endl;
            return;
        }

        if (read_crc != Codec12::CalcCRC(input.substr(8, input.size() - 12))) return;

        valid_ = true;
        ParseCodec();
    }
    catch (const std::exception& e)
    {
        if (warnings()) cerr << "Error reading Teltonika data: " << e.what() << endl;
    }
}

Teltonika::Teltonika(const std::vector<std::uint8_t>& input) :
    Teltonika(std::string(input.begin(), input.end()))
{
}

Teltonika::Teltonika(const string& input) : impl_(make_unique<implementation>(input))
{
}

Teltonika::~Teltonika() = default;
Teltonika::Teltonika(Teltonika&&) noexcept = default;
Teltonika& Teltonika::operator=(Teltonika&&) noexcept = default;

bool Teltonika::IsValid() const
{
    return impl_->valid_;
}

uint8_t Teltonika::Items() const
{
    return impl_->items;
}

nlohmann::ordered_json avl_names(const avl_record& record)
{
    auto named = nlohmann::ordered_json::object();
    for (const auto& [io, value] : record) named[std::string(avlTypes::get(io).name)] = value;
    return named;
}

std::vector<avl_record>& Teltonika::AVLData()
{
    return impl_->avl_data;
}

const std::vector<avl_position>& Teltonika::Positions() const
{
    return impl_->positions;
}

std::string Teltonika::CRC() const
{
    return impl_->valid_ ? impl_->crc_string : std::string(4, '\0');
}

void Teltonika::implementation::ParseCodec()
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
    if (warnings()) cerr << "Unknown Codec: " << (int)codec << endl;
}


void Teltonika::implementation::Codec8e()
{
    uint8_t items1 = parser.readUInt8();
    for (int i = 0; i < items1; i++)
    {
        ReadGPSData();
        avl_data.push_back(ReadAVLData()); // Must be read to get to end and check crc.
    }
    uint8_t items2 = parser.readUInt8();
    items = (items1 == items2) ? items1 : 0; // Item count must match.
}

void Teltonika::implementation::Codec12()
{
    uint8_t items1 = parser.readUInt8();
    for (int i = 0; i < items1; i++)
    {
        parser.readUInt8(); // type
        parser.readString(parser.readUInt32());
    }
    uint8_t items2 = parser.readUInt8();
    items = (items1 == items2) ? items1 : 0; // Item count must match.
}


timepoint Teltonika::implementation::TimeStamp()
{
    // Milliseconds since the epoch.
    return timepoint{chrono::duration_cast<timepoint::duration>(chrono::milliseconds{parser.readUInt64()})};
}

void Teltonika::implementation::ReadGPSData()
{
    avl_position position;
    position.time = TimeStamp();
    parser.readUInt8(); // priority
    position.lng = parser.readInt32() / 10000000.0;
    position.lat = parser.readInt32() / 10000000.0;
    position.alt = parser.readUInt16();
    position.ang = parser.readUInt16();
    position.sat = parser.readUInt8();
    position.speed = parser.readUInt16();
    positions.push_back(position);
}

avl_record Teltonika::implementation::ReadAVLData()
{
    avl_record data{};
    parser.readUInt16(); // event IO ID
    parser.readUInt16(); // total IO count

    for (const int size : {1, 2, 4, 8, -1})
    {
        const int nitems = parser.readUInt16();
        for (int i = 0; i < nitems; ++i)
        {
            const int io = parser.readUInt16();
            auto val = avlTypes::get(io).ReadValue(parser, size);

            if (io == 548 || io == 385 || !sc::utf8::is_valid(val))
            {
                // Binary values go out as base64: 548 is Advanced BLE Beacon data, 385 BLE Beacon data.
                val = sc::base64::encode(val);
            }
            data[static_cast<std::uint16_t>(io)] = val;
        }
    }
    return data;
}

nlohmann::ordered_json Teltonika::GPSInfo() const
{
    if (impl_->positions.empty()) return {};
    const auto& d = impl_->positions.back();
    if (d.lat == 0) return {};
    return {
        {"gpstime", format_timestamp(d.time)},
        {"lat", d.lat},
        {"lng", d.lng},
        {"alt", d.alt},
        {"ang", d.ang},
        {"sat", d.sat},
        {"speed", d.speed}
    };
}
