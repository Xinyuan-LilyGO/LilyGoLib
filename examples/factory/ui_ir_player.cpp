/**
 * @file      ui_ir_player.cpp
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 *
 * IR Player — List IR files from SD card and send them
 */
#include "ui_define.h"

#if !defined(EXCLUDE_IR_PLAYER)

#include <IRsend.h>
#include <SD.h>

extern IRsend irsend;

static lv_obj_t *page_container = NULL;
static lv_obj_t *file_list_card = NULL;
static lv_obj_t *status_lbl = NULL;

/* IR file header */
typedef struct {
    char magic[4];        // "IRAW"
    uint16_t version;     // 1
    uint16_t freq;        // Hz
    uint16_t raw_len;     // number of uint16_t
    uint16_t proto;       // decode_type_t
    uint64_t value;       // decoded value
} ir_file_header_t;

#define MAX_FILES 20
static constexpr uint16_t IR_RAW_MAX_SAMPLES = 2048;
static lv_obj_t *file_rows[MAX_FILES] = {NULL};
static int file_count = 0;

static void set_status(const char *text, lv_color_t color)
{
    if (!status_lbl) return;
    lv_label_set_text(status_lbl, text);
    lv_obj_set_style_text_color(status_lbl, color, 0);
}

static const char *ir_file_basename(const char *path)
{
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool make_ir_path(char *out, size_t out_size, const char *name)
{
    if (!out || out_size == 0 || !name || !name[0]) return false;
    int written = 0;
    if (name[0] == '/') {
        written = snprintf(out, out_size, "%s", name);
    } else {
        written = snprintf(out, out_size, "/ir/%s", name);
    }
    return written > 0 && written < (int)out_size;
}

static char *copy_path(const char *path)
{
    if (!path) return NULL;
    size_t len = strlen(path) + 1;
    char *copy = (char *)malloc(len);
    if (copy) memcpy(copy, path, len);
    return copy;
}

static const char *get_row_path(lv_event_t *e)
{
    lv_obj_t *target = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *row = target ? lv_obj_get_parent(target) : NULL;
    return row ? (const char *)lv_obj_get_user_data(row) : NULL;
}

static void row_delete_cb(lv_event_t *e)
{
    lv_obj_t *row = (lv_obj_t *)lv_event_get_target(e);
    char *path = row ? (char *)lv_obj_get_user_data(row) : NULL;
    if (path) {
        free(path);
        lv_obj_set_user_data(row, NULL);
    }
}

/* ── Send IR file ── */
static void send_ir_file(const char *path)
{
#if defined(ARDUINO)
    File f = SD.open(path, FILE_READ);
    if (!f) {
        set_status("Failed to open file!", lv_color_hex(0xFF4444));
        return;
    }

    /* Read header */
    ir_file_header_t header;
    if (f.read((uint8_t *)&header, sizeof(header)) != sizeof(header)) {
        f.close();
        set_status("Invalid file format!", lv_color_hex(0xFF4444));
        return;
    }

    /* Validate magic */
    if (memcmp(header.magic, "IRAW", 4) != 0) {
        f.close();
        set_status("Not an IR file!", lv_color_hex(0xFF4444));
        return;
    }

    if (header.version != 1 || header.raw_len == 0 || header.raw_len > IR_RAW_MAX_SAMPLES) {
        f.close();
        set_status("Unsupported IR file!", lv_color_hex(0xFF4444));
        return;
    }

    /* Read raw data */
    uint16_t *rawbuf = (uint16_t *)malloc(header.raw_len * sizeof(uint16_t));
    if (!rawbuf) {
        f.close();
        set_status("Out of memory!", lv_color_hex(0xFF4444));
        return;
    }

    if (f.read((uint8_t *)rawbuf, header.raw_len * sizeof(uint16_t)) != header.raw_len * sizeof(uint16_t)) {
        free(rawbuf);
        f.close();
        set_status("Failed to read data!", lv_color_hex(0xFF4444));
        return;
    }
    f.close();

    /* Switch to send mode and transmit */
    hw_ir_function_select(true);
    irsend.sendRaw(rawbuf, header.raw_len, header.freq);
    hw_ir_function_select(false);

    free(rawbuf);

    if (status_lbl) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Sent: %s (%u samples)", ir_file_basename(path), header.raw_len);
        lv_label_set_text(status_lbl, buf);
        lv_obj_set_style_text_color(status_lbl, UI_COLOR_ACCENT, 0);
    }
#endif
}

/* Forward declaration */
static void build_file_list(void);

/* ── Send button callback ── */
static void send_btn_cb(lv_event_t *e)
{
    const char *path = get_row_path(e);
    if (path) {
        send_ir_file(path);
    }
}

/* ── Delete button callback ── */
static void del_btn_cb(lv_event_t *e)
{
    const char *path = get_row_path(e);
    if (!path) return;

    char path_copy[96];
    if (snprintf(path_copy, sizeof(path_copy), "%s", path) >= (int)sizeof(path_copy)) {
        set_status("File path too long!", lv_color_hex(0xFF4444));
        return;
    }

    if (SD.remove(path_copy)) {
        if (status_lbl) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Deleted: %s", ir_file_basename(path_copy));
            lv_label_set_text(status_lbl, buf);
            lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF4444), 0);
        }
        /* Refresh file list */
        build_file_list();
    } else {
        set_status("Delete failed!", lv_color_hex(0xFF4444));
    }
}

/* ── Build file list ── */
static void build_file_list(void)
{
    /* Clear old rows */
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_rows[i]) {
            lv_obj_delete(file_rows[i]);
            file_rows[i] = NULL;
        }
    }
    file_count = 0;

    if (!file_list_card) return;

    /* Check if SD card is available */
    if (hw_get_sd_size() <= 0) {
        lv_obj_t *lbl = lv_label_create(file_list_card);
        lv_label_set_text(lbl, "No SD card detected");
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
        file_rows[0] = lbl;
        file_count = 1;
        return;
    }

    /* Scan /ir directory */
    File root = SD.open("/ir");
    if (!root || !root.isDirectory()) {
        lv_obj_t *lbl = lv_label_create(file_list_card);
        lv_label_set_text(lbl, "No /ir directory found");
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
        file_rows[0] = lbl;
        file_count = 1;
        if (root) root.close();
        return;
    }

    File file = root.openNextFile();
    while (file && file_count < MAX_FILES) {
        if (!file.isDirectory()) {
            const char *name = file.name();
            /* Only show .ir files */
            int nlen = strlen(name);
            if (nlen > 3 && strcmp(name + nlen - 3, ".ir") == 0) {
                /* Build full path */
                char path_buf[96];
                char *full_path = make_ir_path(path_buf, sizeof(path_buf), name) ? copy_path(path_buf) : NULL;
                if (full_path) {
                    /* Create row */
                    lv_obj_t *row = lv_obj_create(file_list_card);
                    lv_obj_set_user_data(row, full_path);
                    lv_obj_add_event_cb(row, row_delete_cb, LV_EVENT_DELETE, NULL);
                    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
                    lv_obj_set_style_border_width(row, 0, 0);
                    lv_obj_set_style_radius(row, 0, 0);
                    lv_obj_set_style_pad_top(row, 4, 0);
                    lv_obj_set_style_pad_bottom(row, 4, 0);
                    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                    lv_obj_set_style_pad_column(row, 8, 0);

                    /* File icon */
                    lv_obj_t *icon = lv_label_create(row);
                    lv_label_set_text(icon, LV_SYMBOL_FILE);
                    lv_obj_set_style_text_color(icon, UI_COLOR_ACCENT, 0);

                    /* File name */
                    lv_obj_t *name_lbl = lv_label_create(row);
                    lv_label_set_text(name_lbl, ir_file_basename(full_path));
                    lv_obj_set_style_text_color(name_lbl, UI_COLOR_TEXT_PRIMARY, 0);
                    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0);
                    lv_obj_set_flex_grow(name_lbl, 1);
                    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
                    lv_obj_set_width(name_lbl, LV_SIZE_CONTENT);

                    /* File size */
                    char size_buf[16];
                    int fsize = file.size();
                    if (fsize > 1024) {
                        snprintf(size_buf, sizeof(size_buf), "%dKB", fsize / 1024);
                    } else {
                        snprintf(size_buf, sizeof(size_buf), "%dB", fsize);
                    }
                    lv_obj_t *size_lbl = lv_label_create(row);
                    lv_label_set_text(size_lbl, size_buf);
                    lv_obj_set_style_text_color(size_lbl, UI_COLOR_TEXT_SECONDARY, 0);
                    lv_obj_set_style_text_font(size_lbl, &lv_font_montserrat_12, 0);

                    /* Send button */
                    lv_obj_t *send_btn = lv_btn_create(row);
                    lv_obj_set_size(send_btn, LV_SIZE_CONTENT, 28);
                    lv_obj_set_style_bg_color(send_btn, UI_COLOR_ACCENT, 0);
                    lv_obj_set_style_bg_opa(send_btn, LV_OPA_COVER, 0);
                    lv_obj_set_style_radius(send_btn, 6, 0);
                    lv_obj_set_style_border_width(send_btn, 0, 0);
                    ui_add_accent_focus_style(send_btn);
                    lv_obj_set_style_pad_hor(send_btn, 8, 0);
                    lv_obj_add_event_cb(send_btn, send_btn_cb, LV_EVENT_CLICKED, NULL);
                    {
                        lv_obj_t *l = lv_label_create(send_btn);
                        lv_label_set_text(l, LV_SYMBOL_PLAY);
                        lv_obj_center(l);
                        lv_obj_set_style_text_color(l, lv_color_white(), 0);
                    }

                    /* Delete button */
                    lv_obj_t *del_btn = lv_btn_create(row);
                    lv_obj_set_size(del_btn, LV_SIZE_CONTENT, 28);
                    lv_obj_set_style_bg_color(del_btn, lv_color_hex(0x444444), 0);
                    lv_obj_set_style_bg_opa(del_btn, LV_OPA_COVER, 0);
                    lv_obj_set_style_radius(del_btn, 6, 0);
                    lv_obj_set_style_border_width(del_btn, 1, 0);
                    lv_obj_set_style_border_color(del_btn, lv_color_hex(0xFF4444), 0);
                    lv_obj_set_style_pad_hor(del_btn, 8, 0);
                    lv_obj_add_event_cb(del_btn, del_btn_cb, LV_EVENT_CLICKED, NULL);
                    {
                        lv_obj_t *l = lv_label_create(del_btn);
                        lv_label_set_text(l, LV_SYMBOL_TRASH);
                        lv_obj_center(l);
                        lv_obj_set_style_text_color(l, lv_color_hex(0xFF4444), 0);
                    }

                    file_rows[file_count] = row;
                    file_count++;
                }
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();

    if (file_count == 0) {
        lv_obj_t *lbl = lv_label_create(file_list_card);
        lv_label_set_text(lbl, "No .ir files in /ir/");
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
        file_rows[0] = lbl;
        file_count = 1;
    }
}

/* ── Refresh button ── */
static void refresh_btn_cb(lv_event_t *e)
{
    build_file_list();
    if (status_lbl) {
        lv_label_set_text(status_lbl, "File list refreshed.");
        lv_obj_set_style_text_color(status_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    }
}

/* ── Back handler ── */
static void back_event_handler(lv_event_t *e)
{
    /* Clear file rows */
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_rows[i]) {
            lv_obj_delete(file_rows[i]);
            file_rows[i] = NULL;
        }
    }
    file_count = 0;

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }

    file_list_card = NULL;
    status_lbl = NULL;

    menu_show();
}

/* ── Enter ── */
void ui_ir_player_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "IR Player", back_event_handler);

    /* Ensure SD card is mounted */
    if (!hw_is_sd_insert()) {
        return;
    }

    /* ── Status card ── */
    lv_obj_t *status_card = ui_create_card(page_container, "Status");
    status_lbl = lv_label_create(status_card);
    lv_label_set_text(status_lbl, "Select a file to send.");
    lv_obj_set_style_text_color(status_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_12, 0);

    /* ── File list card ── */
    file_list_card = ui_create_card(page_container, "IR Files (/ir/)");

    /* Build initial file list */
    build_file_list();

    /* ── Refresh button ── */
    lv_obj_t *btn_card = ui_create_card(page_container, NULL);
    lv_obj_t *refresh_btn = lv_btn_create(btn_card);
    lv_obj_set_size(refresh_btn, LV_PCT(60), 36);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_bg_opa(refresh_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(refresh_btn, 8, 0);
    lv_obj_set_style_border_width(refresh_btn, 1, 0);
    lv_obj_set_style_border_color(refresh_btn, UI_COLOR_DIVIDER, 0);
    lv_obj_add_event_cb(refresh_btn, refresh_btn_cb, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *l = lv_label_create(refresh_btn);
        lv_label_set_text(l, LV_SYMBOL_REFRESH " Refresh");
        lv_obj_center(l);
        lv_obj_set_style_text_color(l, UI_COLOR_TEXT_PRIMARY, 0);
    }
}

/* ── Exit ── */
void ui_ir_player_exit(lv_obj_t *parent)
{
}

app_t ui_ir_player_main = {
    .setup_func_cb = ui_ir_player_enter,
    .exit_func_cb = ui_ir_player_exit,
    .user_data = nullptr,
};

#endif /* EXCLUDE_IR_PLAYER */
