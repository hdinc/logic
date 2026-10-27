#pragma once

// icons of font awesome free, solid style (res/fonts/fa-solid-900.ttf, sil
// ofl 1.1, icons cc by 4.0), merged into the interface font by load_fonts.
// each is the utf-8 of the icon's codepoint, see fontawesome.com/icons (free,
// solid) to add more.

namespace logic::ui {

namespace icon {

inline constexpr const char* play = "\xef\x81\x8b"; // play
inline constexpr const char* pause = "\xef\x81\x8c"; // pause
inline constexpr const char* simulate = "\xef\x85\x84"; // circle-play
inline constexpr const char* stop_simulation = "\xef\x8a\x8d"; // circle-stop
inline constexpr const char* cancel = "\xef\x81\x97"; // circle-xmark
inline constexpr const char* reset = "\xef\x81\x88"; // backward-step
inline constexpr const char* step = "\xef\x81\x91"; // forward-step
inline constexpr const char* undo = "\xef\x83\xa2"; // arrow-rotate-left
inline constexpr const char* redo = "\xef\x80\x9e"; // arrow-rotate-right
inline constexpr const char* select = "\xef\x89\x85"; // arrow-pointer
inline constexpr const char* wire = "\xef\x8c\x83"; // pencil
inline constexpr const char* input = "\xef\x8b\xb6"; // right-to-bracket
inline constexpr const char* output = "\xef\x8b\xb5"; // right-from-bracket
inline constexpr const char* cell = "\xef\x8b\x9b"; // microchip
inline constexpr const char* waveforms = "\xef\xa0\xbe"; // wave-square
inline constexpr const char* signals = "\xef\x83\xbe"; // square-plus
inline constexpr const char* upload = "\xef\x82\x93"; // upload
inline constexpr const char* download = "\xef\x80\x99"; // download
inline constexpr const char* folder = "\xef\x81\xbb"; // folder
inline constexpr const char* new_folder = "\xef\x99\x9e"; // folder-plus
inline constexpr const char* file = "\xef\x85\x9b"; // file
inline constexpr const char* rename = "\xef\x81\x84"; // pen-to-square
inline constexpr const char* trash = "\xef\x8b\xad"; // trash-can

}

// size of the toolbar buttons, chosen in their right click menu or View >
// Toolbar icons, saved with the settings
enum class toolbar_size {
    small,
    medium,
    large
};
toolbar_size get_toolbar_size();
void set_toolbar_size(toolbar_size s);
// the side of a toolbar button in pixels, never smaller than a text button
float toolbar_button_size();
// the space between toolbar buttons
float toolbar_gap();
// the choices as menu items
void toolbar_size_menu();

// a square toolbar button showing one icon, highlighted while active, greyed
// out unless enabled. the tooltip names it, since there is no text, and
// shows while it's greyed out too. the icon's visible shape is centered. a
// right click chooses the size. text after it on the same line is centered
// on it.
bool icon_button(const char* icon, const char* tip, bool active = false, bool enabled = true);

// moves the next control with a frame (checkbox, input) down to the middle
// of a toolbar line, text gets there by itself
void toolbar_center();

}
