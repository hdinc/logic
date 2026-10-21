#include "trace.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace logic::wave {

namespace {

std::atomic<uint64_t> versions{0};

const uint64_t pow10[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
    10000000000, 100000000000, 1000000000000, 10000000000000, 100000000000000, 1000000000000000};

constexpr double nan = std::numeric_limits<double>::quiet_NaN();

}

std::string format_time(time t, timebase tb)
{
    if (t == 0) return "0 s";

    struct unit {
        int exponent;
        const char* name;
    };
    static const unit units[] = {{0, "s"}, {-3, "ms"}, {-6, "us"}, {-9, "ns"}, {-12, "ps"}, {-15, "fs"}};

    for (auto& u : units) {
        if (u.exponent < tb.exponent) break;
        int digits = u.exponent - tb.exponent;
        time size = pow10[digits];
        if (t < size && u.exponent != tb.exponent) continue;
        auto s = std::to_string(t / size);
        if (digits > 0) {
            // the remainder with leading zeros
            auto frac = std::to_string(t % size);
            frac.insert(0, digits - frac.size(), '0');
            while (!frac.empty() && frac.back() == '0')
                frac.pop_back();
            if (!frac.empty()) s += "." + frac;
        }
        return s + " " + u.name;
    }
    return std::to_string(t);
}

double seconds(double ticks, timebase tb)
{
    return ticks * std::pow(10.0, tb.exponent);
}

std::string format_frequency(double ticks, timebase tb)
{
    double s = seconds(std::abs(ticks), tb);
    if (!(s > 0)) return {};
    double f = 1.0 / s;
    static const char* names[] = {"Hz", "kHz", "MHz", "GHz", "THz"};
    int k = 0;
    while (f >= 1000.0 && k < 4) {
        f /= 1000.0;
        k++;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4g %s", f, names[k]);
    return buf;
}

trace::trace(kind k, int width)
    : m_kind(k)
    , m_width(std::max(1, width))
    , m_words(sim::value::word_count(std::max(1, width)))
{
    touch();
}

void trace::touch()
{
    m_version = ++versions;
}

sim::value trace::value(size_t i) const
{
    if (m_kind != kind::digital) return sim::unknown(m_width);
    sim::value v(m_width, 0);
    std::memcpy(v.data(), words_of(i), 3 * m_words * sizeof(uint64_t));
    return v;
}

sim::bit_state trace::bit(size_t i, int b) const
{
    if (m_kind != kind::digital || b < 0 || b >= m_width) return sim::bit_state::x;
    auto w = words_of(i);
    uint64_t m = uint64_t(1) << (b % 64);
    int k = b / 64;
    if (w[2 * m_words + k] & m) return sim::bit_state::z;
    if (w[m_words + k] & m) return sim::bit_state::x;
    return w[k] & m ? sim::bit_state::one : sim::bit_state::zero;
}

bool trace::fully_known(size_t i) const
{
    switch (m_kind) {
    case kind::digital: {
        auto w = words_of(i);
        for (int k = m_words; k < 3 * m_words; k++)
            if (w[k]) return false;
        return true;
    }
    case kind::real:
        return !std::isnan(reals[head + i]);
    case kind::text:
        return true;
    }
    return true;
}

bool trace::fully_floating(size_t i) const
{
    if (m_kind != kind::digital) return false;
    auto w = words_of(i);
    for (int k = 0; k < m_words; k++)
        if (w[2 * m_words + k] != sim::word_mask(m_width, k)) return false;
    return true;
}

double trace::number(size_t i, bool is_signed) const
{
    switch (m_kind) {
    case kind::real:
        return reals[head + i];
    case kind::text:
        return nan;
    case kind::digital:
        break;
    }
    if (!fully_known(i)) return nan;
    auto w = words_of(i);
    if (m_width <= 64) {
        uint64_t u = w[0];
        if (!is_signed) return double(u);
        if (m_width < 64 && (u >> (m_width - 1)) & 1) u |= ~sim::mask(m_width);
        return double(int64_t(u));
    }
    double d = 0;
    for (int k = m_words - 1; k >= 0; k--)
        d = d * 18446744073709551616.0 + double(w[k]);
    if (is_signed && bit(i, m_width - 1) == sim::bit_state::one) d -= std::ldexp(1.0, m_width);
    return d;
}

bool trace::same(size_t i, size_t j) const
{
    switch (m_kind) {
    case kind::digital:
        return std::memcmp(words_of(i), words_of(j), 3 * m_words * sizeof(uint64_t)) == 0;
    case kind::real: {
        double a = reals[head + i], b = reals[head + j];
        return a == b || (std::isnan(a) && std::isnan(b));
    }
    case kind::text:
        return texts[head + i] == texts[head + j];
    }
    return false;
}

size_t trace::index_at(time t) const
{
    auto it = std::upper_bound(times.begin() + head, times.end(), t);
    if (it == times.begin() + head) return npos;
    return size_t(it - times.begin()) - head - 1;
}

size_t trace::first_after(time t) const
{
    auto it = std::upper_bound(times.begin() + head, times.end(), t);
    if (it == times.end()) return npos;
    return size_t(it - times.begin()) - head;
}

size_t trace::lower_bound(time t) const
{
    return size_t(std::lower_bound(times.begin() + head, times.end(), t) - times.begin()) - head;
}

bool trace::push(time t, const sim::value& v)
{
    if (m_kind != kind::digital) return false;
    const sim::value& w = v.width() == m_width ? v : v.slice(0, m_width);
    auto d = w.data();
    size_t n = 3 * m_words;
    size_t count = size();
    if (count > 0 && std::memcmp(words_of(count - 1), d, n * sizeof(uint64_t)) == 0) return false;
    touch();
    if (count > 0 && at(count - 1) == t) {
        // several changes at one time: the last one counts
        if (count > 1 && std::memcmp(words_of(count - 2), d, n * sizeof(uint64_t)) == 0) {
            pop_back();
            return true;
        }
        std::memcpy(&words[(head + count - 1) * n], d, n * sizeof(uint64_t));
        return true;
    }
    times.push_back(t);
    words.insert(words.end(), d, d + n);
    return true;
}

bool trace::push(time t, double v)
{
    if (m_kind != kind::real) return false;
    size_t count = size();
    auto same_value = [](double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); };
    if (count > 0 && same_value(reals.back(), v)) return false;
    touch();
    if (count > 0 && at(count - 1) == t) {
        if (count > 1 && same_value(reals[reals.size() - 2], v)) {
            pop_back();
            return true;
        }
        reals.back() = v;
        return true;
    }
    times.push_back(t);
    reals.push_back(v);
    return true;
}

bool trace::push(time t, std::string v)
{
    if (m_kind != kind::text) return false;
    size_t count = size();
    if (count > 0 && texts.back() == v) return false;
    touch();
    if (count > 0 && at(count - 1) == t) {
        if (count > 1 && texts[texts.size() - 2] == v) {
            pop_back();
            return true;
        }
        texts.back() = std::move(v);
        return true;
    }
    times.push_back(t);
    texts.push_back(std::move(v));
    return true;
}

bool trace::push_from(const trace& other, size_t i)
{
    switch (m_kind) {
    case kind::digital:
        return push(other.at(i), other.value(i));
    case kind::real:
        return push(other.at(i), other.real(i));
    case kind::text:
        return push(other.at(i), other.text(i));
    }
    return false;
}

void trace::pop_back()
{
    if (empty()) return;
    touch();
    times.pop_back();
    switch (m_kind) {
    case kind::digital:
        words.resize(words.size() - 3 * m_words);
        break;
    case kind::real:
        reals.pop_back();
        break;
    case kind::text:
        texts.pop_back();
        break;
    }
}

void trace::drop_front(size_t n)
{
    n = std::min(n, size());
    if (n == 0) return;
    touch();
    head += n;
    if (head >= 1024 && head * 2 >= times.size()) compact();
}

void trace::compact()
{
    times.erase(times.begin(), times.begin() + head);
    switch (m_kind) {
    case kind::digital:
        words.erase(words.begin(), words.begin() + head * 3 * m_words);
        break;
    case kind::real:
        reals.erase(reals.begin(), reals.begin() + head);
        break;
    case kind::text:
        texts.erase(texts.begin(), texts.begin() + head);
        break;
    }
    head = 0;
}

void trace::clear()
{
    touch();
    head = 0;
    times.clear();
    words.clear();
    reals.clear();
    texts.clear();
}

double trace::block_number(size_t physical, bool is_signed) const
{
    return number(physical - head, is_signed);
}

std::pair<double, double> trace::range(size_t a, size_t b, bool is_signed) const
{
    double lo = nan, hi = nan;
    auto add = [&](double v) {
        if (std::isnan(v)) return;
        if (std::isnan(lo) || v < lo) lo = v;
        if (std::isnan(hi) || v > hi) hi = v;
    };
    b = std::min(b, size());
    if (a >= b) return {lo, hi};

    // physical indices, blocks start at multiples of block
    size_t pa = head + a, pb = head + b;
    if (pb - pa <= 2 * block) {
        for (size_t p = pa; p < pb; p++)
            add(block_number(p, is_signed));
        return {lo, hi};
    }

    if (blocks_version != m_version || blocks_signed != is_signed) {
        blocks.assign(times.size() / block, {nan, nan});
        for (size_t k = 0; k < blocks.size(); k++) {
            double l = nan, h = nan;
            for (size_t p = std::max(k * block, head); p < (k + 1) * block; p++) {
                double v = block_number(p, is_signed);
                if (std::isnan(v)) continue;
                if (std::isnan(l) || v < l) l = v;
                if (std::isnan(h) || v > h) h = v;
            }
            blocks[k] = {l, h};
        }
        blocks_version = m_version;
        blocks_signed = is_signed;
    }

    size_t first_block = (pa + block - 1) / block, last_block = pb / block;
    for (size_t p = pa; p < first_block * block; p++)
        add(block_number(p, is_signed));
    for (size_t k = first_block; k < last_block && k < blocks.size(); k++) {
        add(blocks[k].first);
        add(blocks[k].second);
    }
    for (size_t p = std::max(last_block * block, pa); p < pb; p++)
        add(block_number(p, is_signed));
    return {lo, hi};
}

std::string_view signal::leaf() const
{
    auto k = name.find_last_of("/.");
    return k == std::string::npos ? std::string_view(name) : std::string_view(name).substr(k + 1);
}

std::string_view signal::scope() const
{
    auto k = name.find_last_of("/.");
    return k == std::string::npos ? std::string_view() : std::string_view(name).substr(0, k);
}

const signal* trace_set::find(int64_t key) const
{
    for (auto& s : signals)
        if (s.key == key) return &s;
    return nullptr;
}

const signal* trace_set::find_name(std::string_view name) const
{
    for (auto& s : signals)
        if (s.name == name) return &s;
    return nullptr;
}

std::string display_name(std::string_view name, bool full_path)
{
    if (!full_path) {
        auto last = name.find_last_of("/.");
        if (last != std::string_view::npos) name.remove_prefix(last + 1);
    }
    return std::string(name);
}

}
