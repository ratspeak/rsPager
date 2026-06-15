#include "LvPowerOffOverlay.h"
#include "ui/Theme.h"
#include "fonts/fonts.h"
#include <lvgl.h>

void LvPowerOffOverlay::create() {
    if (_overlay) return;
    _overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_overlay, 320, 110);
    lv_obj_center(_overlay);
    lv_obj_set_style_bg_color(_overlay, lv_color_hex(Theme::BG_ELEVATED), 0);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_overlay, lv_color_hex(Theme::ACCENT), 0);
    lv_obj_set_style_border_width(_overlay, 1, 0);
    lv_obj_set_style_radius(_overlay, 4, 0);
    lv_obj_set_style_pad_all(_overlay, 8, 0);
    lv_obj_set_style_pad_row(_overlay, 6, 0);
    lv_obj_set_layout(_overlay, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(_overlay);
    lv_obj_set_style_text_font(title, &lv_font_ratdeck_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(Theme::ACCENT), 0);
    lv_label_set_text(title, "Power off?");

    _lblDetail = lv_label_create(_overlay);
    lv_obj_set_style_text_font(_lblDetail, &lv_font_ratdeck_12, 0);
    lv_obj_set_style_text_color(_lblDetail, lv_color_hex(Theme::TEXT_PRIMARY), 0);
    lv_label_set_long_mode(_lblDetail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_lblDetail, 300);
    lv_obj_set_style_text_align(_lblDetail, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_lblDetail, "");

    lv_obj_t* hint = lv_label_create(_overlay);
    lv_obj_set_style_text_font(hint, &lv_font_ratdeck_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(Theme::TEXT_MUTED), 0);
    lv_label_set_text(hint, "Enter powers off | Esc cancels");

    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

void LvPowerOffOverlay::show(bool usbPowered) {
    if (!_overlay) create();
    if (_lblDetail) {
        lv_label_set_text(_lblDetail, usbPowered
            ? "On USB: stays dark until unplugged. Wheel click wakes."
            : "Wake: hold PWR (right button) 1s, or plug USB.");
    }
    lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
    _visible = true;
}

void LvPowerOffOverlay::hide() {
    if (_overlay) lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
    _visible = false;
}

bool LvPowerOffOverlay::handleKey(const KeyEvent& event) {
    if (!_visible) return false;
    if (event.repeat) return true;
    hide();
    if (event.enter && onConfirm) onConfirm();
    return true;
}
