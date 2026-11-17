# simply-cpp-teltonika

C++20 parser and encoder for Teltonika AVL Codec 8 Extended telemetry frames
and Codec 12 command frames.

**Documentation:** the [simply-cpp wiki](https://github.com/roelofrossouw/simply-cpp/wiki) has a [Teltonika guide](https://github.com/roelofrossouw/simply-cpp/wiki/Teltonika) and the [sc-teltonika reference](https://github.com/roelofrossouw/simply-cpp/wiki/Reference-sc-teltonika), plus [getting started](https://github.com/roelofrossouw/simply-cpp/wiki/Getting-Started) for the whole suite.

## Usage

```cmake
find_package(sc-teltonika CONFIG REQUIRED)

target_link_libraries(myapp PRIVATE sc::sc-teltonika-shared)
```

```cpp
#include <sc/teltonika/teltonika.h>
#include <sc/teltonika/command.h>

sc::teltonika::frame message{bytes_received};
if (message.is_valid()) {
    const auto gps = message.gps();
    for (auto& record : message.avl_data()) {
        const auto fuel_rate = record.find(110);              // AVL ID -> value with its unit, e.g. "4.5l/h"
        const auto by_name = sc::teltonika::avl_names(record); // {"Fuel Rate": "4.5l/h", ...}
    }
    for (const auto& raw : message.records()) {               // the numbers as the device sent them
        const auto why = sc::teltonika::event_name(raw.event_io);
    }
}

const auto to_send = sc::teltonika::command{"getinfo"}.bytes(); // a Codec 12 command frame
```

Everything is in `sc::teltonika`: `frame` (a received frame), `command` (a Codec 12 command to
send), the record and position structs, and free functions (`to_position`, `to_record`, `format_io`,
`gps_info`, `event_name`, `priority_name`, `decode_beacons`, `beacon_distance_m`, `avl_names`).

5.0.0 renamed the API (4.x: `Teltonika`, `Codec12`, `IsValid()`, `GPSInfo()`, `AVLData()`,
`Positions()`, `Codec()`, `Records()`, `Messages()`, `Codec12::Data()`/`CalcCRC()`, in the global
namespace) and made the binary parser and AVL table private. Parsing and formatting are unchanged:
tests/golden.cpp checks the whole API's output against 4.0.1's.

Frames that can't be read (a wrong length, an unknown codec, a read past the end) are reported
on stderr as warnings, written at `sc::log_level::warning` (the default) and above:
`sc::teltonika::log_level(sc::log_level::error)` keeps them quiet.
