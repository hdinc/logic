// the entries of a folder as a table, the part a file manager and a file
// chooser share

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace logic::ui {

// lists dir in a table of name, size and date (where there's room), folders
// first. a click on a header sorts, names in natural order (file2 before
// file10). the folder is read again when dir changes and once a second, so
// changes made elsewhere show up.
class file_list {
public:
    // the folder shown
    std::filesystem::path dir;
    // the highest folder it goes up to, none if empty
    std::filesystem::path root;
    // the selected entry, empty for none
    std::filesystem::path selected;
    // which files are shown, all if not set. folders always are.
    std::function<bool(const std::filesystem::path&)> show;

    // the folders from root to dir as buttons, a click goes there
    void path_bar();
    // the table, as high as height (0: the rest of the window). a double
    // click goes into a folder, or on a file returns true with that file.
    bool table(std::filesystem::path& activated, float height = 0.0f);

    // reads the folder at the next frame, e.g. after changing it
    void refresh() { stale = true; }
    // dir, and selected if it's in there, scrolled to
    void go(const std::filesystem::path& folder, const std::filesystem::path& select = {});

private:
    struct entry {
        std::filesystem::path path;
        std::string name;
        bool folder = false;
        uintmax_t size = 0;
        std::string date;
    };
    void read();
    void sort();

    std::vector<entry> entries;
    std::filesystem::path listed;
    std::string error;
    double read_at = -1.0;
    bool stale = true;
    // the table's sort, column and direction
    int sort_column = 0;
    bool descending = false;
    bool scroll_to_selected = false;
};

// a file size as "12 KB"
std::string format_size(uintmax_t bytes);

}
