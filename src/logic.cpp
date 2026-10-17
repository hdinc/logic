#include "logic.h"
#include "Hd.h"
#include <string>
#include <cstring>
#include <imgui_internal.h>

#include "library/library.h"

namespace logic {

logic::logic(Hd::Window* w)
    : editor_tex{Hd::Texture::emptyTexture(3840, 2160)}
    , editor_fb{editor_tex}
    , window{w}
    , gui{window}
{
    library::library work;
    work.name = "work";
    libraries.insert({"work", std::move(work)});

    library::library builtin;
    builtin.name = "builtin";
    libraries.insert({"builtin", std::move(builtin)});
}

logic::~logic()
{
}

void logic::exec()
{

    auto f = Hd::Resource::getExeRes("res/fonts/Roboto.ttf");
    void* font_data = new char[f.second];
    memcpy(font_data, f.first, f.second);
    ImGui::GetIO().Fonts->AddFontFromMemoryTTF(font_data, f.second, 20);

    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.FrameRounding = 10.0f;
    s.GrabRounding = 10.0f;
    s.WindowRounding = 10.0f;

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    gui.Functions.add([&] { draw_gui(); });

    while (!window->ShouldClose()) {

        window->PollEvents();

        process_inputs();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        gui.Draw();
        draw();

        window->SwapBuffers();
    }
}

void logic::process_inputs()
{
}

void logic::draw()
{
}

void logic::draw_gui()
{
    using namespace ImGui;

    if (BeginMainMenuBar()) {

        if (BeginMenu("File")) {
            if (MenuItem("Exit")) {
            }
            EndMenu();
        }

        if (BeginMenu("Help")) {
            EndMenu();
        }

        EndMainMenuBar();
    }
    DockSpaceOverViewport();

    if (IsKeyPressed(ImGuiKey_F1)) {
        show_demo = !show_demo;
    }

    if (show_demo) {
        ShowDemoWindow();
    }

    auto flags = ImGuiWindowFlags_NoCollapse;
    Begin("Libraries", 0, flags);

    // if (Button("Add New")) {
    // circuit c;
    // for (int i = 0;; i++) {
    //     std::string n = "New" + std::to_string(i);
    //     for (int j = 0; j < circuits.size(); j++) {
    //         if (circuits[j].c.get_name() == n) {
    //             goto end_1;
    //         }
    //     }
    //     c.set_name(n);
    //     circuits.push_back({std::move(c), false});
    //     break;
    // end_1:;
    // }
    // }

    // Separator();

    // for (auto& c : circuits) {
    //     if (c.rename) {
    //         static char buf[50];
    //         c.c.get_name().copy(buf, 50);
    //         if (c.rename_activated) {
    //             SetKeyboardFocusHere();
    //             c.rename_activated = false;
    //         }
    //         if (InputText("##input_text", buf, 50, ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue)) {
    //             c.rename = false;
    //             c.rename_activated = false;
    //             auto old_name = c.c.get_name();
    //             c.c.set_name(std::string(buf));
    //             for (auto& ce : circuit_editors) {
    //                 if (ce->name == old_name) {
    //                     ce->name = c.c.get_name();
    //                     // ce->tab_select = true;
    //                 }
    //             }
    //         }
    //     } else {
    //         if (Button(c.c.get_name().c_str())) {
    //             for (int i = 0; i < circuit_editors.size(); i++) {
    //                 if (circuit_editors[i]->name == c.c.get_name()) {
    //                     // circuit_editors[i]->tab_select = true;
    //                     goto end_2;
    //                 }
    //             }
    //             circuit_editors.emplace_back(new editor::circuit_editor::circuit_editor(c.c.get_name()));
    //             // circuit_editors.back()->tab_select = true;
    //         end_2:;
    //         };
    //         if (IsItemHovered()) {
    //             if (IsKeyPressed(ImGuiKey_F2)) {
    //                 c.rename = true;
    //                 c.rename_activated = true;
    //             }
    //         }
    //     }
    // }

    for (auto& l : libraries) {

        // PushStyleColor(ImGuiCol_HeaderHovered, 0x000000);
        // PushStyleColor(ImGuiCol_HeaderActive, 0x000000);

        bool b = TreeNode(l.first.c_str());

        if (BeginPopupContextItem()) {
            if (Selectable("Add New")) {
                library::circuit c;
                int n = 1;
                std::string name;
                while (1) {
                    name = "New " + std::to_string(n);
                    for (auto& c : l.second.circuits) {
                        if (name == c.first) {
                            n++;
                            goto end;
                        }
                    }
                    break;
                end:;
                }
                c.name = name;
                l.second.circuits.insert({name, std::move(c)});
                CloseCurrentPopup();
            }
            EndPopup();
        }

        if (b) {

            for (auto& c : l.second.circuits) {
                if (Selectable(c.first.c_str())) {
                    std::string n = l.first + "::" + c.first;
                    if (editors.find(n) == editors.end()) {
                        auto e = std::make_unique<editor_data>(l.first, c.first, libraries);
                        editors.insert({n, std::move(e)});
                    }
                }
            }
            TreePop();
        }

        // PopStyleColor(2);
    }

    End();

    Begin("Editor", 0, flags);

    if (IsWindowFocused()) {
        if (IsKeyPressed(ImGuiKey_Tab)) {
            show_symbol_editor = !show_symbol_editor;
        }
    }

    editor::circuit_editor::circuit_editor* current_circuit_editor = 0;
    editor::symbol_editor::symbol_editor* current_symbol_editor = 0;

    if (BeginTabBar("Editor Tabs", ImGuiTabBarFlags_Reorderable)) {

        for (auto& e : editors) {

            ImGuiTabItemFlags flags = 0;
            // if (circuit_editors[i]->tab_select) {
            //     flags = ImGuiTabItemFlags_SetSelected;
            //     circuit_editors[i]->tab_select = false;
            // }
            if (BeginTabItem(e.second->name.c_str(), 0, flags)) {

                current_circuit_editor = &e.second->ce;
                current_symbol_editor = &e.second->se;

                auto s = GetContentRegionAvail();
                if (s.x > 1.0f && s.y > 1.0f) {
                    auto cpos = GetCursorScreenPos();
                    auto draw_pos = GetCursorPos();
                    InvisibleButton("dummy", s);
                    if (show_symbol_editor) {

                        if (IsItemHovered() && IsWindowFocused()) {
                            auto mpos_ = GetMousePos();
                            e.second->se.mpos = {2.0f * ((mpos_.x - cpos.x) / s.x) - 1.0f, 2.0f * (1.0f - (mpos_.y - cpos.y) / s.y) - 1.0f};
                            e.second->se.process_inputs();
                        }

                        e.second->se.draw(&editor_fb, s.x, s.y);
                        //
                        SetCursorPos(draw_pos);
                        Image((ImTextureID)editor_tex.Id(), {s.x, s.y}, {0, s.y / editor_tex.GetHeight()}, {s.x / editor_tex.GetWidth(), 0});
                        SetCursorPos(draw_pos);

                    } else {
                        // selected_circuit_editor = circuit_editors[i];

                        static bool search_popup_activated = false;
                        // SetItemUsingMouseWheel();
                        if (IsItemHovered() && IsWindowFocused()) {

                            auto mpos_ = GetMousePos();
                            e.second->ce.mpos = {2.0f * ((mpos_.x - cpos.x) / s.x) - 1.0f, 2.0f * (1.0f - (mpos_.y - cpos.y) / s.y) - 1.0f};
                            e.second->ce.process_inputs();

                            if (IsKeyPressed(ImGuiKey_Space)) {
                                OpenPopup("search popup");
                                search_popup_activated = true;
                            }

                            if (IsKeyDown(ImGuiKey_LeftCtrl) && IsKeyPressed(ImGuiKey_S)) {
                                save_editor(*e.second);
                                printf("save\n");
                            }
                        }

                        e.second->ce.draw(&editor_fb, s.x, s.y);

                        SetCursorPos(draw_pos);
                        Image((ImTextureID)editor_tex.Id(), {s.x, s.y}, {0, s.y / editor_tex.GetHeight()}, {s.x / editor_tex.GetWidth(), 0});
                        SetCursorPos(draw_pos);

                        if (BeginPopup("search popup")) {
                            static char buf[100];
                            if (search_popup_activated) {
                                SetKeyboardFocusHere();
                                search_popup_activated = false;
                            }
                            if (InputText("Search", buf, 100, ImGuiInputTextFlags_EscapeClearsAll)) {
                            }
                            BeginChild("search popup results", {0, 200});
                            for (auto& c : libraries["work"].circuits) {
                                if (Selectable(c.first.c_str())) {
                                    printf("selected\n");
                                    e.second->ce.add_component("work", c.first);
                                    CloseCurrentPopup();
                                };
                            }
                            EndChild();
                            EndPopup();
                        }
                    }
                }

                EndTabItem();
            }
        }

        EndTabBar();
    }

    End();

    Begin("Properties");

    if (show_symbol_editor) {
        if (current_symbol_editor) {
            current_symbol_editor->property_editor();
        }
    } else {
        if (current_circuit_editor) {
            current_circuit_editor->property_editor();
        }
    }

    // if (selected_circuit_editor) {
    //     if (selected_circuit_editor->selected_component) {
    //         selected_circuit_editor->selected_component->property_editor();
    //     }
    // }

    End();
}

void logic::save_editor(logic::editor_data& e)
{
    library::circuit c;

    c.name = e.name;
    c.wires = e.ce.get_wires();
    c.components = e.ce.get_components();
    c.ports = e.ce.get_ports();
    c.symbol = e.se.get_symbol();

    if (c.ports.size() != c.symbol.pins.size()) {
        printf("ports size and symbol pins size are different");
        return;
    }

    for (int i = 0; i < c.ports.size(); i++) {
        for (auto& p : c.ports) {
            if (p.number == i + 1) {
                goto e1;
            }
        }
        printf("invalid port numbers");
        return;
    e1:;
    }

    for (int i = 0; i < c.symbol.pins.size(); i++) {
        for (auto& p : c.symbol.pins) {
            if (p.number == i + 1) {
                goto e2;
            }
        }
        printf("invalid pin numbers");
        return;
    e2:;
    }

    auto l = libraries.find(e.library_name);
    if (l != libraries.end()) {
        l->second.circuits[e.name] = std::move(c);
    } else {
        printf("no library named %s", e.library_name.c_str());
    }
}

}
