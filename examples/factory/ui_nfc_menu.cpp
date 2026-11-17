/**
 * @file      ui_nfc_menu.cpp
 * @brief     Hierarchical NFC application menu.
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 */
#include "ui_define.h"

#if defined(ARDUINO)

static lv_obj_t *nfc_page = nullptr;
static lv_obj_t *nfc_parent = nullptr;
static ui_nfc_child_back_cb_t nfc_child_back_cb = nullptr;

static void show_nfc_menu();
static void show_other_menu();
static void return_to_nfc_menu();
static void return_to_other_menu();

#if !defined(EXCLUDE_NFC) && !defined(ARDUINO_T_EMBED_PLUS)
extern app_t ui_nfc_main;
#endif

#if defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_T_EMBED_PLUS)
#if !defined(EXCLUDE_PN532)
extern app_t ui_pn532_main;
extern app_t ui_pn532_write_main;
extern app_t ui_pn532_erase_main;
#endif
#endif

static void destroy_nfc_page()
{
    if (nfc_page) {
        ui_destroy_app_page(nfc_page);
        nfc_page = nullptr;
    }
}

void ui_nfc_set_child_back(ui_nfc_child_back_cb_t cb)
{
    nfc_child_back_cb = cb;
}

void ui_nfc_child_back(void)
{
    ui_nfc_child_back_cb_t cb = nfc_child_back_cb;
    nfc_child_back_cb = nullptr;
    if (cb) {
        cb();
    } else {
        menu_show();
    }
}

static void unavailable(const char *name)
{
    char message[96];
    snprintf(message, sizeof(message), "%s is not available on this device.", name);
    ui_msg_pop_up("NFC", message);
}

static app_t *read_app()
{
#if (defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_T_EMBED_PLUS)) && \
    !defined(EXCLUDE_PN532)
    if (hw_get_device_online() & HW_NFC_ONLINE) {
        return &ui_pn532_main;
    }
#endif
#if !defined(EXCLUDE_NFC) && !defined(ARDUINO_T_EMBED_PLUS)
    return &ui_nfc_main;
#else
    return nullptr;
#endif
}

static app_t *write_app()
{
#if (defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_T_EMBED_PLUS)) && \
    !defined(EXCLUDE_PN532)
    if (hw_get_device_online() & HW_NFC_ONLINE) {
        return &ui_pn532_write_main;
    }
#endif
    return nullptr;
}

static app_t *erase_app()
{
#if (defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_T_EMBED_PLUS)) && \
    !defined(EXCLUDE_PN532)
    if (hw_get_device_online() & HW_NFC_ONLINE) {
        return &ui_pn532_erase_main;
    }
#endif
    return nullptr;
}

static void launch_app(app_t *app, ui_nfc_child_back_cb_t back_cb)
{
    if (!app || !app->setup_func_cb || !nfc_parent) return;
    lv_obj_t *parent = nfc_parent;
    destroy_nfc_page();
    ui_nfc_set_child_back(back_cb);
    app->setup_func_cb(parent);
}

static const char *button_text(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target_obj(event);
    lv_obj_t *label = button ? lv_obj_get_child(button, 0) : nullptr;
    return label ? lv_label_get_text(label) : "";
}

static void nfc_menu_button_cb(lv_event_t *event)
{
    const char *text = button_text(event);
    if (strcmp(text, "Read") == 0) {
        app_t *app = read_app();
        if (app) launch_app(app, return_to_nfc_menu);
        else unavailable("Read");
    } else if (strcmp(text, "Write") == 0) {
        app_t *app = write_app();
        if (app) launch_app(app, return_to_nfc_menu);
        else unavailable("Write");
    } else if (strcmp(text, "Other") == 0) {
        destroy_nfc_page();
        show_other_menu();
    }
}

static void nfc_menu_back_cb(lv_event_t *)
{
    destroy_nfc_page();
    menu_show();
}

static void other_menu_back_cb(lv_event_t *)
{
    destroy_nfc_page();
    show_nfc_menu();
}

static void return_to_nfc_menu()
{
    destroy_nfc_page();
    show_nfc_menu();
}

static void return_to_other_menu()
{
    destroy_nfc_page();
    show_other_menu();
}

static lv_obj_t *create_primary_button(lv_obj_t *parent, const char *text,
                                       lv_event_cb_t event_cb)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, 40);
    lv_obj_set_style_bg_color(button, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 4, 0);
    ui_add_accent_focus_style(button);
    lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, nullptr);
    if (lv_group_t *group = lv_group_get_default()) {
        lv_group_add_obj(group, button);
    }

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return button;
}

static void show_nfc_menu()
{
    nfc_page = ui_create_app_page(nfc_parent, "NFC", nfc_menu_back_cb);
    lv_obj_t *card = ui_create_card(nfc_page, nullptr);
    lv_obj_set_style_pad_row(card, 8, 0);
    create_primary_button(card, "Read", nfc_menu_button_cb);
    create_primary_button(card, "Write", nfc_menu_button_cb);
    create_primary_button(card, "Other", nfc_menu_button_cb);
}

static void show_other_menu()
{
    nfc_page = ui_create_app_page(nfc_parent, "Other", other_menu_back_cb);
    lv_obj_t *card = ui_create_card(nfc_page, "Other");
    ui_create_card_button(card, LV_SYMBOL_TRASH, nullptr, "Erase", [](lv_event_t *) {
        app_t *app = erase_app();
        if (app) launch_app(app, return_to_other_menu);
        else unavailable("Erase");
    });
    ui_create_card_button(card, LV_SYMBOL_LIST, nullptr, "Copy Tag", [](lv_event_t *) {
        ui_msg_pop_up("Copy Tag", "Function not available yet.");
    });
}

static void ui_nfc_menu_enter(lv_obj_t *parent)
{
    nfc_parent = parent;
    ui_nfc_set_child_back(nullptr);
    show_nfc_menu();
}

static void ui_nfc_menu_exit(lv_obj_t *)
{
    destroy_nfc_page();
    ui_nfc_set_child_back(nullptr);
    nfc_parent = nullptr;
}

app_t ui_nfc_menu_main = {
    .setup_func_cb = ui_nfc_menu_enter,
    .exit_func_cb = ui_nfc_menu_exit,
    .user_data = nullptr,
};

#endif
