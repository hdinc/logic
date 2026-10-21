#include "timeline.h"

#include <algorithm>
#include <cmath>

namespace logic::wave {

namespace {

constexpr double max_span = double(never / 4);
constexpr double max_time = double(never / 2);

}

void timeline::zoom(double wheel, double fraction)
{
    anchor_fraction = std::clamp(fraction, 0.0, 1.0);
    anchor = m_start + m_span * anchor_fraction;
    target_span = std::clamp(target_span * std::pow(1.25, -wheel), 1.0, max_span);
}

void timeline::pan(double fraction)
{
    set_start(m_start - fraction * m_span);
}

void timeline::set_start(double t)
{
    m_start = std::clamp(t, 0.0, max_time - m_span);
    target_span = m_span;
    follow = false;
}

void timeline::fit(time first, time last)
{
    if (last > first) m_span = std::clamp(double(last - first) * 1.05, 1.0, max_span);
    m_start = std::clamp(double(first), 0.0, max_time - m_span);
    target_span = m_span;
}

void timeline::zoom_to(double a, double b)
{
    if (b < a) std::swap(a, b);
    m_span = target_span = std::clamp(b - a, 1.0, max_span);
    m_start = std::clamp(a, 0.0, max_time - m_span);
    follow = false;
}

void timeline::reveal(double t)
{
    if (t >= m_start && t <= m_start + m_span) return;
    set_start(t - m_span / 2);
}

void timeline::update(double dt, time now, bool running)
{
    if (m_span != target_span) {
        double k = std::log(target_span / m_span);
        double t = 1.0 - std::exp(-std::max(0.0, dt) * 20.0);
        m_span = std::abs(k) < 0.001 ? target_span : m_span * std::exp(k * t);
        if (!follow || !running) m_start = anchor - m_span * anchor_fraction;
    }
    bool advanced = last_now != never && now != last_now;
    last_now = now;
    if (follow && (running || advanced)) {
        m_start = double(now) - m_span * 0.95;
        anchor = std::max(0.0, m_start) + m_span * anchor_fraction;
    }
    m_start = std::clamp(m_start, 0.0, max_time - m_span);
}

}
