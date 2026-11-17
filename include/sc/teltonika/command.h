#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace sc::teltonika {
    // A Codec 12 command for a device ("getinfo", "setdigout 1 60"): bytes() is the whole frame to send.
    class command {
    public:
        explicit command(const std::string &text);
        ~command();

        command(const command &) = delete;
        command &operator=(const command &) = delete;
        command(command &&) noexcept;
        command &operator=(command &&) noexcept;

        // The frame as the device expects it: preamble, length, codec 12, the command and its CRC.
        [[nodiscard]] std::string bytes() const;

        // CRC-16/IBM of data, as Teltonika frames carry it.
        [[nodiscard]] static std::uint32_t crc(const std::string &data);

    private:
        // Kept out of the header, so the class stays one pointer in size whatever is added.
        class implementation;
        std::unique_ptr<implementation> impl_;
    };
}
