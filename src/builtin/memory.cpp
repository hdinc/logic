#include "builder.h"
#include "builtin.h"
#include "../io/firmware.h"

#include <cstring>

namespace logic::builtin {

namespace {

using model::direction;
using model::grid;
using sim::value;

// d, clk, q. q takes d on the rising edge of clk, starts at 0.
class register_component : public sim::component {
public:
    explicit register_component(int width)
        : width(width)
    {
        reset();
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto clk = ports[1].get(0);
        bool rising = prev_clk == sim::bit_state::zero && clk == sim::bit_state::one;
        prev_clk = clk;

        if (rising) {
            auto& d = ports[0];
            for (int w = 0; w < q.words(); w++)
                q.set_word(w, d.bits(w), d.unknown_bits(w), 0);
        }
        ports[2] = q;
    }

    void reset() override
    {
        q = sim::known(0, width);
        prev_clk = sim::bit_state::z;
    }

    void inspect(std::vector<sim::inspect_item>& out) const override { out.push_back({"q", q}); }

    // q's words (bits, x) and the clock
    bool save_state(std::vector<uint8_t>& out) const override
    {
        out.clear();
        auto put = [&](uint64_t v) {
            for (int k = 0; k < 8; k++)
                out.push_back(uint8_t(v >> (8 * k)));
        };
        for (int w = 0; w < q.words(); w++) {
            put(q.bits(w));
            put(q.x(w));
        }
        out.push_back((uint8_t)prev_clk);
        return true;
    }

    bool load_state(std::span<const uint8_t> in) override
    {
        if (in.size() != size_t(16 * q.words() + 1)) return false;
        auto get = [&](size_t at) {
            uint64_t v = 0;
            for (int k = 0; k < 8; k++)
                v |= uint64_t(in[at + k]) << (8 * k);
            return v;
        };
        for (int w = 0; w < q.words(); w++)
            q.set_word(w, get(16 * w), get(16 * w + 8), 0);
        prev_clk = (sim::bit_state)(in.back() & 3);
        return true;
    }

private:
    int width;
    value q;
    sim::bit_state prev_clk = sim::bit_state::z;
};

class reg : public primitive {
public:
    std::string kind() const override { return "register"; }
    std::string name() const override { return "Register"; }
    std::string category() const override { return "Memory"; }
    std::string description() const override { return "stores D on the rising edge of the clock"; }

    std::vector<model::param_def> params() const override
    {
        return {model::int_param("width", "Bit width", 8, 1, model::max_width)};
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int width = (int)model::get_int(v, "width");

        builder b;
        b.rect({-2 * grid, -3 * grid}, {2 * grid, 3 * grid});

        b.line({-3 * grid, grid}, {-2 * grid, grid});
        b.text({-18, 7}, "D", 6.0f);
        b.port("d", direction::input, width, {-3 * grid, grid}, "data, stored on the rising edge of clk");

        // clock input marked with a triangle
        b.line({-3 * grid, -grid}, {-2 * grid, -grid});
        b.polyline({{-2 * grid, -grid + 4}, {-14, -grid}, {-2 * grid, -grid - 4}});
        b.port("clk", direction::input, 1, {-3 * grid, -grid}, "clock, d is stored when it goes from 0 to 1");

        b.line({2 * grid, grid}, {3 * grid, grid});
        b.text({12, 7}, "Q", 6.0f);
        b.port("q", direction::output, width, {3 * grid, grid}, "the stored value, 0 after reset");

        if (width > 1) b.text({-6, -26}, std::to_string(width), 6.0f);
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<register_component>((int)model::get_int(v, "width"));
    }
};

// words of a rom or ram, little endian bytes per word for files and the
// debugger
class memory_words : public sim::debug_interface {
public:
    memory_words(std::string name, int addr_width, int data_width)
        : name(std::move(name))
        , data_width(data_width)
        , bytes_per_word((data_width + 7) / 8)
        , words(size_t(1) << addr_width, 0)
    {
    }

    // the contents of a firmware file, bytes per word little endian
    void load(const io::firmware& f)
    {
        initial.assign(words.size(), 0);
        auto bytes = f.flatten(0, initial.size() * bytes_per_word, 0);
        for (size_t w = 0; w < initial.size(); w++) {
            uint64_t v = 0;
            for (int k = 0; k < bytes_per_word; k++)
                v |= uint64_t(bytes[w * bytes_per_word + k]) << (8 * k);
            initial[w] = v & sim::mask(data_width);
        }
        words = initial;
    }

    void reset()
    {
        if (initial.empty()) std::fill(words.begin(), words.end(), 0);
        else words = initial;
    }

    // the word at a known address
    value read(const value& addr) const
    {
        if (!addr.fully_known()) return sim::unknown(data_width);
        return sim::known(words[addr.to_u64() % words.size()], data_width);
    }

    void write(const value& addr, const value& data)
    {
        if (!addr.fully_known()) return;
        words[addr.to_u64() % words.size()] = data.to_u64() & sim::mask(data_width);
    }

    std::vector<sim::memory_space> memories() const override { return {{name, words.size() * bytes_per_word}}; }

    bool read_memory(int space, uint64_t address, std::span<uint8_t> out) const override
    {
        if (space != 0 || address + out.size() > words.size() * bytes_per_word) return false;
        for (size_t k = 0; k < out.size(); k++) {
            auto byte = address + k;
            out[k] = uint8_t(words[byte / bytes_per_word] >> (8 * (byte % bytes_per_word)));
        }
        return true;
    }

    bool write_memory(int space, uint64_t address, std::span<const uint8_t> data) override
    {
        if (space != 0 || address + data.size() > words.size() * bytes_per_word) return false;
        for (size_t k = 0; k < data.size(); k++) {
            auto byte = address + k;
            auto& w = words[byte / bytes_per_word];
            int shift = 8 * int(byte % bytes_per_word);
            w = ((w & ~(uint64_t(0xFF) << shift)) | uint64_t(data[k]) << shift) & sim::mask(data_width);
        }
        return true;
    }

private:
    std::string name;
    int data_width;
    int bytes_per_word;
    std::vector<uint64_t> words;
    // from the file, empty if there is none
    std::vector<uint64_t> initial;
};

// loads the file parameter into the memory, false with an error
bool load_contents(memory_words& m, const sim::create_info& info, std::string& error)
{
    auto file = model::get_string(info.params, "file");
    if (file.empty()) return true;
    std::filesystem::path p = file;
    if (p.is_relative()) p = info.dir / p;
    io::firmware f;
    if (!io::load_firmware(p, f, error)) return false;
    m.load(f);
    return true;
}

// addr, cs -> data. cs 0 leaves data floating, not connected reads.
class rom_component : public sim::component {
public:
    rom_component(int addr_width, int data_width)
        : mem("rom", addr_width, data_width)
        , data_width(data_width)
    {
    }

    // addr, data, cs
    void eval(std::span<value> ports, sim::context&) override
    {
        auto cs = ports[2].get(0);
        if (cs == sim::bit_state::zero) ports[1] = sim::floating(data_width);
        else if (cs == sim::bit_state::x) ports[1] = sim::unknown(data_width);
        else ports[1] = mem.read(ports[0]);
    }

    void reset() override { mem.reset(); }
    sim::debug_interface* debug() override { return &mem; }

    memory_words mem;

private:
    int data_width;
};

// addr, data (inout), cs, we: with cs 1 it drives data with the word at
// addr while we is 0, and stores data while we is 1. starts with the file or
// zeros.
class ram_component : public sim::component {
public:
    ram_component(int addr_width, int data_width)
        : mem("ram", addr_width, data_width)
        , data_width(data_width)
    {
    }

    void eval(std::span<value> ports, sim::context&) override
    {
        auto cs = ports[2].get(0), we = ports[3].get(0);
        bool selected = cs == sim::bit_state::one || cs == sim::bit_state::z;
        if (!selected) {
            ports[1] = sim::floating(data_width);
        } else if (we == sim::bit_state::one) {
            // the net has what's written, this doesn't drive it
            mem.write(ports[0], ports[1]);
            ports[1] = sim::floating(data_width);
        } else if (we == sim::bit_state::zero || we == sim::bit_state::z) {
            ports[1] = mem.read(ports[0]);
        } else {
            ports[1] = sim::unknown(data_width);
        }
    }

    void reset() override { mem.reset(); }
    sim::debug_interface* debug() override { return &mem; }

    memory_words mem;

private:
    int data_width;
};

class memory_primitive : public primitive {
public:
    explicit memory_primitive(bool writable)
        : writable(writable)
    {
    }

    std::string kind() const override { return writable ? "ram" : "rom"; }
    std::string name() const override { return writable ? "RAM" : "ROM"; }
    std::string category() const override { return "Memory"; }
    std::string description() const override
    {
        return writable ? "memory on a data bus: reads with we 0, writes with we 1, cs 0 lets go of the bus" : "read only memory with the contents of a file";
    }

    std::vector<model::param_def> params() const override
    {
        return {
            model::int_param("addr_width", "Address bits", 8, 1, 24),
            model::int_param("data_width", "Data bits", 8, 1, 64),
            model::text_param("file", "Contents", "",
                "intel hex, s-records, elf or binary, relative to the library folder. bytes per word little endian"),
        };
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int aw = (int)model::get_int(v, "addr_width"), dw = (int)model::get_int(v, "data_width");
        auto stroke = [](int w) { return w > 1 ? model::bus_stroke : 0.0f; };
        builder b;
        b.rect({-3 * grid, -3 * grid}, {3 * grid, 3 * grid});
        b.text({-12, 15}, writable ? "RAM" : "ROM", 7.0f);

        b.line({-4 * grid, grid}, {-3 * grid, grid}, stroke(aw));
        b.text({-27, 8}, "A", 5.0f);
        b.port("addr", direction::input, aw, {-4 * grid, grid}, "address");

        b.line({3 * grid, grid}, {4 * grid, grid}, stroke(dw));
        b.text({23, 8}, "D", 5.0f);
        b.port("data", writable ? direction::inout : direction::output, dw, {4 * grid, grid}, writable ? "data bus" : "the word at the address");

        b.line({-4 * grid, -grid}, {-3 * grid, -grid});
        b.text({-27, -12}, "cs", 5.0f);
        b.port("cs", direction::input, 1, {-4 * grid, -grid}, "1 or not connected: selected");

        if (writable) {
            b.line({-4 * grid, -2 * grid}, {-3 * grid, -2 * grid});
            b.text({-27, -22}, "we", 5.0f);
            b.port("we", direction::input, 1, {-4 * grid, -2 * grid}, "1 writes data, 0 reads");
        }
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values&) const override { return nullptr; }

    std::unique_ptr<sim::component> create(const sim::create_info& info, std::string& error) const override
    {
        int aw = (int)model::get_int(info.params, "addr_width"), dw = (int)model::get_int(info.params, "data_width");
        if (writable) {
            auto c = std::make_unique<ram_component>(aw, dw);
            if (!load_contents(c->mem, info, error)) return nullptr;
            return c;
        }
        auto c = std::make_unique<rom_component>(aw, dw);
        if (!load_contents(c->mem, info, error)) return nullptr;
        return c;
    }

private:
    bool writable;
};

}

void add_memory(model::primitive_registry& r)
{
    r.add(std::make_unique<reg>());
    r.add(std::make_unique<memory_primitive>(false));
    r.add(std::make_unique<memory_primitive>(true));
}

}
