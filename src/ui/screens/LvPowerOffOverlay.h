#pragma once

#include "ui/UIManager.h"

// Power-off confirm dialog — shown on a BOOT press.
class LvPowerOffOverlay {
public:
    void create();
    void show(bool usbPowered);
    void hide();
    bool isVisible() const { return _visible; }
    // Enter/encoder click confirms via onConfirm; any other key cancels.
    bool handleKey(const KeyEvent& event);

    void (*onConfirm)() = nullptr;

private:
    lv_obj_t* _overlay = nullptr;
    lv_obj_t* _lblDetail = nullptr;
    bool _visible = false;
};
