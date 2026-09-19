/**
 * @file      ui_usb_disk.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-08-28
 * 
 * The USB stack is initialized by LilyGoLib at boot. This page only exposes or
 * hides the SD media, so other apps do not unexpectedly share the filesystem
 * with the host computer.
 */
#include "ui_define.h"

#ifdef ARDUINO
#include <SD.h>
#endif

#if !defined(EXCLUDE_USB_DISK)

static lv_obj_t *page_container = NULL;
static lv_timer_t *status_timer = NULL;
static lv_obj_t *usb_label = NULL;
static lv_obj_t *storage_label = NULL;
static lv_obj_t *capacity_label = NULL;
static lv_obj_t *msc_label = NULL;
static lv_obj_t *msc_switch = NULL;
static lv_obj_t *backend_dropdown = NULL;
static lv_obj_t *exclusive_overlay = NULL;
static lv_timer_t *exclusive_timer = NULL;
static lv_timer_t *display_refr_timer = NULL;
static bool exclusive_mode = false;

static lv_obj_t *row_value(lv_obj_t *row)
{
    if (!row) return NULL;
    uint32_t count = lv_obj_get_child_count(row);
    if (!count) return NULL;
    return lv_obj_get_child(row, count - 1);
}

static void format_bytes(char *out, size_t out_size, uint64_t bytes)
{
    if (!out || out_size == 0) return;
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        snprintf(out, out_size, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        snprintf(out, out_size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        snprintf(out, out_size, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(out, out_size, "%llu B", (unsigned long long)bytes);
    }
}

static bool ensure_sd_ready(void)
{
#ifdef ARDUINO
#if defined(HAS_SD_CARD_SOCKET)
    return hw_is_sd_insert();
#else
    return false;
#endif
#else
    return false;
#endif
}

static bool ensure_backend_ready(lilygo_usb_msc_backend_t backend)
{
    if (backend == LILYGO_USB_MSC_BACKEND_SD) {
        ensure_sd_ready();
    }
    return lilygo_usb_msc_backend_available(backend);
}

static lilygo_usb_msc_backend_t dropdown_backend(void)
{
    if (!backend_dropdown) return lilygo_usb_msc_get_backend();
    return lv_dropdown_get_selected(backend_dropdown) == 0
           ? LILYGO_USB_MSC_BACKEND_SD
           : LILYGO_USB_MSC_BACKEND_FLASH;
}

static void set_all_indev_enabled(bool enabled)
{
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        lv_indev_enable(indev, enabled);
    }
}

static void sync_dropdown_selection(void)
{
    if (!backend_dropdown) return;
    lv_dropdown_set_selected(backend_dropdown,
                             lilygo_usb_msc_get_backend() == LILYGO_USB_MSC_BACKEND_SD ? 0 : 1);
}

static void update_status(lv_timer_t *timer)
{
    (void)timer;

    lilygo_usb_msc_poll();

    if (usb_label) {
        lv_label_set_text(usb_label, lilygo_usb_service_available() ? "TinyUSB" : "Unavailable");
    }

    lilygo_usb_msc_backend_t backend = lilygo_usb_msc_get_backend();
    bool backend_ready = lilygo_usb_msc_is_active() ? true : lilygo_usb_msc_backend_available(backend);
    if (storage_label) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s %s",
                 lilygo_usb_msc_backend_name(backend),
                 backend_ready ? "Ready" : "Not Ready");
        lv_label_set_text(storage_label, buf);
    }

    if (capacity_label) {
        char buf[32];
        uint64_t bytes = lilygo_usb_msc_capacity_bytes();
        format_bytes(buf, sizeof(buf), bytes);
        lv_label_set_text(capacity_label, bytes ? buf : "--");
    }

    if (msc_label) {
        lv_label_set_text(msc_label, lilygo_usb_msc_status());
    }

    if (msc_switch) {
        bool active = lilygo_usb_msc_is_active();
        if (active) {
            lv_obj_add_state(msc_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(msc_switch, LV_STATE_CHECKED);
        }
    }
}

static void leave_exclusive_mode()
{
    if (!exclusive_mode) return;

    exclusive_mode = false;

    if (exclusive_timer) {
        lv_timer_del(exclusive_timer);
        exclusive_timer = NULL;
    }

    if (display_refr_timer) {
        lv_timer_resume(display_refr_timer);
        display_refr_timer = NULL;
    }

    if (exclusive_overlay) {
        lv_obj_delete(exclusive_overlay);
        exclusive_overlay = NULL;
    }

    set_all_indev_enabled(true);
    enable_input_devices();

    if (page_container) {
        ui_app_page_disable_nav_auto_hide(page_container);
    }

    if (msc_switch) {
        lv_obj_clear_state(msc_switch, LV_STATE_CHECKED);
    }

    update_status(NULL);
    if (!status_timer) {
        status_timer = lv_timer_create(update_status, 1000, NULL);
    }

    lv_refr_now(NULL);
}

static void exclusive_poll_cb(lv_timer_t *timer)
{
    (void)timer;
    lilygo_usb_msc_poll();
    if (!lilygo_usb_msc_is_active()) {
        leave_exclusive_mode();
    }
}

static void create_exclusive_overlay(lilygo_usb_msc_backend_t backend)
{
    exclusive_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(exclusive_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(exclusive_overlay, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(exclusive_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(exclusive_overlay, 0, 0);
    lv_obj_set_style_radius(exclusive_overlay, 0, 0);
    lv_obj_set_style_pad_all(exclusive_overlay, 16, 0);
    lv_obj_remove_flag(exclusive_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(exclusive_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(exclusive_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(exclusive_overlay,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_label_create(exclusive_overlay);
    lv_label_set_text(icon, LV_SYMBOL_USB);
    lv_obj_set_style_text_color(icon, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);

    lv_obj_t *title = lv_label_create(exclusive_overlay);
    lv_label_set_text(title, "USB Disk Mode");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    lv_obj_t *storage = lv_label_create(exclusive_overlay);
    lv_label_set_text_fmt(storage, "%s exposed", lilygo_usb_msc_backend_name(backend));
    lv_obj_set_style_text_color(storage, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(storage, &lv_font_montserrat_14, 0);

    lv_obj_t *hint = lv_label_create(exclusive_overlay);
    lv_label_set_text(hint, "Please wait for the disk to appear on your computer.\nEject from computer to return.");
    lv_obj_set_style_text_color(hint, UI_COLOR_WARNING, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_width(hint, LV_PCT(90));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
}

static bool enter_exclusive_mode(lilygo_usb_msc_backend_t backend)
{
    if (exclusive_mode) return true;

    if (status_timer) {
        lv_timer_del(status_timer);
        status_timer = NULL;
    }

    if (page_container) {
        ui_app_page_disable_nav_auto_hide(page_container);
    }

    create_exclusive_overlay(backend);
    lv_refr_now(NULL);

    disable_input_devices();
    set_all_indev_enabled(false);

    display_refr_timer = lv_display_get_refr_timer(NULL);
    if (display_refr_timer) {
        lv_timer_pause(display_refr_timer);
    }

    exclusive_mode = true;

    if (!lilygo_usb_msc_set_active(true)) {
        leave_exclusive_mode();
        return false;
    }

    exclusive_timer = lv_timer_create(exclusive_poll_cb, 500, NULL);
    if (!exclusive_timer) {
        lilygo_usb_msc_set_active(false);
        leave_exclusive_mode();
        return false;
    }

    return true;
}

static void expose_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (checked) {
        lilygo_usb_msc_backend_t backend = dropdown_backend();
        if (!ensure_backend_ready(backend) ||
                !lilygo_usb_msc_select_backend(backend) ||
                !enter_exclusive_mode(backend)) {
            lv_obj_clear_state(sw, LV_STATE_CHECKED);
        }
    } else {
        if (!lilygo_usb_msc_set_active(false)) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
    }

    if (!exclusive_mode) {
        update_status(NULL);
    }
}

static void backend_dropdown_cb(lv_event_t *e)
{
    (void)e;
    if (exclusive_mode) return;
    lilygo_usb_msc_backend_t backend = dropdown_backend();
    if (lilygo_usb_msc_is_active()) {
        if (!lilygo_usb_msc_set_active(false)) {
            sync_dropdown_selection();
            if (msc_switch) {
                lv_obj_add_state(msc_switch, LV_STATE_CHECKED);
            }
            update_status(NULL);
            return;
        }
    }
    if (msc_switch) {
        lv_obj_clear_state(msc_switch, LV_STATE_CHECKED);
    }
    ensure_backend_ready(backend);
    lilygo_usb_msc_select_backend(backend);
    sync_dropdown_selection();
    update_status(NULL);
}

static void refresh_btn_cb(lv_event_t *e)
{
    (void)e;
    if (exclusive_mode) return;
    lilygo_usb_msc_backend_t backend = lilygo_usb_msc_get_backend();
    ensure_backend_ready(backend);
    if (!lilygo_usb_msc_is_active()) {
        lilygo_usb_msc_select_backend(backend);
    }
    update_status(NULL);
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    if (exclusive_mode) {
        return;
    }
    if (status_timer) {
        lv_timer_del(status_timer);
        status_timer = NULL;
    }

    if (lilygo_usb_msc_is_active()) {
        if (!lilygo_usb_msc_set_active(false)) {
            update_status(NULL);
            if (status_timer) {
                status_timer = lv_timer_create(update_status, 1000, NULL);
            }
            return;
        }
    }

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    menu_show();
}

void ui_usb_disk_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "USB Disk", back_event_handler);

    ensure_sd_ready();
    lilygo_usb_msc_select_backend(LILYGO_USB_MSC_BACKEND_SD);

    lv_obj_t *card = ui_create_card(page_container, "Status");
    lv_obj_t *row = ui_create_card_info(card, LV_SYMBOL_USB, "USB", "--");
    usb_label = row_value(row);
    row = ui_create_card_info(card, LV_SYMBOL_SD_CARD, "Storage", "--");
    storage_label = row_value(row);
    row = ui_create_card_info(card, LV_SYMBOL_DIRECTORY, "Capacity", "--");
    capacity_label = row_value(row);
    row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "MSC", "--");
    msc_label = row_value(row);

    card = ui_create_card(page_container, "Control");
    row = ui_create_card_dropdown(card, LV_SYMBOL_SD_CARD, "Storage",
                                  "SD Card\nInternal Flash", 0, backend_dropdown_cb);
    backend_dropdown = row_value(row);
    sync_dropdown_selection();
    row = ui_create_card_switch(card, LV_SYMBOL_USB, "Expose Disk", false, expose_switch_cb);
    msc_switch = row_value(row);
    ui_create_card_button(card, LV_SYMBOL_REFRESH, "Storage", "Refresh", refresh_btn_cb);

    lv_obj_t *note = lv_label_create(card);
    lv_label_set_text(note, "Eject on host before disabling.");
    lv_obj_set_style_text_color(note, UI_COLOR_WARNING, 0);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_12, 0);
    lv_obj_set_width(note, LV_PCT(100));

    update_status(NULL);
    status_timer = lv_timer_create(update_status, 1000, NULL);
}

void ui_usb_disk_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_usb_disk_main = {
    .setup_func_cb = ui_usb_disk_enter,
    .exit_func_cb = ui_usb_disk_exit,
    .user_data = nullptr,
};

#endif
