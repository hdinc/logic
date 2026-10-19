#pragma once

#include "../hdl/process.h"
#include "../model/cell.h"
#include "../plugin/registry.h"
#include "../sim/component.h"
#include "socket.h"

#include <memory>
#include <thread>

namespace logic::cosim {

// how to start the simulator of one instance. the bridge's environment is
// added.
struct launch {
    std::vector<std::string> args;
    hdl::environment env;
    std::filesystem::path dir;
    // module the bridge finds the ports in, see logic_vpi.c
    std::string testbench;
};

// an instance simulated by an external simulator process through the vpi
// bridge. the process only runs when asked: at each point in time the
// inputs are sent and the outputs read back, and once that time settled
// the simulator runs until the next event elsewhere or until it changes an
// output, whichever comes first.
//
// limits: inputs set while the external simulator is ahead (e.g. by the
// user between steps) take effect at its time. two external simulators
// with their own timing that feed each other may see changes late, up to
// the next event of the rest of the circuit.
class component : public sim::component {
public:
    // null with an error if the simulator doesn't start. the log gets its
    // output and problems.
    static std::unique_ptr<component> start(std::string name, std::vector<model::port> ports, launch how,
        std::shared_ptr<plugin::message_log> log, std::string& error);
    ~component() override;

    void eval(std::span<sim::value> ports, sim::context& ctx) override;
    // starts the simulator again
    void reset() override;

    // how long to wait for an answer
    static constexpr int timeout_ms = 60000;
    // how far to run when nothing else is scheduled
    static constexpr sim::time idle_step = sim::ms;

private:
    component(std::string name, std::vector<model::port> ports, launch how, std::shared_ptr<plugin::message_log> log);

    bool connect(std::string& error);
    void shutdown();

    // a message: type and payload
    bool send(uint8_t type, const std::vector<uint8_t>& payload);
    bool receive(uint8_t& type, std::vector<uint8_t>& payload);
    // the answer to SET or RUN
    bool read_state();
    // stops using the simulator after a problem
    void fail(const std::string& why);

    std::string name;
    std::vector<model::port> ports;
    launch how;
    std::shared_ptr<plugin::message_log> log;

    std::unique_ptr<hdl::child_process> process;
    std::thread output_reader;
    std::unique_ptr<connection> conn;
    bool alive = false;

    // what the design drives on the outputs and inouts, by port
    std::vector<sim::value> driven;
    // the inputs and inouts sent last, to skip unchanged ones
    std::vector<sim::value> sent;
    bool sent_any = false;

    // the state from the last answer: where the simulator stopped
    sim::time state_time = 0;
    std::vector<sim::value> state_values;
    bool state_finished = false;
    // the simulator stopped ahead of the simulation, state_values are shown
    // when that time comes
    bool pending = false;
};

}
