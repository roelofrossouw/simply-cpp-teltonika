#include "Teltonika.h"
#include "AVLType.h"
#include "Codec12.h"
#include "byte_order.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
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
    position = to_position(raw);
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
            avl_io_value value{static_cast<std::uint16_t>(io), static_cast<std::uint8_t>(size < 0 ? 0 : size), 0, {}};
            if (size < 0)
            {
                value.bytes = parser.readString(static_cast<std::uint16_t>(parser.readUInt16()));
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
            }
            data[value.id] = format_io(value);
            raw.io.push_back(std::move(value));
        }
    }
    return data;
}

avl_position to_position(const avl_raw_record& record)
{
    avl_position position;
    position.time = record.time;
    position.lng = record.lng_e7 / 10000000.0;
    position.lat = record.lat_e7 / 10000000.0;
    position.alt = static_cast<std::uint16_t>(record.alt);
    position.ang = record.ang;
    position.sat = record.sat;
    position.speed = record.speed;
    return position;
}

std::string format_io(const avl_io_value& value)
{
    const auto& type = avlTypes::get(value.id);
    std::string text;
    if (value.size == 0)
    {
        text = value.bytes;
    }
    else
    {
        // As ReadValue() reads it: signed when the type is, then scaled with its unit.
        const int bits = value.size * 8;
        const auto as_signed = bits >= 64 ? static_cast<std::int64_t>(value.number)
                                          : static_cast<std::int64_t>(value.number << (64 - bits)) >> (64 - bits);
        text = type.Format(type.type == AVLType::Signed ? static_cast<double>(as_signed) : static_cast<double>(value.number));
    }
    if (value.id == 548 || value.id == 385 || !sc::utf8::is_valid(text))
    {
        // Binary values go out as base64: 548 is Advanced BLE Beacon data, 385 BLE Beacon data.
        text = sc::base64::encode(text);
    }
    return text;
}

avl_record to_record(const avl_raw_record& record)
{
    avl_record data{};
    for (const auto& value : record.io) data[value.id] = format_io(value);
    return data;
}

nlohmann::ordered_json gps_info(const avl_position& d)
{
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

nlohmann::ordered_json Teltonika::GPSInfo() const
{
    if (impl_->positions.empty()) return {};
    return gps_info(impl_->positions.back());
}

std::string event_name(const std::uint16_t event_io)
{
    // The event IOs devices are set up to report, by what they mean.
    static const std::unordered_map<std::uint16_t, std::string_view> events{
        {0, "Device Movement Event"}, {1, "Digital Input 1"}, {2, "Tilt and Tip"}, {3, "Panic"},
        {16, "Total Mileage"}, {25, "BLE Temperature 1"}, {30, "Vehicle Speed"}, {37, "Fuel Level Percent"},
        {83, "PTO State"}, {87, "Fuel Level"}, {113, "Battery Level"}, {116, "Power Cut"},
        {236, "Panic"}, {237, "Custom"}, {239, "Ignition Event"}, {240, "Movement"},
        {247, "Crash"}, {248, "Immobilizer"}, {249, "Jamming"}, {252, "Power Cut"},
        {253, "Harsh Driving Events"}, {254, "Harsh Driving Value"}, {255, "Speeding"}, {303, "Instant Movement"},
        {381, "Tilt and Tip"}, {385, "Beacon"}, {389, "OBD OEM Total Mileage"}, {390, "OBD OEM Fuel Level"},
        {548, "BLE Beacon Data"}, {10828, "Near Beacon Record"}, {10829, "Away Beacon Record"}, {10831, "Lost Beacon Record"},
    };
    if (const auto found = events.find(event_io); found != events.end()) return std::string(found->second);
    if (avlTypes::types_.contains(event_io)) return std::string(avlTypes::get(event_io).name);
    return "IO " + std::to_string(event_io);
}

std::string priority_name(const std::uint8_t priority)
{
    switch (priority)
    {
    case 0: return "low";
    case 1: return "high";
    case 2: return "panic";
    default: return "priority " + std::to_string(priority);
    }
}


namespace
{
    // A beacon value's bytes, read big-endian as Teltonika sends them; reading past the end throws.
    class beacon_reader
    {
    public:
        explicit beacon_reader(std::string_view data) : data_(data) {}

        [[nodiscard]] bool done() const { return at_ >= data_.size(); }
        [[nodiscard]] std::size_t left() const { return data_.size() - at_; }

        std::string_view take(const std::size_t n)
        {
            if (n > left()) throw std::out_of_range("beacon data ends early");
            const auto part = data_.substr(at_, n);
            at_ += n;
            return part;
        }

        std::uint64_t number(const std::size_t n)
        {
            std::uint64_t value = 0;
            for (const char c: take(n)) value = value << 8 | static_cast<unsigned char>(c);
            return value;
        }

        int signed_number(const std::size_t n)
        {
            const auto bits = static_cast<int>(n * 8);
            return static_cast<int>(static_cast<std::int64_t>(number(n) << (64 - bits)) >> (64 - bits));
        }

    private:
        std::string_view data_;
        std::size_t at_ = 0;
    };

    std::string hex(const std::string_view bytes)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string text;
        for (const char c: bytes)
        {
            text += digits[static_cast<unsigned char>(c) >> 4];
            text += digits[static_cast<unsigned char>(c) & 15];
        }
        return text;
    }

    std::string mac_text(const std::string_view bytes)
    {
        std::string text;
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            if (i) text += ':';
            text += hex(bytes.substr(i, 1));
        }
        return text;
    }

    // An identity as the advanced mode's ID field or the simple mode carries it: 6 bytes a MAC, 16
    // an Eddystone namespace and instance, 20 an iBeacon UUID, major and minor.
    void read_identity(ble_beacon& beacon, const std::string_view id)
    {
        beacon_reader in{id};
        if (id.size() == 6)
        {
            beacon.mac = mac_text(id);
        }
        else if (id.size() == 16)
        {
            beacon.protocol = "Eddystone";
            beacon.eddystone_namespace = hex(in.take(10));
            beacon.instance = hex(in.take(6));
        }
        else if (id.size() == 20)
        {
            beacon.protocol = "iBeacon";
            beacon.uuid = hex(in.take(16));
            beacon.major = static_cast<std::uint16_t>(in.number(2));
            beacon.minor = static_cast<std::uint16_t>(in.number(2));
        }
    }

    std::string eddystone_url(const int scheme, const std::string_view encoded)
    {
        static constexpr const char* schemes[] = {"http://www.", "https://www.", "http://", "https://"};
        static constexpr const char* expansions[] = {".com/", ".org/", ".edu/", ".net/", ".info/", ".biz/", ".gov/",
                                                     ".com", ".org", ".edu", ".net", ".info", ".biz", ".gov"};
        std::string url = scheme >= 0 && scheme < 4 ? schemes[scheme] : "";
        for (const char c: encoded)
        {
            const auto code = static_cast<unsigned char>(c);
            if (code < std::size(expansions)) url += expansions[code];
            else url += c;
        }
        return url;
    }

    // A BLE advertisement's structures (length, type, data). False when it isn't one: the lengths
    // don't add up to its end (zero padding after the last allowed).
    bool read_advertisement(ble_beacon& beacon, const std::string_view advertisement)
    {
        std::vector<std::pair<int, std::string_view>> parts;
        std::size_t at = 0;
        while (at < advertisement.size())
        {
            const auto length = static_cast<unsigned char>(advertisement[at]);
            if (length == 0)
            {
                if (advertisement.find_first_not_of('\0', at) != std::string_view::npos) return false;
                break;
            }
            if (at + 1 + length > advertisement.size()) return false;
            parts.emplace_back(static_cast<unsigned char>(advertisement[at + 1]), advertisement.substr(at + 2, length - 1));
            at += 1 + length;
        }
        if (parts.empty()) return false;
        for (const auto& [type, data]: parts)
        {
            beacon_reader in{data};
            if (type == 0x16 && data.size() >= 3 && data.substr(0, 2) == std::string_view{"\xaa\xfe", 2})
            {
                in.take(2);
                const auto frame = in.number(1);
                if (frame == 0x00 && in.left() >= 17) // UID: transmit power at 0 m, namespace, instance
                {
                    beacon.protocol = "Eddystone";
                    beacon.rssi_at_1m = in.signed_number(1) - 41; // 41 dB lost over the first metre
                    beacon.eddystone_namespace = hex(in.take(10));
                    beacon.instance = hex(in.take(6));
                }
                else if (frame == 0x10 && in.left() >= 2) // URL
                {
                    beacon.protocol = "Eddystone";
                    beacon.rssi_at_1m = in.signed_number(1) - 41;
                    const auto scheme = static_cast<int>(in.number(1));
                    beacon.url = eddystone_url(scheme, in.take(in.left()));
                }
                else if (frame == 0x20 && in.left() >= 13 && in.number(1) == 0) // TLM, unencrypted
                {
                    beacon.protocol = "Eddystone";
                    if (const auto mv = static_cast<int>(in.number(2)); mv) beacon.battery_mv = mv;
                    if (const auto fixed = in.signed_number(2); fixed != -0x8000) beacon.temperature_c = fixed / 256.0;
                }
            }
            else if (type == 0xff && data.size() >= 25 && data.substr(0, 4) == std::string_view{"\x4c\x00\x02\x15", 4})
            {
                in.take(4); // Apple's company id, iBeacon type and length
                beacon.protocol = "iBeacon";
                beacon.uuid = hex(in.take(16));
                beacon.major = static_cast<std::uint16_t>(in.number(2));
                beacon.minor = static_cast<std::uint16_t>(in.number(2));
                if (const auto power = in.signed_number(1); power < 0) beacon.rssi_at_1m = power; // 0: not set
            }
            else if ((type == 0x08 || type == 0x09) && beacon.name.empty())
            {
                beacon.name = std::string{data};
            }
        }
        return true;
    }

    // The simple mode (385): a byte of data parts, then each beacon: flags (0x20 iBeacon, else
    // Eddystone; 0x01 RSSI, 0x02 battery voltage, 0x04 temperature follow, in that order), its identity.
    void simple_beacons(std::vector<ble_beacon>& beacons, beacon_reader& in)
    {
        in.number(1); // this part (high half) of how many (low half)
        while (!in.done())
        {
            const auto flags = in.number(1);
            ble_beacon beacon;
            read_identity(beacon, in.take(flags & 0x20 ? 20 : 16));
            if (flags & 0x01) beacon.rssi = in.signed_number(1);
            if (flags & 0x02) beacon.battery_mv = static_cast<int>(in.number(2));
            if (flags & 0x04) beacon.temperature_c = in.signed_number(2);
            beacons.push_back(std::move(beacon));
        }
    }

    // The advanced mode (548, 10828, 10829, 10831): a header byte, then each beacon its length and
    // fields (id, length, value): 0 RSSI, 1 identity, 2 beacon data (its BLE advertisement; older
    // firmware: flags, 0x40 low battery, 0x80 a voltage byte follows), 5 name, 6 temperature (0.01
    // degree), 7 humidity, 8 magnet, 9 motion, 10 motion count, 11 pitch, 12 roll, 13 low battery,
    // 14 battery voltage (mV), 15 MAC.
    void advanced_beacons(std::vector<ble_beacon>& beacons, beacon_reader& in)
    {
        in.number(1); // header
        while (!in.done())
        {
            beacon_reader fields{in.take(in.number(1))};
            ble_beacon beacon;
            while (!fields.done())
            {
                const auto field = fields.number(1);
                const auto length = static_cast<std::size_t>(fields.number(1));
                const auto value = fields.take(length);
                beacon_reader v{value};
                switch (field)
                {
                    case 0: beacon.rssi = v.signed_number(1); break;
                    case 1: read_identity(beacon, value); break;
                    case 2:
                        if (!read_advertisement(beacon, value) && !value.empty())
                        {
                            const auto flags = v.number(1);
                            if (flags & 0x40) beacon.low_battery = true;
                            if (flags & 0x80 && !v.done()) beacon.battery_mv = static_cast<int>(v.number(1)) * 10 + 2000;
                        }
                        break;
                    case 5: beacon.name = std::string{value}; break;
                    case 6: beacon.temperature_c = v.signed_number(2) / 100.0; break;
                    case 7: beacon.humidity = static_cast<int>(v.number(1)); break;
                    case 8: beacon.magnet = v.number(1) != 0; break;
                    case 9: beacon.motion = v.number(1) != 0; break;
                    case 10: beacon.motion_count = static_cast<int>(v.number(2)); break;
                    case 11: beacon.pitch = v.signed_number(1); break;
                    case 12: beacon.roll = v.signed_number(2); break;
                    case 13: beacon.low_battery = v.number(1) != 0; break;
                    case 14: beacon.battery_mv = static_cast<int>(v.number(2)); break;
                    case 15: beacon.mac = mac_text(value); break;
                    default: break; // a field this version doesn't know
                }
            }
            beacons.push_back(std::move(beacon));
        }
    }
}

std::vector<ble_beacon> decode_beacons(const avl_io_value& value)
{
    std::vector<ble_beacon> beacons;
    beacon_reader in{value.bytes};
    try
    {
        if (value.id == 385) simple_beacons(beacons, in);
        else if (value.id == 548 || value.id == 10828 || value.id == 10829 || value.id == 10831) advanced_beacons(beacons, in);
    }
    catch (const std::out_of_range&)
    {
        // A value cut short: the beacons read whole stay.
    }
    return beacons;
}

double beacon_distance_m(const int rssi, const std::optional<int> rssi_at_1m)
{
    const double metres = std::pow(10.0, (rssi_at_1m.value_or(-59) - rssi) / 20.0);
    return std::max(0.1, std::round(metres * 10.0) / 10.0);
}
