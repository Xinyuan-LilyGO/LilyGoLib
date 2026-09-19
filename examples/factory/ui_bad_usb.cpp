/**
 * @file      ui_bad_usb.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-29
 * 
 * This app intentionally requires a manual Start action. It supports a small,
 * local-testing DuckyScript subset and ships only a harmless keyboard demo.
 **/
#include "ui_define.h"

#ifdef ARDUINO
#include <SD.h>
#endif

#include <ctype.h>
#include <stdarg.h>
#include <strings.h>

#if !defined(EXCLUDE_BAD_USB)

#define BADUSB_DIR "/badusb"
#define BADUSB_MAX_SCRIPTS 16
#define BADUSB_PATH_LEN 96
#define BADUSB_NAME_LEN 48
#define BADUSB_OPTIONS_LEN 768
#define BADUSB_LINE_LEN 192

typedef struct {
    char name[BADUSB_NAME_LEN];
    char path[BADUSB_PATH_LEN];
} badusb_script_t;

static lv_obj_t *page_container = NULL;
static lv_timer_t *ui_timer = NULL;
static lv_obj_t *script_dropdown = NULL;
static lv_obj_t *state_label = NULL;
static lv_obj_t *line_label = NULL;
static lv_obj_t *script_count_label = NULL;
static lv_obj_t *run_btn = NULL;
static lv_obj_t *stop_btn = NULL;

static badusb_script_t scripts[BADUSB_MAX_SCRIPTS];
static uint8_t script_count = 0;
static char dropdown_options[BADUSB_OPTIONS_LEN];
static char selected_script_path[BADUSB_PATH_LEN];

static TaskHandle_t script_task = NULL;
static volatile bool script_running = false;
static volatile bool script_stop_requested = false;
static volatile uint32_t script_line = 0;
static portMUX_TYPE status_mux = portMUX_INITIALIZER_UNLOCKED;
static char runtime_status[96] = "Ready";

static lv_obj_t *row_value(lv_obj_t *row)
{
    if (!row) return NULL;
    uint32_t count = lv_obj_get_child_count(row);
    if (!count) return NULL;
    return lv_obj_get_child(row, count - 1);
}

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void set_runtime_status(const char *fmt, ...)
{
    char tmp[sizeof(runtime_status)];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);

    portENTER_CRITICAL(&status_mux);
    copy_text(runtime_status, sizeof(runtime_status), tmp);
    portEXIT_CRITICAL(&status_mux);
}

static void get_runtime_status(char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    portENTER_CRITICAL(&status_mux);
    copy_text(out, out_size, runtime_status);
    portEXIT_CRITICAL(&status_mux);
}

static const char *base_name(const char *path)
{
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool has_script_ext(const char *name)
{
    const char *dot = strrchr(name ? name : "", '.');
    if (!dot) return false;
    return strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".duck") == 0 || strcasecmp(dot, ".ducky") == 0;
}

static bool ensure_sd_ready(void)
{
#ifdef ARDUINO
    return hw_is_sd_insert();
#else
    return false;
#endif
}

static void ensure_demo_script(void)
{
#ifdef ARDUINO
    if (!ensure_sd_ready()) return;
    if (!SD.exists(BADUSB_DIR)) {
        SD.mkdir(BADUSB_DIR);
    }

    const char *demo_path = BADUSB_DIR "/demo.txt";
    if (SD.exists(demo_path)) return;

    File f = SD.open(demo_path, FILE_WRITE);
    if (!f) return;
    f.println("REM Safe local keyboard test");
    f.println("DELAY 1000");
    f.println("STRING T-Deck USB HID test");
    f.println("ENTER");
    f.close();
#endif
}

static void build_dropdown_options(void)
{
    dropdown_options[0] = '\0';
    if (script_count == 0) {
        copy_text(dropdown_options, sizeof(dropdown_options), "(no scripts)");
        return;
    }

    size_t used = 0;
    for (uint8_t i = 0; i < script_count; ++i) {
        int written = snprintf(dropdown_options + used, sizeof(dropdown_options) - used,
                               "%s%s", i ? "\n" : "", scripts[i].name);
        if (written < 0 || (size_t)written >= sizeof(dropdown_options) - used) break;
        used += written;
    }
}

static void scan_scripts(void)
{
    script_count = 0;
    dropdown_options[0] = '\0';

#ifdef ARDUINO
    ensure_demo_script();
    if (!ensure_sd_ready()) {
        build_dropdown_options();
        set_runtime_status("SD not ready");
        return;
    }

    File root = SD.open(BADUSB_DIR);
    if (!root || !root.isDirectory()) {
        build_dropdown_options();
        set_runtime_status("/badusb missing");
        return;
    }

    File file = root.openNextFile();
    while (file && script_count < BADUSB_MAX_SCRIPTS) {
        if (!file.isDirectory()) {
            const char *name = base_name(file.name());
            if (has_script_ext(name)) {
                copy_text(scripts[script_count].name, sizeof(scripts[script_count].name), name);
                if (file.name()[0] == '/') {
                    copy_text(scripts[script_count].path, sizeof(scripts[script_count].path), file.name());
                } else {
                    snprintf(scripts[script_count].path, sizeof(scripts[script_count].path), "%s/%s", BADUSB_DIR, name);
                }
                script_count++;
            }
        }
        file = root.openNextFile();
    }
    root.close();
#endif

    build_dropdown_options();
    set_runtime_status(script_count ? "Ready" : "No scripts");
}

static char *trim_ws(char *s)
{
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char) * (end - 1))) {
        *--end = '\0';
    }
    return s;
}

static bool read_script_line(File &file, char *out, size_t out_size)
{
    if (!out || out_size == 0 || !file.available()) return false;

    size_t len = 0;
    while (file.available()) {
        int ch = file.read();
        if (ch < 0) break;
        if (ch == '\r') continue;
        if (ch == '\n') break;
        if (len + 1 < out_size) {
            out[len++] = (char)ch;
        }
    }
    out[len] = '\0';
    return true;
}

static void wait_with_stop(uint32_t ms)
{
    uint32_t start = millis();
    while (!script_stop_requested && millis() - start < ms) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static uint32_t parse_delay_ms(const char *s)
{
    if (!s) return 0;
    long val = atol(s);
    if (val < 0) val = 0;
    if (val > 60000) val = 60000;
    return (uint32_t)val;
}

static bool map_key_token(const char *token, uint8_t *key)
{
    if (!token || !key) return false;

    if (strlen(token) == 1) {
        char c = token[0];
        if (isalpha((unsigned char)c)) {
            *key = (uint8_t)tolower((unsigned char)c);
            return true;
        }
        if (isdigit((unsigned char)c)) {
            *key = (uint8_t)c;
            return true;
        }
    }

    if (strcasecmp(token, "ENTER") == 0 || strcasecmp(token, "RETURN") == 0) {
        *key = LILYGO_USB_KEY_ENTER;
    } else if (strcasecmp(token, "TAB") == 0) {
        *key = LILYGO_USB_KEY_TAB;
    } else if (strcasecmp(token, "ESC") == 0 || strcasecmp(token, "ESCAPE") == 0) {
        *key = LILYGO_USB_KEY_ESC;
    } else if (strcasecmp(token, "SPACE") == 0) {
        *key = ' ';
    } else if (strcasecmp(token, "BACKSPACE") == 0 || strcasecmp(token, "BKSP") == 0) {
        *key = LILYGO_USB_KEY_BACKSPACE;
    } else if (strcasecmp(token, "DELETE") == 0 || strcasecmp(token, "DEL") == 0) {
        *key = LILYGO_USB_KEY_DELETE;
    } else if (strcasecmp(token, "UP") == 0 || strcasecmp(token, "UPARROW") == 0) {
        *key = LILYGO_USB_KEY_UP;
    } else if (strcasecmp(token, "DOWN") == 0 || strcasecmp(token, "DOWNARROW") == 0) {
        *key = LILYGO_USB_KEY_DOWN;
    } else if (strcasecmp(token, "LEFT") == 0 || strcasecmp(token, "LEFTARROW") == 0) {
        *key = LILYGO_USB_KEY_LEFT;
    } else if (strcasecmp(token, "RIGHT") == 0 || strcasecmp(token, "RIGHTARROW") == 0) {
        *key = LILYGO_USB_KEY_RIGHT;
    } else if (strcasecmp(token, "HOME") == 0) {
        *key = LILYGO_USB_KEY_HOME;
    } else if (strcasecmp(token, "END") == 0) {
        *key = LILYGO_USB_KEY_END;
    } else if (strcasecmp(token, "PAGEUP") == 0 || strcasecmp(token, "PAGE_UP") == 0) {
        *key = LILYGO_USB_KEY_PAGE_UP;
    } else if (strcasecmp(token, "PAGEDOWN") == 0 || strcasecmp(token, "PAGE_DOWN") == 0) {
        *key = LILYGO_USB_KEY_PAGE_DOWN;
    } else if (strcasecmp(token, "INSERT") == 0 || strcasecmp(token, "INS") == 0) {
        *key = LILYGO_USB_KEY_INSERT;
    } else if (token[0] == 'F' || token[0] == 'f') {
        int fnum = atoi(token + 1);
        if (fnum >= 1 && fnum <= 12) {
            *key = LILYGO_USB_KEY_F1 + (uint8_t)(fnum - 1);
        } else {
            return false;
        }
    } else {
        return false;
    }

    return true;
}

static bool execute_combo(char *line)
{
    uint8_t modifiers = 0;
    uint8_t key = LILYGO_USB_KEY_NONE;
    char *save = NULL;
    char *token = strtok_r(line, " \t+-", &save);

    while (token) {
        if (strcasecmp(token, "CTRL") == 0 || strcasecmp(token, "CONTROL") == 0) {
            modifiers |= LILYGO_USB_MOD_CTRL;
        } else if (strcasecmp(token, "SHIFT") == 0) {
            modifiers |= LILYGO_USB_MOD_SHIFT;
        } else if (strcasecmp(token, "ALT") == 0) {
            modifiers |= LILYGO_USB_MOD_ALT;
        } else if (strcasecmp(token, "GUI") == 0 || strcasecmp(token, "WINDOWS") == 0 ||
                   strcasecmp(token, "WIN") == 0 || strcasecmp(token, "COMMAND") == 0) {
            modifiers |= LILYGO_USB_MOD_GUI;
        } else if (!map_key_token(token, &key)) {
            set_runtime_status("Unsupported: %s", token);
            return false;
        }
        token = strtok_r(NULL, " \t+-", &save);
    }

    if (key == LILYGO_USB_KEY_NONE) {
        set_runtime_status("Missing key");
        return false;
    }

    if (modifiers) {
        return lilygo_usb_hid_keyboard_combo(modifiers, key);
    }
    return lilygo_usb_hid_keyboard_tap_key(key);
}

static bool execute_line(char *line, uint32_t *default_delay)
{
    char *cmd = trim_ws(line);
    if (!cmd[0] || cmd[0] == '#') return true;
    if (strncasecmp(cmd, "REM", 3) == 0 && (cmd[3] == '\0' || isspace((unsigned char)cmd[3]))) {
        return true;
    }

    char raw[BADUSB_LINE_LEN];
    copy_text(raw, sizeof(raw), cmd);

    char *arg = cmd;
    while (*arg && !isspace((unsigned char)*arg)) arg++;
    if (*arg) {
        *arg++ = '\0';
        arg = trim_ws(arg);
    }

    if (strcasecmp(cmd, "STRING") == 0) {
        return lilygo_usb_hid_keyboard_send_text(arg, 5);
    }
    if (strcasecmp(cmd, "DELAY") == 0) {
        wait_with_stop(parse_delay_ms(arg));
        return true;
    }
    if (strcasecmp(cmd, "DEFAULT_DELAY") == 0 || strcasecmp(cmd, "DEFAULTDELAY") == 0) {
        *default_delay = parse_delay_ms(arg);
        if (*default_delay > 5000) *default_delay = 5000;
        return true;
    }

    return execute_combo(raw);
}

static void badusb_task(void *param)
{
    (void)param;
    script_running = true;
    script_stop_requested = false;
    script_line = 0;

    if (!lilygo_usb_hid_keyboard_available()) {
        set_runtime_status("USB HID unavailable");
        script_running = false;
        script_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    set_runtime_status("Armed 2s");
    wait_with_stop(2000);

#ifdef ARDUINO
    File file = SD.open(selected_script_path, FILE_READ);
    if (!file) {
        set_runtime_status("Open failed");
        script_running = false;
        script_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    uint32_t default_delay = 0;
    char line[BADUSB_LINE_LEN];
    while (!script_stop_requested && read_script_line(file, line, sizeof(line))) {
        script_line = script_line + 1;
        set_runtime_status("Line %lu", (unsigned long)script_line);
        execute_line(line, &default_delay);
        if (default_delay && !script_stop_requested) {
            wait_with_stop(default_delay);
        }
    }
    file.close();
#endif

    lilygo_usb_hid_keyboard_release_all();
    set_runtime_status(script_stop_requested ? "Stopped" : "Done");
    script_running = false;
    script_task = NULL;
    vTaskDelete(NULL);
}

static void update_ui(lv_timer_t *timer)
{
    (void)timer;

    if (script_count_label) {
        lv_label_set_text_fmt(script_count_label, "%u", script_count);
    }

    if (line_label) {
        lv_label_set_text_fmt(line_label, "%lu", (unsigned long)script_line);
    }

    if (state_label) {
        char buf[96];
        get_runtime_status(buf, sizeof(buf));
        lv_label_set_text(state_label, buf);
    }

    if (run_btn) {
        if (script_running || script_count == 0) {
            lv_obj_add_state(run_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(run_btn, LV_STATE_DISABLED);
        }
    }
    if (stop_btn) {
        if (script_running) {
            lv_obj_clear_state(stop_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(stop_btn, LV_STATE_DISABLED);
        }
    }
}

static void script_select_cb(lv_event_t *e)
{
    (void)e;
    if (!script_dropdown || script_count == 0) return;
    uint16_t sel = lv_dropdown_get_selected(script_dropdown);
    if (sel < script_count) {
        copy_text(selected_script_path, sizeof(selected_script_path), scripts[sel].path);
        set_runtime_status("%s", scripts[sel].name);
    }
}

static void refresh_btn_cb(lv_event_t *e)
{
    (void)e;
    scan_scripts();
    if (script_dropdown) {
        lv_dropdown_set_options(script_dropdown, dropdown_options);
        lv_dropdown_set_selected(script_dropdown, 0);
    }
    if (script_count) {
        copy_text(selected_script_path, sizeof(selected_script_path), scripts[0].path);
    } else {
        selected_script_path[0] = '\0';
    }
    update_ui(NULL);
}

static void run_btn_cb(lv_event_t *e)
{
    (void)e;
    if (script_running || script_count == 0) return;

    uint16_t sel = script_dropdown ? lv_dropdown_get_selected(script_dropdown) : 0;
    if (sel >= script_count) return;

    copy_text(selected_script_path, sizeof(selected_script_path), scripts[sel].path);
    script_stop_requested = false;
    BaseType_t ok = xTaskCreate(badusb_task, "badusb", 6144, NULL, 1, &script_task);
    if (ok != pdPASS) {
        script_task = NULL;
        script_running = false;
        set_runtime_status("Task failed");
    }
    update_ui(NULL);
}

static void stop_btn_cb(lv_event_t *e)
{
    (void)e;
    script_stop_requested = true;
    lilygo_usb_hid_keyboard_release_all();
    set_runtime_status("Stopping");
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    script_stop_requested = true;
    lilygo_usb_hid_keyboard_release_all();
    for (uint8_t i = 0; script_running && i < 50; ++i) {
        delay(10);
    }

    if (ui_timer) {
        lv_timer_del(ui_timer);
        ui_timer = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    menu_show();
}

void ui_bad_usb_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "BadUSB", back_event_handler);

    lilygo_usb_service_begin();
    scan_scripts();
    if (script_count) {
        copy_text(selected_script_path, sizeof(selected_script_path), scripts[0].path);
    } else {
        selected_script_path[0] = '\0';
    }

    lv_obj_t *card = ui_create_card(page_container, "Status");
    lv_obj_t *row = ui_create_card_info(card, LV_SYMBOL_USB, "HID",
                                        lilygo_usb_hid_keyboard_available() ? "Ready" : "Unavailable");
    (void)row;
    row = ui_create_card_info(card, LV_SYMBOL_LIST, "Scripts", "0");
    script_count_label = row_value(row);
    row = ui_create_card_info(card, LV_SYMBOL_EDIT, "Line", "0");
    line_label = row_value(row);
    row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "State", "Ready");
    state_label = row_value(row);

    card = ui_create_card(page_container, "Script");
    script_dropdown = lv_dropdown_create(lv_obj_create(card));
    lv_dropdown_set_options(script_dropdown, dropdown_options);
    lv_dropdown_set_selected(script_dropdown, 0);
    lv_obj_set_width(script_dropdown, 132);
    lv_obj_set_height(script_dropdown, 32);
    lv_obj_add_event_cb(script_dropdown, script_select_cb, LV_EVENT_VALUE_CHANGED, NULL);
    ui_create_card_item(card, LV_SYMBOL_FILE, "File", script_dropdown);
    ui_create_card_button(card, LV_SYMBOL_REFRESH, "List", "Refresh", refresh_btn_cb);

    lv_obj_t *note = lv_label_create(card);
    lv_label_set_text(note, "Manual start only. Use on your own devices.");
    lv_obj_set_style_text_color(note, UI_COLOR_WARNING, 0);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_12, 0);
    lv_obj_set_width(note, LV_PCT(100));

    card = ui_create_card(page_container, "Control");
    row = ui_create_card_button(card, LV_SYMBOL_PLAY, "Arm", "Start", run_btn_cb);
    run_btn = row_value(row);
    row = ui_create_card_button(card, LV_SYMBOL_STOP, "Run", "Stop", stop_btn_cb);
    stop_btn = row_value(row);
    lv_obj_add_state(stop_btn, LV_STATE_DISABLED);

    update_ui(NULL);
    ui_timer = lv_timer_create(update_ui, 300, NULL);
}

void ui_bad_usb_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_bad_usb_main = {
    .setup_func_cb = ui_bad_usb_enter,
    .exit_func_cb = ui_bad_usb_exit,
    .user_data = nullptr,
};

#endif
