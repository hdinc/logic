#pragma once

#include "simulator.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace logic::sim {

// runs a simulator on its own thread. others send commands and read
// snapshots of the values, they never touch the simulator's values directly.
// the structure (scopes, nets, problems) doesn't change and can be read any
// time through sim().
class runner {
public:
    explicit runner(std::unique_ptr<simulator> s);
    ~runner();

    runner(const runner&) = delete;
    runner& operator=(const runner&) = delete;

    const simulator& sim() const { return *s; }

    // commands, applied by the simulation thread in the order they are sent
    void set_input(int port, value v);
    // advances to the next event
    void step();
    // back to time 0, see simulator::reset
    void reset();
    // runs f on the simulation thread, e.g. to change a component
    void post(std::function<void(simulator&)> f);
    // the same for reading only: real time running goes on undisturbed
    void query(std::function<void(simulator&)> f);

    // keeps running until paused or the circuit oscillates
    void run(bool on);
    bool running() const { return m_running; }

    // simulated seconds per real second, 0 runs as fast as possible
    void set_speed(double s);
    double speed() const;

    // takes the latest values published by the simulation thread. call it
    // once per frame, view() doesn't change in between.
    void sync();
    const snapshot& view() const { return front; }
    // simulated seconds per real second measured while running, at the last
    // sync
    double actual_speed() const { return front_speed; }

    // blocks until the commands sent so far are applied and published, then
    // syncs
    void wait_idle();

private:
    void loop();
    void push(std::function<void(simulator&)> f, bool restarts = true);
    // copies the values for sync(), called by the simulation thread
    void publish(double measured);

    std::unique_ptr<simulator> s;

    mutable std::mutex m;
    std::condition_variable cv;
    std::condition_variable idle_cv;

    // shared, guarded by m
    struct command {
        std::function<void(simulator&)> f;
        // changes the simulation, real time starts over from it
        bool restarts;
    };
    std::vector<command> commands;
    uint64_t sent = 0;
    uint64_t done = 0;
    double m_speed = 1.0;
    // speed or running changed, real time starts over from now
    bool restart_clock = true;
    bool quit = false;
    state published;
    uint64_t version = 0;
    double published_speed = 0.0;

    std::atomic<bool> m_running{false};

    // owned by the reading side
    snapshot front;
    uint64_t front_version = 0;
    double front_speed = 0.0;

    std::thread worker;
};

}
