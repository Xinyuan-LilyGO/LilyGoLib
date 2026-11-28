/**
 * @file      ui_nfc_info.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-25
 *
 * Responsive NFC hardware/software comparison with focusable capability rows.
 */
#include "ui_define.h"
#if defined(ARDUINO) && defined(USING_ST25R3916) && !defined(EXCLUDE_NFC_MFC)
#include "ui_nfc_info.h"

namespace {
static void focused(lv_event_t *event)
{
    lv_obj_scroll_to_view_recursive(lv_event_get_target_obj(event), LV_ANIM_ON);
}

static void details(lv_event_t *event)
{
    const auto *support = static_cast<const UINfcSupport *>(lv_event_get_user_data(event));
    create_msgbox(lv_obj_get_parent(lv_event_get_target_obj(event)), support->card,
                  support->details, nullptr, nullptr, nullptr);
}

static void legend(lv_event_t *event)
{
    create_msgbox(lv_obj_get_parent(lv_event_get_target_obj(event)), "NFC support",
        "HW: hardware radio support at 13.56 MHz.\nR: payload read.\nW: payload write.\nE: card emulation.\n\n"
        "Checks indicate implemented support, subject to card format, available keys and write protection. "
        "Radio compatibility does not mean full card copying. Emulation is experimental and needs RF testing. "
        "Erase clears supported NDEF content only. Select a card row for its limits.", nullptr, nullptr, nullptr);
}

static lv_obj_t *label(lv_obj_t *parent, const char *text)
{
    auto *object = lv_label_create(parent); lv_label_set_text(object, text);
    lv_obj_set_height(object, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(object, UI_COLOR_TEXT_PRIMARY, 0);
    return object;
}

static lv_obj_t *row(lv_obj_t *parent, int height, bool button)
{
    auto *object = button ? lv_button_create(parent) : lv_obj_create(parent);
    lv_obj_set_size(object, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(object, height, 0);
    lv_obj_set_style_pad_hor(object, 6, 0); lv_obj_set_style_pad_ver(object, 6, 0);
    lv_obj_set_style_pad_column(object, 3, 0);
    lv_obj_set_style_radius(object, 8, 0);
    lv_obj_set_style_border_width(object, button ? 1 : 0, 0);
    lv_obj_set_style_border_color(object, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_bg_color(object, UI_COLOR_CARD_BG, 0);
    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(object, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    if (button) {
        ui_add_accent_focus_style(object);
        lv_obj_add_event_cb(object, focused, LV_EVENT_FOCUSED, nullptr);
    }
    return object;
}

static void heading(lv_obj_t *parent, const char *text)
{
    auto *object = label(parent, text); lv_obj_set_width(object, 1); lv_obj_set_flex_grow(object, 1);
    lv_label_set_long_mode(object, LV_LABEL_LONG_SCROLL_CIRCULAR);
}
} // namespace

void ui_nfc_info_create(lv_obj_t *parent)
{
    const int width = lv_display_get_horizontal_resolution(nullptr) - 16;
    const auto *font = lv_theme_get_font_normal(parent);
    lv_point_t header_size;
    lv_text_get_size(&header_size, "HW", font, 0, 0, width, LV_TEXT_FLAG_NONE);
    const int column = LV_MAX(LV_CLAMP(26, width * 9 / 100, 46), header_size.x + 2);
    const int height = LV_MAX(32, lv_font_get_line_height(font) + 16);
    lv_obj_set_style_pad_row(parent, 4, 0);
    auto *intro = label(parent, "ST25R3916  /  13.56 MHz");
    lv_obj_set_width(intro, LV_PCT(100)); lv_label_set_long_mode(intro, LV_LABEL_LONG_WRAP);
    auto *header = row(parent, height - 8, true); heading(header, "Card");
    lv_obj_add_event_cb(header, [](lv_event_t *event) {
        // Reaching the header with the encoder must also reveal the intro.
        lv_obj_scroll_to_y(lv_obj_get_parent(lv_event_get_target_obj(event)), 0, LV_ANIM_ON);
    }, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(header, legend, LV_EVENT_CLICKED, nullptr);
    for (const char *text : {"HW", "R", "W", "E"}) {
        auto *cell = label(header, text); lv_obj_set_width(cell, column);
        lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
    }
    for (const auto &support : kNfcSupport) {
        auto *item = row(parent, height, true); heading(item, support.card);
        for (bool available : {support.hardware, support.read, support.write, support.emulate}) {
            auto *cell = label(item, available ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
            lv_obj_set_width(cell, column); lv_obj_set_style_text_font(cell, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(cell, available ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY, 0);
        }
        lv_obj_add_event_cb(item, details, LV_EVENT_CLICKED, const_cast<UINfcSupport *>(&support));
    }
    auto *help = row(parent, height, true); heading(help, "Legend / limits");
    lv_obj_add_event_cb(help, legend, LV_EVENT_CLICKED, nullptr);
}
#endif
