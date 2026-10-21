#pragma once

#include "../wave/session.h"
#include "../wave/ui/view.h"
#include "../wave/vcd.h"

#include <Hd/Gui.h>
#include <Hd/Window.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace logic::viewer {

// a window showing the waveforms of a vcd file
class viewer_app {
public:
    explicit viewer_app(Hd::Window& window);
    ~viewer_app();

    // a vcd file, or a session with the file it names
    void open(const std::filesystem::path& file);
    void run();

private:
    // a file loaded on a thread
    struct load_job {
        std::filesystem::path file;
        // applied when it's loaded: a session, or the rows of a reload
        std::optional<wave::session> session;
        wave::load_progress progress;
        std::atomic<bool> done{false};
        std::optional<wave::trace_set> result;
        std::string error;
        std::thread thread;
    };

    void load(const std::filesystem::path& vcd, std::optional<wave::session> session);
    void finish_loading();
    void reload();
    void save_session(const std::filesystem::path& file);

    void draw_gui();
    void menu_bar();
    void signals_panel();
    void signal_item(const wave::signal& s);
    void loading_popup();
    void dialogs();
    void shortcuts();

    void add_settings_handler();
    void apply_theme();
    void set_title();
    void error(std::string message);

    Hd::Window& window;
    Hd::Gui gui;

    wave::trace_set data;
    std::filesystem::path file;
    // the last session file, saving goes there
    std::filesystem::path session_file;
    wave::view view;
    std::unique_ptr<load_job> job;

    // signals panel
    std::string filter;
    // scopes of the signals, in order of appearance
    struct scope_node {
        std::string name;
        std::vector<scope_node> children;
        std::vector<int> signals;
    };
    scope_node tree;
    void build_tree();
    void draw_scope(const scope_node& node);
    void add_all(const scope_node& node);

    std::string error_message;
    std::vector<std::string> notes;
    bool light_theme = false;
    bool should_exit = false;
    bool show_help = false;
};

}
