#pragma once

#include "../trace.h"

namespace logic::wave {

// the visible time range, in ticks. navigation is independent of drawing,
// so zoom animation also works while the simulator is paused. follow keeps
// up with running and stepping, only manual panning turns it off.
class timeline {
public:
    // wheel steps about the point at fraction of the width
    void zoom(double wheel, double fraction);
    // by a fraction of the span, positive moves to earlier times
    void pan(double fraction);
    // the start moved to t
    void set_start(double t);
    // shows [first, last] with a little room
    void fit(time first, time last);
    // shows exactly [a, b]
    void zoom_to(double a, double b);
    // moves the range so that t is visible, if it isn't
    void reveal(double t);
    // animates zooming and follows now
    void update(double dt, time now, bool running);

    double start() const { return m_start; }
    double span() const { return m_span; }
    double end() const { return m_start + m_span; }
    bool follow = true;

private:
    double m_start = 0;
    double m_span = 1e12;
    double target_span = m_span;
    double anchor = 0;
    double anchor_fraction = 0;
    // time at the last update, never before the first one
    time last_now = never;
};

}
