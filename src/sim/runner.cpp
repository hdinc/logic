#include "runner.h"

#include <algorithm>
#include <chrono>

namespace logic::sim {

namespace {

using clock = std::chrono::steady_clock;

// how long the simulation thread works before looking for commands again
constexpr auto work_slice = std::chrono::milliseconds(8);
// at most this often values are published while running
constexpr auto publish_interval = std::chrono::milliseconds(15);
// sleep when there is nothing to do, also the longest wait between events
constexpr auto idle_wait = std::chrono::milliseconds(16);
// falling further behind real time than this gives up catching up
constexpr double max_lag = 0.25;

double to_seconds(clock::duration d)
{
    return std::chrono::duration<double>(d).count();
}

}

runner::runner(std::unique_ptr<simulator> sim)
    : s(std::move(sim))
    , published(s->current())
    , front(*s)
{
    worker = std::thread([this] { loop(); });
}

runner::~runner()
{
    {
        std::lock_guard lk(m);
        quit = true;
    }
    cv.notify_all();
    worker.join();
}

void runner::push(std::function<void(simulator&)> f)
{
    {
        std::lock_guard lk(m);
        commands.push_back(std::move(f));
        sent++;
    }
    cv.notify_all();
}

void runner::set_input(int port, value v)
{
    push([port, v = std::move(v)](simulator& sim) { sim.set_input(port, v); });
}

void runner::step()
{
    push([](simulator& sim) { sim.step(); });
}

void runner::reset()
{
    push([](simulator& sim) { sim.reset(); });
}

void runner::post(std::function<void(simulator&)> f)
{
    push(std::move(f));
}

void runner::run(bool on)
{
    {
        std::lock_guard lk(m);
        m_running = on;
        restart_clock = true;
    }
    cv.notify_all();
}

void runner::set_speed(double sp)
{
    {
        std::lock_guard lk(m);
        if (m_speed == sp) return;
        m_speed = std::max(sp, 0.0);
        restart_clock = true;
    }
    cv.notify_all();
}

double runner::speed() const
{
    std::lock_guard lk(m);
    return m_speed;
}

void runner::sync()
{
    std::lock_guard lk(m);
    if (version == front_version) return;
    front.take(published);
    front_version = version;
    front_speed = published_speed;
}

void runner::wait_idle()
{
    {
        std::unique_lock lk(m);
        auto target = sent;
        idle_cv.wait(lk, [&] { return done >= target; });
    }
    sync();
}

void runner::publish(double measured)
{
    std::lock_guard lk(m);
    published = s->current();
    published_speed = measured;
    version++;
}

void runner::loop()
{
    // real time: simulated time base_sim was at base_wall, it advances with
    // the speed from there
    clock::time_point base_wall;
    time base_sim = 0;

    // measuring the actual speed over about half a second
    clock::time_point measure_wall = clock::now();
    time measure_sim = 0;
    double measured = 0.0;

    auto last_publish = clock::now();

    std::unique_lock lk(m);
    while (true) {
        // restart_clock also wakes on pause, to publish the final values
        cv.wait(lk, [&] { return quit || !commands.empty() || m_running || restart_clock; });
        if (quit) break;

        auto cmds = std::move(commands);
        commands.clear();
        auto seq = sent;
        double speed = m_speed;
        bool run = m_running;
        bool restart = restart_clock || !cmds.empty();
        restart_clock = false;
        lk.unlock();

        for (auto& c : cmds)
            c(*s);

        auto now_wall = clock::now();
        if (restart) {
            base_wall = now_wall;
            base_sim = s->now();
            measure_wall = now_wall;
            measure_sim = s->now();
        }

        // how long to sleep afterwards if nothing comes in
        clock::duration wait = clock::duration::zero();

        if (run) {
            auto deadline = now_wall + work_slice;
            if (speed <= 0.0) {
                // as fast as possible, event by event
                if (s->next_event() == never) wait = idle_wait;
                for (int n = 0; s->next_event() != never && !s->oscillating(); n++) {
                    s->step();
                    if (n % 64 == 63 && clock::now() > deadline) break;
                }
            } else {
                auto target = base_sim + time(to_seconds(now_wall - base_wall) * speed * double(seconds));
                bool reached = false;
                while (!s->oscillating()) {
                    reached = s->run_until(std::max(target, s->now()), 64);
                    if (reached || clock::now() > deadline) break;
                }
                // too slow for real time: don't try to catch up later
                if (!reached && double(target - s->now()) > max_lag * speed * double(seconds)) {
                    base_wall = clock::now();
                    base_sim = s->now();
                }
                // until the next event is due in real time
                auto next = s->next_event();
                if (reached) {
                    wait = idle_wait;
                    if (next != never) {
                        double secs = double(next - s->now()) / (speed * double(seconds));
                        wait = std::min<clock::duration>(idle_wait, std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(secs)));
                    }
                }
            }

            auto t = clock::now();
            if (t - measure_wall > std::chrono::milliseconds(500)) {
                measured = double(s->now() - measure_sim) / double(seconds) / to_seconds(t - measure_wall);
                measure_wall = t;
                measure_sim = s->now();
            }
        }

        bool stop = run && s->oscillating();
        auto t = clock::now();
        if (!cmds.empty() || stop || !run || t - last_publish >= publish_interval) {
            publish(run ? measured : 0.0);
            last_publish = t;
        }

        lk.lock();
        if (stop) m_running = false;
        if (!cmds.empty()) {
            done = seq;
            idle_cv.notify_all();
        }
        if (wait > clock::duration::zero() && m_running)
            cv.wait_for(lk, wait, [&] { return quit || !commands.empty() || !m_running || restart_clock; });
    }
}

}
