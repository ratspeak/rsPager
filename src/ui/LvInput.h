#pragma once

#include <lvgl.h>

class Keyboard;
class Scrollwheel;
class TouchInput;
struct KeyEvent;

// LVGL input device drivers for T-Pager keyboard/encoder hardware
namespace LvInput {

void init(Keyboard* kb, Scrollwheel* sw, TouchInput* touch);

// Feed a KeyEvent into the LVGL keypad indev (called from main loop)
void feedKey(const KeyEvent& evt);

// Get the LVGL input group (for focusing widgets)
lv_group_t* group();

// Navbar (tab-cycling) mode: hide the in-content focus ring. Group focus stays
// logical so entering the screen restores the ring on the last-focused widget.
void setFocusSuppressed(bool suppressed);
bool focusSuppressed();

// Focus a widget through the suppression filter — use instead of
// lv_group_focus_obj() for a screen's initial focus placement.
void focusObj(lv_obj_t* obj);

// Re-apply palette colors to the touch cursor after a theme switch
void applyTheme();

}  // namespace LvInput
