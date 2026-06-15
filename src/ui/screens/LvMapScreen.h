#pragma once

#include "ui/UIManager.h"

class LvMapScreen : public LvScreen {
public:
    void createUI(lv_obj_t* parent) override;
    const char* title() const override { return "Map"; }
};
