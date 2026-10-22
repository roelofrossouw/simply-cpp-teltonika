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
    std::vector<avl_raw_record> records{};
    std::vector<codec12_message> messages{};
    std::uint8_t codec = 0;

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

std::uint8_t Teltonika::Codec() const
{
    return impl_->codec;
}

const std::vector<avl_raw_record>& Teltonika::Records() const
{
    return impl_->records;
}

const std::vector<codec12_message>& Teltonika::Messages() const
{
    return impl_->messages;
}

std::string Teltonika::CRC() const
{
    return impl_->valid_ ? impl_->crc_string : std::string(4, '\0');
}

void Teltonika::implementation::ParseCodec()
{
    codec = parser.readUInt8();
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
        codec12_message message;
        message.type = parser.readUInt8();
        message.text = parser.readString(parser.readUInt32());
        messages.push_back(std::move(message));
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
    avl_raw_record raw;
    raw.time = position.time = TimeStamp();
    raw.priority = parser.readUInt8();
    raw.lng_e7 = parser.readInt32();
    raw.lat_e7 = parser.readInt32();
    raw.alt = static_cast<std::int16_t>(parser.readUInt16());
    raw.ang = parser.readUInt16();
    raw.sat = parser.readUInt8();
    raw.speed = parser.readUInt16();
    position.lng = raw.lng_e7 / 10000000.0;
    position.lat = raw.lat_e7 / 10000000.0;
    position.alt = static_cast<std::uint16_t>(raw.alt);
    position.ang = raw.ang;
    position.sat = raw.sat;
    position.speed = raw.speed;
    positions.push_back(position);
    records.push_back(std::move(raw));
}

avl_record Teltonika::implementation::ReadAVLData()
{
    avl_record data{};
    auto& raw = records.back(); // ReadGPSData made it
    raw.event_io = parser.readUInt16();
    parser.readUInt16(); // total IO count

    for (const int size : {1, 2, 4, 8, -1})
    {
        const int nitems = parser.readUInt16();
        for (int i = 0; i < nitems; ++i)
        {
            const int io = parser.readUInt16();
            const auto& type = avlTypes::get(io);
            avl_io_value value{static_cast<std::uint16_t>(io), static_cast<std::uint8_t>(size < 0 ? 0 : size), 0, {}};
            std::string val;
            if (size < 0)
            {
                value.bytes = parser.readString(static_cast<std::uint16_t>(parser.readUInt16()));
                val = value.bytes;
            }
            else
            {
                switch (size)
                {
                case 1: value.number = parser.readUInt8(); break;
                case 2: value.number = parser.readUInt16(); break;
                case 4: value.number = parser.readUInt32(); break;
                default: value.number = parser.readUInt64(); break;
                }
                // As ReadValue() reads it: signed when the type is, then scaled with its unit.
                const int bits = size * 8;
                const auto as_signed = bits == 64 ? static_cast<std::int64_t>(value.number)
                                                  : static_cast<std::int64_t>(value.number << (64 - bits)) >> (64 - bits);
                val = type.Format(type.type == AVLType::Signed ? static_cast<double>(as_signed) : static_cast<double>(value.number));
            }
            raw.io.push_back(std::move(value));

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
