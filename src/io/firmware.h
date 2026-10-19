#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace logic::io {

// memory contents from a file: what goes where, and the symbols of an elf
struct firmware {
    struct segment {
        uint64_t address = 0;
        std::vector<uint8_t> bytes;
    };

    std::vector<segment> segments;
    // start address if the file has one
    bool has_entry = false;
    uint64_t entry = 0;
    // function and object symbols by address (elf only)
    std::map<uint64_t, std::string> symbols;

    // highest address + 1 of all segments, 0 if empty
    uint64_t end() const;
    // the bytes of [address, address + size), fill where nothing is loaded
    std::vector<uint8_t> flatten(uint64_t address, uint64_t size, uint8_t fill = 0xFF) const;
};

enum class firmware_format {
    // from the extension and the contents
    automatic,
    intel_hex,
    srecord,
    elf,
    binary
};

// parses the text or bytes of a firmware file. binary data is loaded at
// base. false with an error.
bool parse_firmware(const std::vector<uint8_t>& data, firmware_format format, firmware& out, std::string& error, uint64_t base = 0);

// reads a file, the format from its extension (.hex .ihx .srec .s19 .s28
// .s37 .elf .bin) or its contents
bool load_firmware(const std::filesystem::path& file, firmware& out, std::string& error, uint64_t base = 0);

}
