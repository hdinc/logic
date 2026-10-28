#include "zoom.h"
#include "icons.h"

#include <imgui_internal.h>

#include <algorithm>
#include <vector>

namespace logic::ui::zoom {

namespace {

std::string zoomed;
std::string focused;
// the open windows of this frame, in order
std::vector<std::string> windows;
// per begin: whether ImGui::Begin was called
std::vector<bool> begun;
// select the zoomed tab in the bar once after a change
bool select_tab = false;

// "Waveforms: top###waveforms1" is shown as "Waveforms: top"
std::string label_of(const std::string& name)
{
    auto at = name.find("###");
    return at == std::string::npos ? name : name.substr(0, at);
}

}

float bar_height()
{
    auto& style = ImGui::GetStyle();
    return ImGui::GetFrameHeight() + style.WindowPadding.y * 2;
}

bool active()
{
    return !zoomed.empty();
}

const std::string& current()
{
    return zoomed;
}

void set(const std::string& name)
{
    if (zoomed == name) return;
    zoomed = name;
    select_tab = true;
}

void clear()
{
    zoomed.clear();
}

void toggle()
{
    if (active())
        clear();
    else
        set(focused.empty() ? "Editor" : focused);
}

bool begin(const char* name, bool* open, ImGuiWindowFlags flags)
{
    using namespace ImGui;
    if (!open || *open) windows.push_back(name);

    if (active() && zoomed != name) {
        // its size, position or dock asked for don't go to the next window
        GImGui->NextWindowData.ClearFlags();
        begun.push_back(false);
        return false;
    }

    if (!active()) {
        bool visible = Begin(name, open, flags);
        begun.push_back(true);
        // the last item is the dock tab or the title bar
        if (IsItemHovered() && IsMouseDoubleClicked(ImGuiMouseButton_Left)) set(name);
        if (BeginPopupContextItem("##zoom")) {
            if (MenuItem("Zoom")) set(name);
            SetItemTooltip("Fill the app with this window, the others become tabs at the bottom");
            EndPopup();
        }
        if (IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) focused = name;
        return visible;
    }

    // the zoomed window over everything docked, above the tab bar. a dock
    // asked for its normal window doesn't apply here.
    GImGui->NextWindowData.HasFlags &= ~ImGuiNextWindowDataFlags_HasDock;
    auto vp = GetMainViewport();
    SetNextWindowPos(vp->WorkPos);
    SetNextWindowSize({vp->WorkSize.x, std::max(1.0f, vp->WorkSize.y - bar_height())});
    // imgui hashes what follows the last ###, so the name's own ### mustn't
    // be in the id or this would be the window of the layout
    std::string key = name;
    std::replace(key.begin(), key.end(), '#', '_');
    auto id = label_of(name) + "###zoom_" + key;
    bool visible = Begin(id.c_str(), open, flags | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    begun.push_back(true);
    if (IsItemHovered() && IsMouseDoubleClicked(ImGuiMouseButton_Left)) clear();
    if (BeginPopupContextItem("##zoom")) {
        if (MenuItem("Unzoom")) clear();
        EndPopup();
    }
    focused = name;
    return visible;
}

bool shown()
{
    return !begun.empty() && begun.back();
}

void end()
{
    if (begun.empty()) return;
    bool was = begun.back();
    begun.pop_back();
    if (was) ImGui::End();
}

void end_frame()
{
    using namespace ImGui;
    auto list = std::move(windows);
    windows.clear();
    if (active() && std::find(list.begin(), list.end(), zoomed) == list.end()) clear();
    if (!active()) return;

    auto vp = GetMainViewport();
    float h = bar_height();
    SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h});
    SetNextWindowSize({vp->WorkSize.x, h});
    Begin("##zoom_bar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    float button = GetFrameHeight();
    std::string chosen;
    // the tabs, with room for the unzoom button at the right
    float room = std::max(1.0f, GetContentRegionAvail().x - button - GetStyle().ItemSpacing.x);
    BeginChild("tabs", {room, GetFrameHeight()}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    if (BeginTabBar("windows", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (auto& name : list) {
            PushID(name.c_str());
            ImGuiTabItemFlags flags = select_tab && name == zoomed ? ImGuiTabItemFlags_SetSelected : 0;
            if (BeginTabItem((label_of(name) + "###tab").c_str(), nullptr, flags)) {
                if (!select_tab && name != zoomed) chosen = name;
                EndTabItem();
            }
            PopID();
        }
        EndTabBar();
    }
    EndChild();
    select_tab = false;
    SameLine();
    if (small_icon_button(icon::unzoom, "Unzoom: back to the layout")) clear();
    End();
    if (!chosen.empty()) set(chosen);
}

}
