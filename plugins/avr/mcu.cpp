// the avr runs ahead of the simulation by at most run_ahead. it stops early
// when an output changes and puts the change out when the simulation gets
// to that time, so outputs come at their exact time and inputs are seen up
// to run_ahead late. sleeping skips to the next timer of simavr, or to the
// next input change if there is none.

#include "mcu.h"
#include "disasm.h"

#include "avr_eeprom.h"
#include "avr_ioport.h"
#include "avr_uart.h"
#include "sim_avr.h"
#include "sim_core.h"
#include "sim_cycle_timers.h"
#include "sim_interrupts.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>

namespace avr {

namespace {

// simavr's logger is global, it finds the instance by its avr
std::mutex log_mutex;
std::map<avr_t*, mcu*> log_targets;

void logger(avr_t* a, const int level, const char* format, va_list ap)
{
    if (level > LOG_WARNING) return;
    char buf[512];
    std::vsnprintf(buf, sizeof(buf), format, ap);
    std::lock_guard lk(log_mutex);
    auto it = log_targets.find(a);
    if (it != log_targets.end()) it->second->log(level, buf);
}

void on_uart_output(avr_irq_t*, uint32_t value, void* param)
{
    static_cast<mcu*>(param)->uart_output((uint8_t)value);
}

avr_io_t* find_io(avr_t* a, const char* kind)
{
    for (auto io = a->io_port; io; io = io->next)
        if (std::strcmp(io->kind, kind) == 0) return io;
    return nullptr;
}

}

struct mcu::port_state {
    avr_ioport_t* io;
    // the registers last seen
    uint8_t port = 0, ddr = 0;
    // pin index per bit, -1 if the bit isn't a pin of the model
    int pin[8];
};

mcu::mcu(const part& part_, const logic_params& params, const logic_host& h)
    : p(part_)
    , host(h)
{
    auto frequency = logic_param_int(&params, "frequency", p.default_frequency);
    if (frequency <= 0 || frequency > 1000000000) throw std::runtime_error("the frequency must be between 1 Hz and 1 GHz");
    period = LOGIC_S / (uint64_t)frequency;
    auto ahead = logic_param_int(&params, "run_ahead", 10);
    if (ahead < 0) throw std::runtime_error("run ahead can't be negative");
    run_ahead = std::max<uint64_t>(period, (uint64_t)ahead * LOGIC_US);

    static std::once_flag once;
    std::call_once(once, [] { avr_global_logger_set(logger); });

    a = avr_make_mcu_by_name(p.core);
    if (!a) throw std::runtime_error(std::string("simavr doesn't know ") + p.core);
    {
        std::lock_guard lk(log_mutex);
        log_targets[a] = this;
    }
    try {
        setup(frequency);
    } catch (...) {
        release();
        throw;
    }
    reset();
}

void mcu::setup(int64_t frequency)
{
    a->log = LOG_WARNING;
    if (avr_init(a) != 0) throw std::runtime_error(log_text.empty() ? "simavr can't initialize " + std::string(p.core) : log_text);
    a->frequency = (uint32_t)frequency;
    // pull-ups are done here, simavr's would hide the pins' levels
    a->options.no_pullups = 1;

    for (size_t i = 0; i < p.pins.size(); i++) {
        auto& d = p.pins[i];
        auto it = std::find_if(ports_.begin(), ports_.end(), [&](auto& s) { return s.io->name == d.port; });
        if (it == ports_.end()) {
            port_state s{};
            for (auto io = a->io_port; io; io = io->next)
                if (std::strcmp(io->kind, "port") == 0 && ((avr_ioport_t*)io)->name == d.port) s.io = (avr_ioport_t*)io;
            if (!s.io) throw std::runtime_error(std::string(p.core) + " has no port " + d.port);
            std::fill(std::begin(s.pin), std::end(s.pin), -1);
            ports_.push_back(s);
            it = ports_.end() - 1;
        }
        it->pin[d.bit] = (int)i;
    }

    if (p.uart) {
        for (auto io = a->io_port; io; io = io->next)
            if (std::strcmp(io->kind, "uart") == 0 && ((avr_uart_t*)io)->name == p.uart) uart = io;
        if (uart) {
            // no console output and no sleeping while polling
            uint32_t flags = 0;
            avr_ioctl(a, AVR_IOCTL_UART_SET_FLAGS(p.uart), &flags);
            uart_out = avr_io_getirq(a, AVR_IOCTL_UART_GETIRQ(p.uart), UART_IRQ_OUTPUT);
            uart_in = avr_io_getirq(a, AVR_IOCTL_UART_GETIRQ(p.uart), UART_IRQ_INPUT);
            avr_irq_register_notify(uart_out, on_uart_output, this);
        }
    }

    flash.assign(a->flashend + 1, 0xFF);
    eeprom.assign(a->e2end + 1, 0xFF);
    breakpoints.assign(a->flashend + 1, 0);
    input.assign(p.pins.size(), 3);
    drive.assign(p.pins.size(), false);
    level.assign(p.pins.size(), false);
    next_drive = drive;
    next_level = level;
}

mcu::~mcu() { release(); }

void mcu::release()
{
    if (!a) return;
    {
        std::lock_guard lk(log_mutex);
        log_targets.erase(a);
    }
    avr_terminate(a);
    std::free(a);
    a = nullptr;
}

void mcu::log(int lvl, const char* text)
{
    log_text = text;
    while (!log_text.empty() && (log_text.back() == '\n' || log_text.back() == '\r'))
        log_text.pop_back();
    if (host.log) host.log(host.data, lvl == LOG_ERROR ? LOGIC_LOG_ERROR : LOGIC_LOG_WARNING, log_text.c_str());
}

void mcu::reset()
{
    // power on: the firmware in flash, everything else cleared
    std::memcpy(a->flash, flash.data(), flash.size());
    std::memset(a->data, 0, a->ramend + 1);
    avr_reset(a);
    if (find_io(a, "eeprom") && !eeprom.empty()) {
        avr_eeprom_desc_t d{eeprom.data(), 0, (uint32_t)eeprom.size()};
        avr_ioctl(a, AVR_IOCTL_EEPROM_SET, &d);
    }
    for (auto& s : ports_)
        s.port = s.ddr = 0;
    std::fill(drive.begin(), drive.end(), false);
    std::fill(level.begin(), level.end(), false);
    next_drive = drive;
    next_level = level;
    std::fill(input.begin(), input.end(), 3);
    started = false;
    held = false;
    pending = false;
    pause_reason = nullptr;
    paused_at = LOGIC_NEVER;
    skip_breakpoint = false;
    step = false;
    tx.clear();
    rx.clear();
}

void mcu::restart(uint64_t now)
{
    base = now;
    started = true;
}

uint64_t mcu::time_of(uint64_t cycle) const { return base + cycle * period; }

uint64_t mcu::cycle_at(uint64_t t) const { return t <= base ? 0 : (t - base + period - 1) / period; }

void mcu::step_one()
{
    // simavr's avr_callback_run_raw without its sleeping: run() moves the
    // cycle on while the avr sleeps
    avr_flashaddr_t new_pc = a->pc;
    if (a->state == cpu_Running) new_pc = avr_run_one(a);
    avr_cycle_timer_process(a);
    a->pc = new_pc;
    if (a->state == cpu_Sleeping && !a->sreg[S_I]) {
        // nothing can wake it
        a->state = cpu_Done;
        return;
    }
    if ((a->state == cpu_Running || a->state == cpu_Sleeping) && a->interrupt_state) avr_service_interrupts(a);
}

void mcu::run(uint64_t until)
{
    while (!pending && !pause_reason) {
        if (a->state != cpu_Running && a->state != cpu_Sleeping) return;
        if (time_of(a->cycle) >= until) return;

        if (a->state == cpu_Running) {
            if (breakpoint_count && a->pc < breakpoints.size() && breakpoints[a->pc] && !skip_breakpoint) {
                // stops before the instruction, it runs when going on
                pause_reason = "breakpoint";
                skip_breakpoint = true;
                return;
            }
            step_one();
            skip_breakpoint = false;
            if (step) {
                step = false;
                pause_reason = "step";
            }
        } else {
            // sleeping: on to the next timer or until, whichever comes first
            auto t = a->cycle_timers.timer;
            if (!t) return;
            auto to = std::min<uint64_t>(t->when, cycle_at(until));
            if (to > a->cycle) a->cycle = to;
            step_one();
        }
        take_registers();
    }
}

void mcu::take_registers()
{
    bool changed = false;
    for (auto& s : ports_) {
        uint8_t port = a->data[s.io->r_port], ddr = a->data[s.io->r_ddr];
        if (port == s.port && ddr == s.ddr) continue;
        s.port = port;
        s.ddr = ddr;
        changed = true;
        for (int b = 0; b < 8; b++) {
            int i = s.pin[b];
            if (i < 0) continue;
            next_drive[i] = (ddr >> b) & 1;
            next_level[i] = (port >> b) & 1;
        }
    }
    if (!changed) return;
    // the direction or the pull-ups may have changed
    apply_inputs();
    for (size_t i = 0; i < drive.size(); i++)
        if (next_drive[i] != drive[i] || (next_drive[i] && next_level[i] != level[i])) pending = true;
}

void mcu::apply_inputs()
{
    for (auto& s : ports_) {
        bool pullups_off = s.io->pud.reg && avr_regbit_get(a, s.io->pud);
        for (int b = 0; b < 8; b++) {
            int i = s.pin[b];
            if (i < 0 || ((s.ddr >> b) & 1)) continue;
            int v = input[i];
            int want = v == 1 ? 1 : v == 3 ? (((s.port >> b) & 1) && !pullups_off) : 0;
            if (((a->data[s.io->r_pin] >> b) & 1) != want) avr_raise_irq(s.io->io.irq + b, (uint32_t)want);
        }
    }
}

uint64_t mcu::bit_time() const
{
    auto u = (avr_uart_t*)uart;
    uint32_t ubrr = avr_regbit_get(a, u->ubrrl) | (uint32_t(avr_regbit_get(a, u->ubrrh)) << 8);
    uint64_t cycles = uint64_t(ubrr + 1) * (avr_regbit_get(a, u->u2x) ? 8 : 16);
    return cycles * period;
}

bool mcu::tx_enabled() const { return uart && avr_regbit_get(a, ((avr_uart_t*)uart)->txen); }

bool mcu::rx_enabled() const { return uart && avr_regbit_get(a, ((avr_uart_t*)uart)->rxen); }

void mcu::uart_output(uint8_t byte) { tx.send(byte, time_of(a->cycle), bit_time()); }

void mcu::eval(logic_value* ports, logic_context& ctx)
{
    uint64_t now = ctx.now;
    for (size_t i = 0; i < p.pins.size(); i++)
        input[i] = logic_get_bit(&ports[i], 0);
    int reset_n = logic_get_bit(&ports[p.pins.size()], 0);

    if (!started) restart(now);
    if (reset_n == 0) {
        // held in reset, starts like after power on when let go
        if (!held) {
            reset();
            started = true;
            held = true;
        }
        write_ports(ports, now);
        ctx.wake = LOGIC_NEVER;
        return;
    }
    if (held) {
        held = false;
        restart(now);
    }

    uint64_t t = time_of(a->cycle);
    if (now >= t) {
        if (pending) {
            drive = next_drive;
            level = next_level;
            pending = false;
        }
        if (pause_reason) {
            ctx.pause = pause_reason;
            pause_reason = nullptr;
            paused_at = now;
        }
    }
    // the pins are inouts: putting out what changed at the pause evaluates
    // again at the same time, that mustn't run on
    bool paused_now = paused_at == now;

    if (uart && p.rxd >= 0) {
        uint8_t byte;
        if (rx.update(input[p.rxd], rx_enabled(), now, bit_time(), byte)) avr_raise_irq(uart_in, byte);
    }

    // an input wakes a sleeping avr at its time
    if (a->state == cpu_Sleeping && t < now) a->cycle = cycle_at(now);
    apply_inputs();
    if (!paused_now) run(now + run_ahead);

    t = time_of(a->cycle);
    uint64_t wake = LOGIC_NEVER;
    if (pending || pause_reason || a->state == cpu_Running) wake = t;
    else if (a->state == cpu_Sleeping && a->cycle_timers.timer) wake = std::max(t, time_of(a->cycle_timers.timer->when));
    // going on after the pause, not in the same point in time
    if (paused_now) wake = std::max(wake, now + 1);
    if (uart) wake = std::min({wake, tx.next_change(now), rx.next_sample()});
    if (wake != LOGIC_NEVER && wake <= now) wake = now + 1;
    ctx.wake = wake;

    write_ports(ports, now);
}

void mcu::write_ports(logic_value* ports, uint64_t now)
{
    for (size_t i = 0; i < p.pins.size(); i++) {
        if ((int)i == p.txd && tx_enabled()) logic_set_u64(&ports[i], (uint64_t)tx.level(now));
        else if (drive[i] && !held) logic_set_u64(&ports[i], level[i]);
        else logic_set_z(&ports[i]);
    }
}

void mcu::inspect(const logic_inspect_sink& sink)
{
    auto put = [&](const char* name, uint64_t v, uint32_t width) {
        logic_sdk::small_value sv;
        sv.bits = v;
        auto view = sv.view(width);
        sink.value(sink.data, name, &view);
    };
    uint8_t sreg = 0;
    for (int b = 0; b < 8; b++)
        if (a->sreg[b]) sreg |= uint8_t(1u << b);
    put("pc", a->pc, 16);
    put("sp", a->data[R_SPL] | (a->data[R_SPH] << 8), 16);
    put("sreg", sreg, 8);
    put("cycles", a->cycle, 64);
    put("sleeping", a->state == cpu_Sleeping, 1);
    // r0..r31 are the first 32 bytes of the data memory in the debugger
}

bool mcu::load_image(const char* param, const logic_image& image, const logic_host&)
{
    if (std::strcmp(param, "firmware") != 0) return false;
    // elf files from avr-gcc put the eeprom at 0x810000
    const uint64_t eeprom_at = 0x810000;
    std::fill(flash.begin(), flash.end(), 0xFF);
    std::fill(eeprom.begin(), eeprom.end(), 0xFF);
    for (uint32_t k = 0; k < image.segment_count; k++) {
        auto& s = image.segments[k];
        for (uint64_t i = 0; i < s.size; i++) {
            uint64_t at = s.address + i;
            if (at < flash.size()) flash[at] = s.bytes[i];
            else if (at >= eeprom_at && at - eeprom_at < eeprom.size()) eeprom[at - eeprom_at] = s.bytes[i];
        }
    }
    reset();
    return true;
}

std::vector<logic_memory_info> mcu::memories()
{
    return {{"flash", a->flashend + 1u}, {"data (registers, i/o, sram)", a->ramend + 1u}, {"eeprom", a->e2end + 1u}};
}

bool mcu::read_memory(uint32_t k, uint64_t address, uint8_t* out, uint64_t n)
{
    if (k == 0 && address + n <= a->flashend + 1u) std::memcpy(out, a->flash + address, n);
    else if (k == 1 && address + n <= a->ramend + 1u) std::memcpy(out, a->data + address, n);
    else if (k == 2 && address + n <= a->e2end + 1u) {
        avr_eeprom_desc_t d{nullptr, (uint16_t)address, (uint32_t)n};
        if (avr_ioctl(a, AVR_IOCTL_EEPROM_GET, &d) != 0 || !d.ee) return false;
        std::memcpy(out, d.ee, n);
    } else return false;
    return true;
}

bool mcu::write_memory(uint32_t k, uint64_t address, const uint8_t* data, uint64_t n)
{
    if (k == 0 && address + n <= a->flashend + 1u) std::memcpy(a->flash + address, data, n);
    else if (k == 1 && address + n <= a->ramend + 1u) std::memcpy(a->data + address, data, n);
    else if (k == 2 && address + n <= a->e2end + 1u && n > 0) {
        avr_eeprom_desc_t d{const_cast<uint8_t*>(data), (uint16_t)address, (uint32_t)n};
        return avr_ioctl(a, AVR_IOCTL_EEPROM_SET, &d) == 0;
    } else return false;
    return true;
}

bool mcu::pc(uint64_t& out)
{
    out = a->pc;
    return true;
}

uint32_t mcu::disassemble(uint64_t address, std::string& text)
{
    if (address + 1 > a->flashend) return 0;
    auto word = [&](uint64_t at) { return at + 1 <= a->flashend ? uint16_t(a->flash[at] | (a->flash[at + 1] << 8)) : uint16_t(0xFFFF); };
    return (uint32_t)avr::disassemble((uint32_t)address, word(address), word(address + 2), text);
}

void mcu::set_breakpoint(uint64_t address, bool on)
{
    if (address >= breakpoints.size()) return;
    if (breakpoints[address] == (on ? 1 : 0)) return;
    breakpoints[address] = on ? 1 : 0;
    breakpoint_count += on ? 1 : -1;
}

void mcu::step_instruction() { step = true; }

}
