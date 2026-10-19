#pragma once

#include "../model/workspace.h"
#include "../plugin/registry.h"
#include "../sim/component.h"
#include "verilator.h"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

namespace logic::hdl {

// how building the model of a cell went
struct build_status {
    enum state_t {
        none,
        queued,
        building,
        done,
        failed
    } state = none;
    // output of the tools
    std::vector<std::string> log;
    std::vector<diagnostic> diagnostics;
    std::string error;
};

// hdl cells: finds their interfaces, builds their models in the background
// and creates their simulation models. verilator only, for now. builds are
// cached in <cache root>/verilator, one per set of parameters.
class manager : public model::external_provider, public sim::component_factory {
public:
    // plugins loads the built models
    manager(const model::workspace& ws, const plugin::registry& plugins, std::filesystem::path cache_root);
    // waits for the build that is running
    ~manager() override;

    // found on first use
    const verilator_tools& verilator() const;

    // model::external_provider for hdl cells simulated by verilator
    bool handles(const model::cell& c) const override;
    bool describe(const model::cell& c, const std::filesystem::path& dir, std::vector<model::port>& ports, std::vector<model::param_def>& params,
        std::string& error) const override;
    bool ports_for(const model::cell& c, const std::filesystem::path& dir, const model::param_values& values, std::vector<model::port>& ports,
        std::string& error) const override;

    // sim::component_factory. builds the model right away if it isn't
    // built, which may take long: use missing_builds and build first.
    std::unique_ptr<sim::component> create(const model::cell& c, const model::param_values& params, std::string& error) const override;

    // the cell's model for the resolved parameter values is in the cache
    bool is_built(const model::cell& c, const model::param_values& values) const;
    // builds it in the background unless it's built, the cell is copied
    void build(const model::cell& c, const model::param_values& values);

    struct request {
        model::id cell;
        model::param_values values;
    };
    // the models a simulation of top needs that aren't built
    std::vector<request> missing_builds(const model::cell& top) const;

    // of the build last requested for the cell
    build_status status(model::id cell) const;
    // something is queued or building
    bool busy() const;

    // a line per finished or failed build since the last call
    std::vector<std::string> take_messages() { return messages.take(); }

private:
    struct job {
        model::id cell;
        verilator_job v;
    };

    verilator_job job_of(const model::cell& c, const model::param_values& values) const;
    std::filesystem::path dir_of(const model::cell& c) const;
    // runs the job on the calling thread, updating the cell's status
    bool run(const job& j, std::filesystem::path& plugin) const;
    void loop();

    const model::workspace& ws;
    const plugin::registry& plugins;
    std::filesystem::path cache_root;

    mutable std::once_flag tools_once;
    mutable verilator_tools tools;

    mutable plugin::message_log messages;

    mutable std::mutex m;
    std::condition_variable cv;
    std::deque<job> queue;
    bool working = false;
    bool quit = false;
    mutable std::map<model::id, build_status> statuses;
    std::thread worker;
};

}
