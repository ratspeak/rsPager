#pragma once

#include "ui/UIManager.h"

class LvHelpOverlay {
public:
    void create();
    void show();
    void hide();
    bool isVisible() const { return _visible; }
    void toggle();
    bool handleKey(const KeyEvent& event);

private:
    lv_obj_t* _overlay = nullptr;
    bool _visible = false;
};
