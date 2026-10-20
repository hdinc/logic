// a uart's bytes as a serial line: 1 start bit (0), 8 data bits from bit 0,
// 1 stop bit (1), 1 while idle. simavr's uart only passes whole bytes, the
// mcu turns them into levels on the txd pin and back from the rxd pin.

#pragma once

#include "logic_model.h"

#include <cstdint>
#include <deque>

namespace avr {

class serial_out {
public:
    // a byte written at time t, each bit bit_time long. it starts after the
    // ones before it.
    void send(uint8_t byte, uint64_t t, uint64_t bit_time)
    {
        uint64_t start = frames.empty() ? t : frames.back().start + 10 * frames.back().bit_time;
        if (start < t) start = t;
        frames.push_back({byte, start, bit_time});
    }

    // the level at time t, forgets frames that ended
    int level(uint64_t t)
    {
        drop(t);
        if (frames.empty() || t < frames.front().start) return 1;
        auto& f = frames.front();
        uint64_t bit = (t - f.start) / f.bit_time;
        if (bit == 0) return 0;
        if (bit <= 8) return (f.byte >> (bit - 1)) & 1;
        return 1;
    }

    // when the level may change after t
    uint64_t next_change(uint64_t t)
    {
        drop(t);
        if (frames.empty()) return LOGIC_NEVER;
        auto& f = frames.front();
        if (t < f.start) return f.start;
        return f.start + ((t - f.start) / f.bit_time + 1) * f.bit_time;
    }

    void clear() { frames.clear(); }

private:
    struct frame {
        uint8_t byte;
        uint64_t start, bit_time;
    };

    void drop(uint64_t t)
    {
        while (!frames.empty() && t >= frames.front().start + 10 * frames.front().bit_time)
            frames.pop_front();
    }

    std::deque<frame> frames;
};

class serial_in {
public:
    // the line's level at time t (0 or 1, anything else counts as 1). a
    // falling edge while idle and enabled starts a frame. returns true with
    // the byte when a frame with a valid stop bit ends.
    bool update(int level, bool enabled, uint64_t t, uint64_t bit_time, uint8_t& out)
    {
        bool line = level != 0;
        bool done = false;
        if (sample != LOGIC_NEVER && t >= sample) {
            if (bit < 8) {
                if (line) byte |= uint8_t(1u << bit);
                bit++;
                sample += this->bit_time;
            } else {
                // the stop bit
                if (line) {
                    out = byte;
                    done = true;
                }
                sample = LOGIC_NEVER;
            }
        }
        if (sample == LOGIC_NEVER && enabled && prev && !line && !done) {
            // the middle of the first data bit
            this->bit_time = bit_time;
            sample = t + bit_time + bit_time / 2;
            bit = 0;
            byte = 0;
        }
        prev = line;
        return done;
    }

    // when the line has to be looked at next
    uint64_t next_sample() const { return sample; }

    void clear()
    {
        sample = LOGIC_NEVER;
        prev = true;
    }

private:
    uint64_t sample = LOGIC_NEVER, bit_time = 1;
    uint8_t byte = 0;
    int bit = 0;
    bool prev = true;
};

}
