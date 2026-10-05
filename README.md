# simply-cpp-teltonika

C++20 parser and encoder for Teltonika AVL Codec 8 Extended telemetry frames
and Codec 12 command frames.

## Usage

```cmake
find_package(sc-teltonika CONFIG REQUIRED)

target_link_libraries(myapp PRIVATE sc::sc-teltonika-shared)
```

```cpp
#include <Teltonika.h>

Teltonika message{frame};
if (message.IsValid()) {
    const auto gps = message.GPSInfo();
    const auto& records = message.AVLData();
}
```
