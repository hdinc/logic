#pragma once

#include "../model/workspace.h"
#include "../sim/component.h"
#include "dynlib.h"

#include <logic_model.h>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace logic::plugin {

// messages from models for the user, written from any thread
class message_log {
public:
    void add(std::string m);
    std::vector<std::string> take();

private:
    std::mutex m;
    std::vector<std::string> messages;
};

// a plugin file loaded from a private copy, so the original can be rebuilt
// while it's in use. components created from it keep it loaded.
struct loaded_plugin {
    std::filesystem::path path;
    std::filesystem::file_time_type mtime;
    std::filesystem::path copy;
    std::unique_ptr<dynlib> lib;
    const logic_plugin* info = nullptr;

    loaded_plugin() = default;
    loaded_plugin(const loaded_plugin&) = delete;
    ~loaded_plugin();

    // null if the plugin has no model with that name
    const logic_model_desc* find(const std::string& model) const;
};

// finds and loads c model plugins. it is the provider of the interfaces of
// c model cells and the factory of their simulation models. a plugin whose
// file changed is loaded again the next time it's used.
class registry : public model::external_provider, public sim::component_factory {
public:
    // the workspace gives the library folders of the cells
    explicit registry(const model::workspace& ws);

    // folders searched for plugins by name, in order
    std::vector<std::filesystem::path> search_dirs;
    // <workspace>/plugins, the plugins folder next to the program and the
    // folders in LOGIC_PLUGIN_PATH
    void add_default_dirs(const std::filesystem::path& workspace_root);

    // the file of a plugin: a path (with a '/' or an extension) is relative
    // to the library folder, a name is searched in search_dirs as
    // <name><ext> or lib<name><ext>. empty if not found.
    std::filesystem::path resolve(const std::string& plugin, const std::filesystem::path& library_dir) const;

    // loads the plugin or returns the loaded one if the file didn't change.
    // null with an error.
    std::shared_ptr<loaded_plugin> load(const std::string& plugin, const std::filesystem::path& library_dir, std::string& error) const;

    struct available {
        std::string name;
        std::filesystem::path path;
    };
    // the plugin files in search_dirs
    std::vector<available> scan() const;

    // parameter definitions of a model as the program uses them
    static std::vector<model::param_def> params_of(const logic_model_desc& d);

    // model::external_provider, handles c model cells
    bool handles(const model::cell& c) const override;
    bool describe(const model::cell& c, const std::filesystem::path& dir, std::vector<model::port>& ports, std::vector<model::param_def>& params,
        std::string& error) const override;
    bool ports_for(const model::cell& c, const std::filesystem::path& dir, const model::param_values& values, std::vector<model::port>& ports,
        std::string& error) const override;

    // sim::component_factory, creates c models
    std::unique_ptr<sim::component> create(const sim::create_info& info, std::string& error) const override;

    // a simulation model of the plugin for an instance of the cell. its
    // ports must be the cell's for the default parameters. dir is the
    // folder relative file parameters start at.
    std::unique_ptr<sim::component> create_from(const std::shared_ptr<loaded_plugin>& p, const logic_model_desc& d, const model::cell& c,
        const model::param_values& params, const std::filesystem::path& dir, std::string& error) const;

    // messages logged by models since the last call
    std::vector<std::string> take_messages() { return log->take(); }

private:
    // the plugin and model of a c model cell
    const logic_model_desc* model_of(const model::cell& c, const std::filesystem::path& dir, std::shared_ptr<loaded_plugin>& p, std::string& error) const;
    std::filesystem::path dir_of(const model::cell& c) const;

    const model::workspace& ws;
    std::shared_ptr<message_log> log;

    mutable std::mutex m;
    mutable std::map<std::filesystem::path, std::shared_ptr<loaded_plugin>> loaded;
    mutable uint64_t copies = 0;
};

}
