#pragma once

#include "../ui/file_list.h"

#include <filesystem>
#include <functional>
#include <string>

namespace logic::app {

// the files of the workspace in the browser: uploading, downloading, new
// folders, renaming and deleting. the folder shown is where uploads and
// dropped files go. library folders and their library.json, cells and
// bundles are managed in the libraries panel, they can't be renamed or
// deleted here.
class file_manager {
public:
    explicit file_manager(const std::filesystem::path& root);

    bool open = false;

    // message reports what was done or failed
    void gui(const std::function<void(std::string)>& message);

    // the folder shown
    const std::filesystem::path& folder() const { return list.dir; }
    // opens the panel at dir with file selected, in front
    void show(const std::filesystem::path& dir, const std::filesystem::path& file = {});

private:
    // why p can't be renamed or deleted here, null if it can
    const char* managed(const std::filesystem::path& p) const;
    void popups(const std::function<void(std::string)>& message);

    std::filesystem::path root;
    ui::file_list list;

    enum class asking { none, new_folder, rename, remove };
    asking ask = asking::none;
    // the entry renamed or deleted
    std::filesystem::path target;
    std::string name;
    std::string error;
    bool focus_name = false;
    // brought to the front at the next frame
    bool focus = false;
};

}
