#include "firmware.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace logic::io {

uint64_t firmware::end() const
{
    uint64_t e = 0;
    for (auto& s : segments)
        e = std::max(e, s.address + s.bytes.size());
    return e;
}

std::vector<uint8_t> firmware::flatten(uint64_t address, uint64_t size, uint8_t fill) const
{
    std::vector<uint8_t> r(size, fill);
    for (auto& s : segments) {
        for (size_t k = 0; k < s.bytes.size(); k++) {
            uint64_t a = s.address + k;
            if (a >= address && a < address + size) r[a - address] = s.bytes[k];
        }
    }
    return r;
}

namespace {

int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// bytes from hex digits, false if one isn't a digit
bool hex_bytes(const std::string& s, size_t from, std::vector<uint8_t>& out)
{
    out.clear();
    if ((s.size() - from) % 2) return false;
    for (size_t i = from; i + 1 < s.size(); i += 2) {
        int hi = hex_digit(s[i]), lo = hex_digit(s[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(uint8_t(hi << 4 | lo));
    }
    return true;
}

// adds bytes to the image, joining them to the segment they continue
void add(firmware& f, uint64_t address, const uint8_t* data, size_t n)
{
    if (n == 0) return;
    if (!f.segments.empty()) {
        auto& last = f.segments.back();
        if (last.address + last.bytes.size() == address) {
            last.bytes.insert(last.bytes.end(), data, data + n);
            return;
        }
    }
    f.segments.push_back({address, std::vector<uint8_t>(data, data + n)});
}

std::vector<std::string> lines_of(const std::vector<uint8_t>& data)
{
    std::vector<std::string> r;
    std::string cur;
    for (auto b : data) {
        char c = (char)b;
        if (c == '\n' || c == '\r') {
            if (!cur.empty()) r.push_back(cur);
            cur.clear();
        } else if (!std::isspace((unsigned char)c)) {
            cur += c;
        }
    }
    if (!cur.empty()) r.push_back(cur);
    return r;
}

bool parse_ihex(const std::vector<uint8_t>& data, firmware& f, std::string& error)
{
    uint64_t base = 0;
    int line_no = 0;
    std::vector<uint8_t> b;
    for (auto& line : lines_of(data)) {
        line_no++;
        auto where = "line " + std::to_string(line_no) + ": ";
        if (line[0] != ':' || !hex_bytes(line, 1, b) || b.size() < 5) {
            error = where + "not an intel hex record";
            return false;
        }
        size_t n = b[0];
        if (b.size() != n + 5) {
            error = where + "wrong length";
            return false;
        }
        uint8_t sum = 0;
        for (auto x : b)
            sum += x;
        if (sum != 0) {
            error = where + "wrong checksum";
            return false;
        }
        uint32_t offset = uint32_t(b[1]) << 8 | b[2];
        const uint8_t* payload = b.data() + 4;
        switch (b[3]) {
        case 0x00: add(f, base + offset, payload, n); break;
        case 0x01: return true;
        case 0x02:
            if (n != 2) return error = where + "bad segment address", false;
            base = (uint64_t(payload[0]) << 8 | payload[1]) << 4;
            break;
        case 0x03:
            if (n != 4) return error = where + "bad start address", false;
            f.has_entry = true;
            f.entry = (uint64_t(payload[0]) << 8 | payload[1]) * 16 + (uint64_t(payload[2]) << 8 | payload[3]);
            break;
        case 0x04:
            if (n != 2) return error = where + "bad linear address", false;
            base = (uint64_t(payload[0]) << 8 | payload[1]) << 16;
            break;
        case 0x05:
            if (n != 4) return error = where + "bad start address", false;
            f.has_entry = true;
            f.entry = uint64_t(payload[0]) << 24 | uint64_t(payload[1]) << 16 | uint64_t(payload[2]) << 8 | payload[3];
            break;
        default: error = where + "unknown record type " + std::to_string(b[3]); return false;
        }
    }
    return true;
}

bool parse_srec(const std::vector<uint8_t>& data, firmware& f, std::string& error)
{
    int line_no = 0;
    std::vector<uint8_t> b;
    for (auto& line : lines_of(data)) {
        line_no++;
        auto where = "line " + std::to_string(line_no) + ": ";
        if (line.size() < 4 || (line[0] != 'S' && line[0] != 's') || !std::isdigit((unsigned char)line[1]) || !hex_bytes(line, 2, b) || b.empty()) {
            error = where + "not an s-record";
            return false;
        }
        int type = line[1] - '0';
        if (b.size() != size_t(b[0]) + 1) {
            error = where + "wrong length";
            return false;
        }
        uint8_t sum = 0;
        for (auto x : b)
            sum += x;
        if (sum != 0xFF) {
            error = where + "wrong checksum";
            return false;
        }
        static const int address_bytes[10] = {2, 2, 3, 4, 0, 2, 3, 4, 3, 2};
        int ab = address_bytes[type];
        if (type == 4 || (int)b.size() < 2 + ab) {
            error = where + "bad record";
            return false;
        }
        uint64_t address = 0;
        for (int k = 0; k < ab; k++)
            address = address << 8 | b[1 + k];
        const uint8_t* payload = b.data() + 1 + ab;
        size_t n = b.size() - 2 - ab;
        if (type >= 1 && type <= 3) add(f, address, payload, n);
        else if (type >= 7) {
            f.has_entry = true;
            f.entry = address;
        }
    }
    return true;
}

// little or big endian numbers from the elf
struct elf_reader {
    const std::vector<uint8_t>& d;
    bool big;
    bool wide;

    bool has(uint64_t at, uint64_t n) const { return at <= d.size() && n <= d.size() - at; }
    uint64_t get(uint64_t at, int n) const
    {
        uint64_t v = 0;
        for (int k = 0; k < n; k++)
            v |= uint64_t(d[at + (big ? n - 1 - k : k)]) << (8 * k);
        return v;
    }
    uint64_t addr(uint64_t at) const { return get(at, wide ? 8 : 4); }
};

bool parse_elf(const std::vector<uint8_t>& d, firmware& f, std::string& error)
{
    if (d.size() < 52 || std::memcmp(d.data(), "\x7f" "ELF", 4) != 0) {
        error = "not an elf file";
        return false;
    }
    elf_reader r{d, d[5] == 2, d[4] == 2};
    if (r.wide && d.size() < 64) {
        error = "truncated elf header";
        return false;
    }

    f.has_entry = true;
    f.entry = r.addr(24);
    uint64_t phoff = r.addr(r.wide ? 32 : 28);
    uint64_t shoff = r.addr(r.wide ? 40 : 32);
    size_t base = r.wide ? 52 : 40;
    int phentsize = (int)r.get(base + 2, 2), phnum = (int)r.get(base + 4, 2);
    int shentsize = (int)r.get(base + 6, 2), shnum = (int)r.get(base + 8, 2);

    // loadable segments at their physical address
    for (int k = 0; k < phnum; k++) {
        uint64_t p = phoff + uint64_t(k) * phentsize;
        if (!r.has(p, phentsize)) return error = "truncated program header", false;
        if (r.get(p, 4) != 1) continue; // PT_LOAD
        uint64_t offset, paddr, filesz;
        if (r.wide) {
            offset = r.get(p + 8, 8);
            paddr = r.get(p + 24, 8);
            filesz = r.get(p + 32, 8);
        } else {
            offset = r.get(p + 4, 4);
            paddr = r.get(p + 12, 4);
            filesz = r.get(p + 16, 4);
        }
        if (filesz == 0) continue;
        if (!r.has(offset, filesz)) return error = "segment outside the file", false;
        f.segments.push_back({paddr, std::vector<uint8_t>(d.begin() + (long)offset, d.begin() + (long)(offset + filesz))});
    }

    // symbols of the first symbol table
    for (int k = 0; k < shnum; k++) {
        uint64_t s = shoff + uint64_t(k) * shentsize;
        if (!r.has(s, shentsize)) break;
        if (r.get(s + 4, 4) != 2) continue; // SHT_SYMTAB
        uint64_t off = r.wide ? r.get(s + 24, 8) : r.get(s + 16, 4);
        uint64_t size = r.wide ? r.get(s + 32, 8) : r.get(s + 20, 4);
        uint32_t link = (uint32_t)r.get(s + (r.wide ? 40 : 24), 4);
        uint64_t entsize = r.wide ? r.get(s + 56, 8) : r.get(s + 36, 4);
        uint64_t strs = shoff + uint64_t(link) * shentsize;
        if (entsize == 0 || !r.has(strs, shentsize)) break;
        uint64_t str_off = r.wide ? r.get(strs + 24, 8) : r.get(strs + 16, 4);
        uint64_t str_size = r.wide ? r.get(strs + 32, 8) : r.get(strs + 20, 4);
        for (uint64_t e = off; e + entsize <= off + size && r.has(e, entsize); e += entsize) {
            uint32_t name = (uint32_t)r.get(e, 4);
            uint64_t value;
            int info;
            if (r.wide) {
                info = d[e + 4];
                value = r.get(e + 8, 8);
            } else {
                value = r.get(e + 4, 4);
                info = d[e + 12];
            }
            int type = info & 15;
            // functions, objects and plain labels (notype, e.g. from assembly)
            if ((type != 0 && type != 1 && type != 2) || name == 0 || name >= str_size || !r.has(str_off + name, 1)) continue;
            std::string n;
            for (uint64_t c = str_off + name; c < d.size() && d[c]; c++)
                n += (char)d[c];
            if (!n.empty() && n[0] != '$' && !f.symbols.count(value)) f.symbols[value] = n;
        }
        break;
    }
    return true;
}

firmware_format detect(const std::vector<uint8_t>& data)
{
    if (data.size() >= 4 && std::memcmp(data.data(), "\x7f" "ELF", 4) == 0) return firmware_format::elf;
    size_t i = 0;
    while (i < data.size() && std::isspace(data[i]))
        i++;
    if (i < data.size() && data[i] == ':') return firmware_format::intel_hex;
    if (i + 1 < data.size() && (data[i] == 'S' || data[i] == 's') && std::isdigit(data[i + 1])) return firmware_format::srecord;
    return firmware_format::binary;
}

}

bool parse_firmware(const std::vector<uint8_t>& data, firmware_format format, firmware& out, std::string& error, uint64_t base)
{
    out = {};
    if (format == firmware_format::automatic) format = detect(data);
    switch (format) {
    case firmware_format::intel_hex: return parse_ihex(data, out, error);
    case firmware_format::srecord: return parse_srec(data, out, error);
    case firmware_format::elf: return parse_elf(data, out, error);
    case firmware_format::binary:
    case firmware_format::automatic: add(out, base, data.data(), data.size()); return true;
    }
    return false;
}

bool load_firmware(const std::filesystem::path& file, firmware& out, std::string& error, uint64_t base)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "can't open " + file.string();
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    auto ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    auto format = firmware_format::automatic;
    if (ext == ".hex" || ext == ".ihx" || ext == ".ihex") format = firmware_format::intel_hex;
    else if (ext == ".srec" || ext == ".s19" || ext == ".s28" || ext == ".s37" || ext == ".mot") format = firmware_format::srecord;
    else if (ext == ".elf") format = firmware_format::elf;
    else if (ext == ".bin" || ext == ".rom") format = firmware_format::binary;

    if (!parse_firmware(data, format, out, error, base)) {
        error = file.filename().string() + ": " + error;
        return false;
    }
    return true;
}

}
