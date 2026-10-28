#pragma once

#include <imgui.h>

#include <string>

// one window filling the app, the other open windows as tabs at the bottom
// (like zoom/unzoom in questa). the docked layout isn't touched: while a
// window is zoomed the others aren't submitted, and the zoomed one is drawn
// in a window of its own over the dockspace.
namespace logic::ui::zoom {

// in place of ImGui::Begin for a window that can be zoomed, returns like
// Begin. false without a window while another window is zoomed. end()
// must always follow. a double click on the window's tab (or the title bar
// of a floating window) zooms it, so does its right click menu.
bool begin(const char* name, bool* open = nullptr, ImGuiWindowFlags flags = 0);
void end();
// whether the last begin() made a window (so its content is drawn), false
// while another window is zoomed
bool shown();

bool active();
// the zoomed window's name, empty if none
const std::string& current();
void set(const std::string& name);
// back to the layout
void clear();
// zooms the window that had focus last, else "Editor"; unzooms if zoomed
void toggle();

// after all windows of the frame: the tab bar while zoomed, and the end of
// a zoom whose window wasn't submitted (e.g. it was closed)
void end_frame();

// the height of the tab bar at the bottom
float bar_height();

}
