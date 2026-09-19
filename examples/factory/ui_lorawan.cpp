/**
 * @file      ui_lorawan.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-04
 * 
 * SX1262/LR1121 LoRaWAN OTAA app with SD-card configuration.
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#if !defined(EXCLUDE_LORAWAN)

#include <RadioLib.h>
#include <Preferences.h>
#include <SD.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <strings.h>

#define LORAWAN_CONFIG_DIR        "/lorawan"

#if defined(ARDUINO_LILYGO_LORA_SX1262)
#define LORAWAN_RADIO_NAME        "SX1262"
#define LORAWAN_CONFIG_PATH       "/lorawan/sx1262_otaa.cfg"
#define LORAWAN_PREF_NAMESPACE    "lwsx1262"
#else
#define LORAWAN_RADIO_NAME        "LR1121"
#define LORAWAN_CONFIG_PATH       "/lorawan/lr1121_otaa.cfg"
#define LORAWAN_PREF_NAMESPACE    "lwlr1121"
#endif

#define LORAWAN_PREF_NONCES_KEY   "nonces"
#define LORAWAN_PAYLOAD_VERSION   1
#define LORAWAN_ERR_CONFIG_INVALID (-32000)
#define LORAWAN_ERR_NO_MEMORY      (-32001)
#define LORAWAN_ERR_SPI_LOCK       (-32002)

typedef struct {
    const char *name;
    const LoRaWANBand_t *band;
    float rf_freq_mhz;
} lorawan_region_entry_t;

static const lorawan_region_entry_t region_table[] = {
    {"EU868",   &EU868,   868.1f},
    {"US915",   &US915,   915.0f},
    {"AU915",   &AU915,   915.0f},
    {"EU433",   &EU433,   433.0f},
    {"CN470",   &CN470,   470.0f},
    {"AS923",   &AS923,   923.2f},
    {"AS923_2", &AS923_2, 923.2f},
    {"AS923_3", &AS923_3, 923.2f},
    {"AS923_4", &AS923_4, 923.2f},
    {"KR920",   &KR920,   920.0f},
    {"IN865",   &IN865,   865.0f},
};

static const uint32_t interval_seconds_list[] = {60, 300, 600};
static const char *interval_options = "1 min\n5 min\n10 min\nCustom";
#define LORAWAN_CUSTOM_INTERVAL_INDEX 3

typedef struct {
    const lorawan_region_entry_t *region;
    uint8_t sub_band;
    uint64_t join_eui;
    uint64_t dev_eui;
    uint8_t app_key[16];
    uint8_t nwk_key[16];
    bool has_dev_eui;
    bool has_app_key;
    bool has_nwk_key;
    uint32_t uplink_interval_s;
    uint8_t fport;
    bool confirmed;
    bool adr;
    uint8_t datarate;
    bool duty_cycle;
    uint32_t duty_cycle_ms_per_hour;
    bool dwell_time;
    uint32_t dwell_time_ms;
    bool has_tx_power;
    int8_t tx_power;
} lorawan_config_t;

typedef struct {
    bool bme_valid;
    bool bme_mock;
    float bme_temp;
    float bme_humi;
    float bme_press;
    float bme_alt;

    bool gps_location_valid;
    bool gps_datetime_valid;
    bool gps_speed_valid;
    bool gps_altitude_valid;
    bool gps_mock;
    double gps_lat;
    double gps_lng;
    double gps_speed;
    double gps_altitude;
    uint16_t gps_satellites;
    struct tm gps_datetime;

    bool battery_valid;
    int battery_percent;
} lorawan_sample_t;

typedef struct {
    lv_obj_t *status;
    lv_obj_t *config;
    lv_obj_t *region;
    lv_obj_t *dev_eui;
    lv_obj_t *interval;
    lv_obj_t *join_state;
    lv_obj_t *dev_addr;
    lv_obj_t *fcnt;
    lv_obj_t *next;
    lv_obj_t *bme;
    lv_obj_t *gps;
    lv_obj_t *payload;
    lv_obj_t *last;
    lv_obj_t *downlink;
    lv_obj_t *radio;
} lorawan_labels_t;

typedef enum {
    LORAWAN_JOIN_NOT_JOINED,
    LORAWAN_JOIN_JOINING,
    LORAWAN_JOIN_JOINED,
    LORAWAN_JOIN_FAILED,
    LORAWAN_JOIN_CONFIG_INVALID,
} lorawan_join_view_t;

RTC_DATA_ATTR static uint8_t lorawan_session[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];

static lv_obj_t *page_container = NULL;
static lv_obj_t *interval_dropdown = NULL;
static lv_obj_t *auto_switch = NULL;
static lv_obj_t *reload_button = NULL;
static lv_obj_t *join_button = NULL;
static lv_obj_t *send_button = NULL;
static lv_obj_t *reset_button = NULL;
static lv_obj_t *radio_unavailable_msgbox = NULL;
static lv_timer_t *lorawan_timer = NULL;
static lorawan_labels_t labels;
static lorawan_config_t config;
static lorawan_sample_t last_sample;
static lorawan_join_view_t join_view = LORAWAN_JOIN_NOT_JOINED;
static bool config_loaded = false;
static bool config_valid = false;
static bool joined = false;
static bool auto_uplink = false;
static uint32_t next_uplink_ms = 0;
static uint32_t next_sample_ms = 0;
static LoRaWANNode *node = NULL;
static Preferences store;
static bool store_open = false;
static volatile bool join_task_running = false;
static volatile bool join_task_done = false;
static volatile int16_t join_task_result = RADIOLIB_ERR_NONE;
static bool join_task_from_auto = false;
#ifdef ARDUINO
static TaskHandle_t join_task_handle = NULL;
#endif

static void update_all_labels(void);
static void update_timer_labels(void);
static void cleanup(void);
static bool do_join(void);
static bool do_send(bool manual);
static bool start_join_async(bool from_auto);

static bool join_in_progress(void)
{
    return join_task_running && !join_task_done;
}

static bool join_result_pending(void)
{
    return join_task_running && join_task_done;
}

static lv_obj_t *row_widget(lv_obj_t *row)
{
    if (!row) {
        return NULL;
    }
    uint32_t child_count = lv_obj_get_child_count(row);
    return child_count ? lv_obj_get_child(row, child_count - 1) : NULL;
}

static void set_obj_disabled(lv_obj_t *obj, bool disabled)
{
    if (!obj) {
        return;
    }
    if (disabled) {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(obj, LV_STATE_DISABLED);
    }
}

static void set_button_text(lv_obj_t *button, const char *text)
{
    if (!button) {
        return;
    }
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label) {
        lv_label_set_text(label, text ? text : "");
        lv_obj_center(label);
    }
}

static void disable_auto_uplink(void)
{
    auto_uplink = false;
    if (auto_switch) {
        lv_obj_remove_state(auto_switch, LV_STATE_CHECKED);
    }
}

static void set_controls_busy(bool busy)
{
    bool session_ready = joined && node;
    set_obj_disabled(interval_dropdown, busy || !session_ready);
    set_obj_disabled(auto_switch, busy || !session_ready);
    set_obj_disabled(send_button, busy || !session_ready);
    set_obj_disabled(reload_button, busy);
    set_obj_disabled(join_button, busy);
    set_obj_disabled(reset_button, busy);
    set_button_text(join_button, busy ? "Joining..." : "Join");

    if (!busy && !session_ready) {
        disable_auto_uplink();
    }
}

static lv_obj_t *add_info_row(lv_obj_t *card, const char *icon, const char *title,
                              const char *value, lv_obj_t **out_label)
{
    lv_obj_t *row = ui_create_card_info(card, icon, title, value);
    if (out_label) {
        *out_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
        lv_label_set_long_mode(*out_label, LV_LABEL_LONG_DOT);
    }
    return row;
}

static void set_label(lv_obj_t *label, const char *text)
{
    if (label) {
        lv_label_set_text(label, text ? text : "--");
    }
}

static void set_status(const char *fmt, ...)
{
    char buf[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    set_label(labels.status, buf);
    LILYGO_LOG_PRINTF("LORAWAN: %s\n", buf);
}

static const lorawan_region_entry_t *find_region(const char *name)
{
    if (!name || !name[0]) {
        return &region_table[0];
    }
    for (size_t i = 0; i < sizeof(region_table) / sizeof(region_table[0]); ++i) {
        if (strcasecmp(name, region_table[i].name) == 0) {
            return &region_table[i];
        }
    }
    return NULL;
}

static uint8_t interval_index_from_seconds(uint32_t seconds)
{
    for (size_t i = 0; i < sizeof(interval_seconds_list) / sizeof(interval_seconds_list[0]); ++i) {
        if (seconds == interval_seconds_list[i]) {
            return (uint8_t)i;
        }
    }
    return LORAWAN_CUSTOM_INTERVAL_INDEX;
}

static uint32_t normalize_interval(uint32_t seconds)
{
    return seconds < 60 ? 60 : seconds;
}

static void config_defaults(lorawan_config_t &cfg)
{
    memset(&cfg, 0, sizeof(cfg));
    cfg.region = &region_table[0];
    cfg.sub_band = 0;
    cfg.join_eui = 0;
    cfg.uplink_interval_s = 300;
    cfg.fport = 10;
    cfg.confirmed = false;
    cfg.adr = true;
    cfg.datarate = 5;
    cfg.duty_cycle = true;
    cfg.duty_cycle_ms_per_hour = 1250;
    cfg.dwell_time = true;
    cfg.dwell_time_ms = 400;
    cfg.has_tx_power = false;
    cfg.tx_power = 14;
}

static char *trim(char *text)
{
    while (*text && isspace((unsigned char) * text)) {
        ++text;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char) * (end - 1))) {
        --end;
    }
    *end = '\0';
    return text;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool parse_hex_bytes(const char *text, uint8_t *out, size_t max_len, size_t *written)
{
    size_t count = 0;
    int high = -1;
    for (size_t i = 0; text && text[i]; ++i) {
        if (text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            ++i;
            continue;
        }
        int value = hex_value(text[i]);
        if (value < 0) {
            continue;
        }
        if (high < 0) {
            high = value;
        } else {
            if (count >= max_len) {
                return false;
            }
            out[count++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
    }
    if (high >= 0) {
        return false;
    }
    if (written) {
        *written = count;
    }
    return true;
}

static bool parse_eui(const char *text, uint64_t *value)
{
    uint8_t bytes[8];
    size_t len = 0;
    if (!parse_hex_bytes(text, bytes, sizeof(bytes), &len) || len != sizeof(bytes)) {
        return false;
    }
    uint64_t out = 0;
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        out = (out << 8) | bytes[i];
    }
    *value = out;
    return true;
}

static bool parse_key(const char *text, uint8_t key[16])
{
    size_t len = 0;
    return parse_hex_bytes(text, key, 16, &len) && len == 16;
}

static bool parse_bool_value(const char *text, bool *value)
{
    if (!text) {
        return false;
    }
    if (strcasecmp(text, "1") == 0 || strcasecmp(text, "true") == 0 ||
            strcasecmp(text, "yes") == 0 || strcasecmp(text, "on") == 0) {
        *value = true;
        return true;
    }
    if (strcasecmp(text, "0") == 0 || strcasecmp(text, "false") == 0 ||
            strcasecmp(text, "no") == 0 || strcasecmp(text, "off") == 0) {
        *value = false;
        return true;
    }
    return false;
}

static bool parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 0);
    if (end == text) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_i32(const char *text, int32_t *value)
{
    char *end = NULL;
    long parsed = strtol(text, &end, 0);
    if (end == text) {
        return false;
    }
    *value = (int32_t)parsed;
    return true;
}

static void format_eui(uint64_t value, char *out, size_t out_size)
{
    snprintf(out, out_size, "%08lX%08lX",
             (unsigned long)(value >> 32),
             (unsigned long)(value & 0xFFFFFFFFUL));
}

static void format_interval(uint32_t seconds, char *out, size_t out_size)
{
    if (seconds >= 60 && (seconds % 60) == 0) {
        snprintf(out, out_size, "%lu min", (unsigned long)(seconds / 60));
    } else {
        snprintf(out, out_size, "%lu s", (unsigned long)seconds);
    }
}

static bool write_template_config(void)
{
    if (!SD.exists(LORAWAN_CONFIG_DIR) && !SD.mkdir(LORAWAN_CONFIG_DIR)) {
        return false;
    }
    File file = SD.open(LORAWAN_CONFIG_PATH, FILE_WRITE);
    if (!file) {
        return false;
    }
    file.print("# ");
    file.print(LORAWAN_RADIO_NAME);
    file.println(" LoRaWAN OTAA config");
    file.println("# Edit these values, then reopen the app or press Reload.");
    file.println("# Supported regions: EU868,US915,AU915,EU433,CN470,AS923,AS923_2,AS923_3,AS923_4,KR920,IN865");
    file.println("region=EU868");
    file.println("# For US915/AU915 commonly use sub_band=2. Other regions usually use 0.");
    file.println("sub_band=0");
    file.println("join_eui=0000000000000000");
    file.println("dev_eui=0011223344556677");
    file.println("app_key=00112233445566778899AABBCCDDEEFF");
    file.println("nwk_key=00112233445566778899AABBCCDDEEFF");
    file.println("# UI presets are 60, 300, or 600 seconds; other values show as Custom.");
    file.println("uplink_interval_s=300");
    file.println("fport=10");
    file.println("confirmed=false");
    file.println("adr=true");
    file.println("datarate=5");
    file.println("duty_cycle=true");
    file.println("duty_cycle_ms_per_hour=1250");
    file.println("dwell_time=true");
    file.println("dwell_time_ms=400");
    file.println("# Optional: tx_power=14");
    file.close();
    return true;
}

static bool ensure_config_storage(bool *created)
{
    if (created) {
        *created = false;
    }
    if (!hw_is_sd_insert()) {
        return false;
    }
    if (!SD.exists(LORAWAN_CONFIG_DIR) && !SD.mkdir(LORAWAN_CONFIG_DIR)) {
        return false;
    }
    if (!SD.exists(LORAWAN_CONFIG_PATH)) {
        bool ok = write_template_config();
        if (created) {
            *created = ok;
        }
        return false;
    }
    return true;
}

static bool load_config_file(bool *created)
{
    config_defaults(config);
    config_loaded = false;
    config_valid = false;

    if (!ensure_config_storage(created)) {
        return false;
    }

    File file = SD.open(LORAWAN_CONFIG_PATH, FILE_READ);
    if (!file) {
        return false;
    }

    char line[192];
    while (file.available()) {
        size_t len = file.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';
        char *work = trim(line);
        if (!work[0] || work[0] == '#' || work[0] == ';') {
            continue;
        }
        char *comment = strchr(work, '#');
        if (comment) {
            *comment = '\0';
        }
        char *eq = strchr(work, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char *key = trim(work);
        char *value = trim(eq + 1);

        if (strcasecmp(key, "region") == 0) {
            const lorawan_region_entry_t *region = find_region(value);
            if (region) {
                config.region = region;
            }
        } else if (strcasecmp(key, "sub_band") == 0) {
            uint32_t parsed = 0;
            if (parse_u32(value, &parsed)) {
                config.sub_band = (uint8_t)parsed;
            }
        } else if (strcasecmp(key, "join_eui") == 0 || strcasecmp(key, "app_eui") == 0) {
            parse_eui(value, &config.join_eui);
        } else if (strcasecmp(key, "dev_eui") == 0) {
            config.has_dev_eui = parse_eui(value, &config.dev_eui);
        } else if (strcasecmp(key, "app_key") == 0) {
            config.has_app_key = parse_key(value, config.app_key);
        } else if (strcasecmp(key, "nwk_key") == 0 || strcasecmp(key, "network_key") == 0) {
            config.has_nwk_key = parse_key(value, config.nwk_key);
        } else if (strcasecmp(key, "uplink_interval_s") == 0 || strcasecmp(key, "interval_s") == 0) {
            uint32_t parsed = 0;
            if (parse_u32(value, &parsed)) {
                config.uplink_interval_s = parsed;
            }
        } else if (strcasecmp(key, "fport") == 0 || strcasecmp(key, "port") == 0) {
            uint32_t parsed = 0;
            if (parse_u32(value, &parsed) && parsed <= 223) {
                config.fport = (uint8_t)parsed;
            }
        } else if (strcasecmp(key, "confirmed") == 0) {
            parse_bool_value(value, &config.confirmed);
        } else if (strcasecmp(key, "adr") == 0) {
            parse_bool_value(value, &config.adr);
        } else if (strcasecmp(key, "datarate") == 0) {
            uint32_t parsed = 0;
            if (parse_u32(value, &parsed) && parsed <= 15) {
                config.datarate = (uint8_t)parsed;
            }
        } else if (strcasecmp(key, "duty_cycle") == 0) {
            parse_bool_value(value, &config.duty_cycle);
        } else if (strcasecmp(key, "duty_cycle_ms_per_hour") == 0) {
            parse_u32(value, &config.duty_cycle_ms_per_hour);
        } else if (strcasecmp(key, "dwell_time") == 0) {
            parse_bool_value(value, &config.dwell_time);
        } else if (strcasecmp(key, "dwell_time_ms") == 0) {
            parse_u32(value, &config.dwell_time_ms);
        } else if (strcasecmp(key, "tx_power") == 0) {
            int32_t parsed = 0;
            if (parse_i32(value, &parsed) && parsed >= -9 && parsed <= 22) {
                config.tx_power = (int8_t)parsed;
                config.has_tx_power = true;
            }
        }
    }
    file.close();

    config.uplink_interval_s = normalize_interval(config.uplink_interval_s);
    config_loaded = true;
    config_valid = config.has_dev_eui && config.has_app_key && config.has_nwk_key;
    return config_valid;
}

static const char *state_decode(int16_t state)
{
    switch (state) {
    case LORAWAN_ERR_CONFIG_INVALID:
        return "CONFIG_INVALID";
    case LORAWAN_ERR_NO_MEMORY:
        return "NO_MEMORY";
    case LORAWAN_ERR_SPI_LOCK:
        return "SPI_LOCK_FAILED";
    case RADIOLIB_ERR_NONE:
        return "OK";
    case RADIOLIB_ERR_CHIP_NOT_FOUND:
        return "CHIP_NOT_FOUND";
    case RADIOLIB_ERR_PACKET_TOO_LONG:
        return "PACKET_TOO_LONG";
    case RADIOLIB_ERR_RX_TIMEOUT:
        return "RX_TIMEOUT";
    case RADIOLIB_ERR_INVALID_FREQUENCY:
        return "INVALID_FREQ";
    case RADIOLIB_ERR_INVALID_OUTPUT_POWER:
        return "INVALID_POWER";
    case RADIOLIB_ERR_NETWORK_NOT_JOINED:
        return "NOT_JOINED";
    case RADIOLIB_ERR_NO_JOIN_ACCEPT:
        return "NO_JOIN_ACCEPT";
    case RADIOLIB_ERR_UPLINK_UNAVAILABLE:
        return "UPLINK_UNAVAILABLE";
    case RADIOLIB_ERR_DWELL_TIME_EXCEEDED:
        return "DWELL_EXCEEDED";
    case RADIOLIB_LORAWAN_SESSION_RESTORED:
        return "SESSION_RESTORED";
    case RADIOLIB_LORAWAN_NEW_SESSION:
        return "NEW_SESSION";
    case RADIOLIB_ERR_NONCES_DISCARDED:
        return "NONCES_DISCARDED";
    case RADIOLIB_ERR_SESSION_DISCARDED:
        return "SESSION_DISCARDED";
    default:
        return "RADIO_ERR";
    }
}

static bool ensure_store(void)
{
    if (store_open) {
        return true;
    }
    store_open = store.begin(LORAWAN_PREF_NAMESPACE, false);
    return store_open;
}

static void save_nonces(void)
{
    if (!node || !ensure_store()) {
        return;
    }
    uint8_t buffer[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
    memcpy(buffer, node->getBufferNonces(), sizeof(buffer));
    store.putBytes(LORAWAN_PREF_NONCES_KEY, buffer, sizeof(buffer));
}

static void save_session(void)
{
    if (!node) {
        return;
    }
    memcpy(lorawan_session, node->getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
}

static void reset_node(void)
{
    if (node) {
        delete node;
        node = NULL;
    }
    joined = false;
}

static bool check_radio_available(void)
{
    return (hw_get_device_online() & HW_RADIO_ONLINE);
}

static bool lorawan_bme_available(void)
{
#if defined(USING_BME280)
    return (hw_get_device_online() & HW_BME280_ONLINE);
#else
    return false;
#endif
}

static bool lorawan_gps_available(void)
{
#if !defined(EXCLUDE_GPS)
    return (hw_get_device_online() & HW_GPS_ONLINE);
#else
    return false;
#endif
}

static void radio_unavailable_msgbox_cb(lv_event_t *e)
{
    (void)e;
    if (radio_unavailable_msgbox) {
        destroy_msgbox(radio_unavailable_msgbox);
        radio_unavailable_msgbox = NULL;
    }
    cleanup();
}

static void show_radio_unavailable_msgbox(void)
{
    if (radio_unavailable_msgbox) {
        return;
    }

    static const char *btns[] = {"OK", ""};
    char msg[128];
    snprintf(msg, sizeof(msg),
             "%s radio not detected.\nPlease flash the correct firmware for this hardware.",
             LORAWAN_RADIO_NAME);
    radio_unavailable_msgbox = create_msgbox(
                                   lv_scr_act(),
                                   "LoRaWAN",
                                   msg,
                                   btns,
                                   radio_unavailable_msgbox_cb,
                                   NULL);
}

static int16_t prepare_radio_locked(void)
{
    if (!config.region) {
        return LORAWAN_ERR_CONFIG_INVALID;
    }
#ifdef ARDUINO_T_DECK_V2
    instance.setRFFrequencyBand(config.region->rf_freq_mhz);
#endif

    radio.standby();

    if (!(hw_get_device_online() & HW_RADIO_ONLINE)) {
        return RADIOLIB_ERR_CHIP_NOT_FOUND;
    }
    return RADIOLIB_ERR_NONE;
}

static int16_t setup_node_locked(void)
{
    reset_node();
    node = new LoRaWANNode(&radio, config.region->band, config.sub_band);
    if (!node) {
        return LORAWAN_ERR_NO_MEMORY;
    }
    int16_t state = node->beginOTAA(config.join_eui, config.dev_eui, config.nwk_key, config.app_key);
    if (state != RADIOLIB_ERR_NONE) {
        return state;
    }
    if (ensure_store() && store.isKey(LORAWAN_PREF_NONCES_KEY)) {
        uint8_t buffer[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
        size_t len = store.getBytes(LORAWAN_PREF_NONCES_KEY, buffer, sizeof(buffer));
        if (len == sizeof(buffer)) {
            state = node->setBufferNonces(buffer);
            if (state != RADIOLIB_ERR_NONE) {
                LILYGO_LOG_PRINTF("LORAWAN: restore nonces failed: %s (%d)\n", state_decode(state), state);
            }
        }
    }
    state = node->setBufferSession(lorawan_session);
    if (state != RADIOLIB_ERR_NONE) {
        LILYGO_LOG_PRINTF("LORAWAN: restore session skipped: %s (%d)\n", state_decode(state), state);
    }
    return RADIOLIB_ERR_NONE;
}

static void apply_node_settings(void)
{
    if (!node) {
        return;
    }
    node->setADR(config.adr);
    node->setDatarate(config.datarate);
    node->setDutyCycle(config.duty_cycle, config.duty_cycle_ms_per_hour);
    node->setDwellTime(config.dwell_time, config.dwell_time_ms);
    if (config.has_tx_power) {
        node->setTxPower(config.tx_power);
    }
}

static uint32_t interval_to_ms(uint32_t seconds)
{
    if (seconds < 1) {
        seconds = 60;
    }
    if (seconds > UINT32_MAX / 1000UL) {
        return UINT32_MAX;
    }
    return seconds * 1000UL;
}

static uint32_t next_uplink_wait_ms(void)
{
    uint32_t wait_ms = interval_to_ms(config.uplink_interval_s);
    if (node) {
        RadioLibTime_t duty_wait = node->timeUntilUplink();
        if (duty_wait > wait_ms) {
            wait_ms = duty_wait > UINT32_MAX ? UINT32_MAX : (uint32_t)duty_wait;
        }
    }
    return wait_ms < 1000UL ? 1000UL : wait_ms;
}

static uint32_t schedule_next_uplink(void)
{
    uint32_t wait_ms = next_uplink_wait_ms();
    next_uplink_ms = millis() + wait_ms;
    return wait_ms;
}

static bool is_join_success(int16_t state)
{
    return state == RADIOLIB_LORAWAN_SESSION_RESTORED ||
           state == RADIOLIB_LORAWAN_NEW_SESSION;
}

static int16_t run_join_core(void)
{
    int16_t state = prepare_radio_locked();
    if (state != RADIOLIB_ERR_NONE) {
        joined = false;
        return state;
    }

    state = setup_node_locked();
    if (state != RADIOLIB_ERR_NONE) {
        joined = false;
        return state;
    }

    state = node->activateOTAA();
    save_nonces();
    if (!is_join_success(state)) {
        joined = false;
        return state;
    }

    joined = true;
    apply_node_settings();
    save_session();
    schedule_next_uplink();
    return state;
}

static int16_t run_join_with_lock(void)
{
#ifdef ARDUINO
    if (!instance.lockSPI(portMAX_DELAY)) {
        return LORAWAN_ERR_SPI_LOCK;
    }
    int16_t state = run_join_core();
    instance.unlockSPI();
    return state;
#else
    return run_join_core();
#endif
}

static bool finish_join_result(int16_t state, bool from_auto)
{
    set_controls_busy(false);

    if (!is_join_success(state)) {
        joined = false;
        join_view = LORAWAN_JOIN_FAILED;
        set_status("Join failed: %s (%d)", state_decode(state), state);
        if (from_auto) {
            disable_auto_uplink();
        }
        update_all_labels();
        return false;
    }

    join_view = LORAWAN_JOIN_JOINED;
    set_status("Joined: %s", state_decode(state));
    update_all_labels();
    return true;
}

static bool do_join(void)
{
    if (!config_loaded) {
        bool created = false;
        load_config_file(&created);
    }
    if (!config_valid) {
        set_status("Edit SD config first");
        join_view = LORAWAN_JOIN_CONFIG_INVALID;
        update_all_labels();
        return false;
    }

    join_view = LORAWAN_JOIN_JOINING;
    set_status("Joining...");
    set_controls_busy(true);
    update_all_labels();
    lv_refr_now(NULL);

    int16_t state = run_join_with_lock();
    return finish_join_result(state, false);
}

#ifdef ARDUINO
static void lorawan_join_task(void *param)
{
    (void)param;
    int16_t state = run_join_with_lock();
    join_task_result = state;
    join_task_done = true;
    join_task_handle = NULL;
    vTaskDelete(NULL);
}
#endif

static bool start_join_async(bool from_auto)
{
    if (join_in_progress()) {
        set_status("Join already running");
        return false;
    }
    if (!config_loaded) {
        bool created = false;
        load_config_file(&created);
    }
    if (!config_valid) {
        join_view = LORAWAN_JOIN_CONFIG_INVALID;
        set_status("Edit SD config first");
        if (from_auto) {
            disable_auto_uplink();
        }
        update_all_labels();
        return false;
    }

    join_task_from_auto = from_auto;
    join_task_running = true;
    join_task_done = false;
    join_task_result = RADIOLIB_ERR_NONE;
    join_view = LORAWAN_JOIN_JOINING;
    set_controls_busy(true);
    set_status("Joining...");
    update_all_labels();
    lv_refr_now(NULL);

#ifdef ARDUINO
    BaseType_t ok = xTaskCreatePinnedToCore(lorawan_join_task, "lwJoin", 8192,
                                            NULL, tskIDLE_PRIORITY + 1,
                                            &join_task_handle, 0);
    if (ok != pdPASS) {
        join_task_handle = NULL;
        join_task_running = false;
        join_task_done = false;
        join_view = LORAWAN_JOIN_FAILED;
        set_controls_busy(false);
        set_status("Join task failed");
        if (from_auto) {
            disable_auto_uplink();
        }
        update_all_labels();
        return false;
    }
    return true;
#else
    int16_t state = run_join_with_lock();
    join_task_running = false;
    join_task_done = false;
    return finish_join_result(state, from_auto);
#endif
}

static void poll_join_task(void)
{
    if (!join_result_pending()) {
        return;
    }

    int16_t state = join_task_result;
    bool from_auto = join_task_from_auto;
    join_task_running = false;
    join_task_done = false;
    join_task_from_auto = false;
#ifdef ARDUINO
    join_task_handle = NULL;
#endif
    finish_join_result(state, from_auto);
}

static int16_t clamp_i16(long value)
{
    if (value < INT16_MIN + 1) {
        return INT16_MIN + 1;
    }
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    return (int16_t)value;
}

static uint16_t clamp_u16(long value)
{
    if (value < 0) {
        return 0;
    }
    if (value > UINT16_MAX - 1) {
        return UINT16_MAX - 1;
    }
    return (uint16_t)value;
}

static int32_t clamp_i32(double value)
{
    if (value < (double)INT32_MIN + 1.0) {
        return INT32_MIN + 1;
    }
    if (value > (double)INT32_MAX) {
        return INT32_MAX;
    }
    return (int32_t)lround(value);
}

static void fill_mock_bme(lorawan_sample_t &sample)
{
    uint32_t phase = (millis() / 10000UL) % 12;
    sample.bme_valid = true;
    sample.bme_mock = true;
    sample.bme_temp = 24.5f + (float)phase * 0.1f;
    sample.bme_humi = 48.0f + (float)((phase * 3) % 10);
    sample.bme_press = 1012.0f + (float)(phase % 4);
    sample.bme_alt = 0.0f;
}

static void fill_mock_gps(lorawan_sample_t &sample)
{
    sample.gps_location_valid = true;
    sample.gps_datetime_valid = false;
    sample.gps_speed_valid = true;
    sample.gps_altitude_valid = true;
    sample.gps_mock = true;
    sample.gps_lat = 0.0;
    sample.gps_lng = 0.0;
    sample.gps_speed = 0.0;
    sample.gps_altitude = 0.0;
    sample.gps_satellites = 0;
}

static void put_u8(uint8_t *payload, size_t &pos, uint8_t value)
{
    payload[pos++] = value;
}

static void put_u16(uint8_t *payload, size_t &pos, uint16_t value)
{
    payload[pos++] = (uint8_t)(value >> 8);
    payload[pos++] = (uint8_t)(value & 0xFF);
}

static void put_i16(uint8_t *payload, size_t &pos, int16_t value)
{
    put_u16(payload, pos, (uint16_t)value);
}

static void put_i32(uint8_t *payload, size_t &pos, int32_t value)
{
    payload[pos++] = (uint8_t)((uint32_t)value >> 24);
    payload[pos++] = (uint8_t)((uint32_t)value >> 16);
    payload[pos++] = (uint8_t)((uint32_t)value >> 8);
    payload[pos++] = (uint8_t)((uint32_t)value & 0xFF);
}

static void collect_sample(lorawan_sample_t &sample)
{
    memset(&sample, 0, sizeof(sample));
    sample.battery_percent = -1;

#if defined(USING_BME280)
    if (lorawan_bme_available()) {
        hw_bme_get_data(sample.bme_temp, sample.bme_humi, sample.bme_press, sample.bme_alt);
        sample.bme_valid = !isnan(sample.bme_temp) && !isnan(sample.bme_humi) && !isnan(sample.bme_press);
    } else {
        fill_mock_bme(sample);
    }
#else
    fill_mock_bme(sample);
#endif

#if !defined(EXCLUDE_GPS)
    if (lorawan_gps_available()) {
        gps_params_t gps = {};
        hw_get_gps_info(gps);
        sample.gps_location_valid = gps.location_valid;
        sample.gps_datetime_valid = gps.datetime_valid;
        sample.gps_speed_valid = gps.speed_valid;
        sample.gps_altitude_valid = gps.altitude_valid;
        sample.gps_lat = gps.lat;
        sample.gps_lng = gps.lng;
        sample.gps_speed = gps.speed;
        sample.gps_altitude = gps.altitude;
        sample.gps_satellites = gps.satellite;
        sample.gps_datetime = gps.datetime;
    } else {
        fill_mock_gps(sample);
    }
#else
    fill_mock_gps(sample);
#endif

    monitor_params_t monitor;
    hw_get_monitor_params(monitor);
    if (monitor.battery_percent >= 0 && monitor.battery_percent <= 100) {
        sample.battery_valid = true;
        sample.battery_percent = monitor.battery_percent;
    }
}

static size_t build_payload(const lorawan_sample_t &sample, uint8_t *payload, size_t payload_size)
{
    (void)payload_size;
    size_t pos = 0;
    uint8_t flags = 0;
    if (sample.bme_valid) flags |= 0x01;
    if (sample.gps_location_valid) flags |= 0x02;
    if (sample.gps_datetime_valid) flags |= 0x04;
    if (sample.gps_speed_valid) flags |= 0x08;
    if (sample.gps_altitude_valid) flags |= 0x10;
    if (sample.battery_valid) flags |= 0x20;

    put_u8(payload, pos, LORAWAN_PAYLOAD_VERSION);
    put_u8(payload, pos, flags);

    put_i16(payload, pos, sample.bme_valid ? clamp_i16(lroundf(sample.bme_temp * 100.0f)) : INT16_MIN);
    put_u16(payload, pos, sample.bme_valid ? clamp_u16(lroundf(sample.bme_humi * 100.0f)) : UINT16_MAX);
    put_u16(payload, pos, sample.bme_valid ? clamp_u16(lroundf(sample.bme_press * 10.0f)) : UINT16_MAX);
    put_i16(payload, pos, sample.bme_valid ? clamp_i16(lroundf(sample.bme_alt * 10.0f)) : INT16_MIN);

    put_i32(payload, pos, sample.gps_location_valid ? clamp_i32(sample.gps_lat * 10000000.0) : INT32_MIN);
    put_i32(payload, pos, sample.gps_location_valid ? clamp_i32(sample.gps_lng * 10000000.0) : INT32_MIN);
    put_i16(payload, pos, sample.gps_altitude_valid ? clamp_i16(lround(sample.gps_altitude * 10.0)) : INT16_MIN);
    put_u16(payload, pos, sample.gps_speed_valid ? clamp_u16(lround(sample.gps_speed * 100.0)) : UINT16_MAX);
    put_u8(payload, pos, sample.gps_satellites > 255 ? 255 : (uint8_t)sample.gps_satellites);

    if (sample.gps_datetime_valid) {
        put_u8(payload, pos, sample.gps_datetime.tm_year + 1900 >= 2000 ?
               (uint8_t)(sample.gps_datetime.tm_year + 1900 - 2000) : 0);
        put_u8(payload, pos, (uint8_t)(sample.gps_datetime.tm_mon + 1));
        put_u8(payload, pos, (uint8_t)sample.gps_datetime.tm_mday);
        put_u8(payload, pos, (uint8_t)sample.gps_datetime.tm_hour);
        put_u8(payload, pos, (uint8_t)sample.gps_datetime.tm_min);
        put_u8(payload, pos, (uint8_t)sample.gps_datetime.tm_sec);
    } else {
        for (int i = 0; i < 6; ++i) {
            put_u8(payload, pos, 0);
        }
    }

    put_u8(payload, pos, sample.battery_valid ? (uint8_t)sample.battery_percent : 255);
    return pos;
}

static void bytes_to_hex(const uint8_t *data, size_t len, char *out, size_t out_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 2 < out_size; ++i) {
        out[pos++] = hex[data[i] >> 4];
        out[pos++] = hex[data[i] & 0x0F];
    }
    out[pos] = '\0';
}

static void update_sample_labels(void)
{
    char buf[128];
    if (last_sample.bme_valid) {
        snprintf(buf, sizeof(buf), "%s%.1fC %.0f%% %.0fhPa",
                 last_sample.bme_mock ? "Sim " : "",
                 last_sample.bme_temp, last_sample.bme_humi, last_sample.bme_press);
    } else {
        snprintf(buf, sizeof(buf), "N.A");
    }
    set_label(labels.bme, buf);

    if (last_sample.gps_location_valid) {
        snprintf(buf, sizeof(buf), "%s%.5f, %.5f %usat",
                 last_sample.gps_mock ? "Sim " : "",
                 last_sample.gps_lat, last_sample.gps_lng,
                 (unsigned)last_sample.gps_satellites);
    } else {
        snprintf(buf, sizeof(buf), "No fix %usat", (unsigned)last_sample.gps_satellites);
    }
    set_label(labels.gps, buf);

    uint8_t payload[40];
    size_t len = build_payload(last_sample, payload, sizeof(payload));
    snprintf(buf, sizeof(buf), "%u B flags=0x%02X", (unsigned)len, payload[1]);
    set_label(labels.payload, buf);
}

static bool do_send(bool manual)
{
    if (join_in_progress()) {
        if (manual) {
            set_status("Join in progress");
        }
        return false;
    }
    if (!joined || !node) {
        if (manual) {
            set_status("Join first");
        } else {
            set_status("Auto skipped: not joined");
        }
        update_all_labels();
        return false;
    }

    collect_sample(last_sample);
    update_sample_labels();

    uint8_t uplink[40];
    size_t uplink_len = build_payload(last_sample, uplink, sizeof(uplink));
    uint8_t downlink[64];
    size_t downlink_len = 0;
    LoRaWANEvent_t uplink_details;
    LoRaWANEvent_t downlink_details;

    uint8_t batt = last_sample.battery_valid ? (uint8_t)last_sample.battery_percent : 255;
    node->setDeviceStatus(batt);

    set_status("Sending...");
    lv_refr_now(NULL);

    if (node->getFCntUp() == 1) {
        node->sendMacCommandReq(RADIOLIB_LORAWAN_MAC_LINK_CHECK);
        node->sendMacCommandReq(RADIOLIB_LORAWAN_MAC_DEVICE_TIME);
    }

    int16_t state = node->sendReceive(uplink, uplink_len, config.fport, downlink, &downlink_len,
                                      config.confirmed, &uplink_details, &downlink_details);

    if (state < RADIOLIB_ERR_NONE) {
        uint32_t wait_ms = schedule_next_uplink();
        set_status("Send failed: %s (%d), next in %lus",
                   state_decode(state), state, (unsigned long)(wait_ms / 1000UL));
        char fail_buf[64];
        snprintf(fail_buf, sizeof(fail_buf), "Failed, wait %lus",
                 (unsigned long)(wait_ms / 1000UL));
        set_label(labels.last, fail_buf);
        update_all_labels();
        return false;
    }

    save_session();
    schedule_next_uplink();

    char buf[160];
    snprintf(buf, sizeof(buf), "%s FCnt=%lu ToA=%lums",
             state > 0 ? "Downlink" : "Sent",
             (unsigned long)node->getFCntUp(),
             (unsigned long)node->getLastToA());
    set_label(labels.last, buf);
    set_status("Send OK");

    if (state > 0) {
        char hex[132];
        if (downlink_len > 0) {
            bytes_to_hex(downlink, downlink_len, hex, sizeof(hex));
            snprintf(buf, sizeof(buf), "RX%u %s RSSI %.1f SNR %.1f",
                     (unsigned)state, hex, radio.getRSSI(), radio.getSNR());
        } else {
            snprintf(buf, sizeof(buf), "RX%u MAC RSSI %.1f SNR %.1f",
                     (unsigned)state, radio.getRSSI(), radio.getSNR());
        }
        set_label(labels.downlink, buf);
    } else {
        set_label(labels.downlink, "None");
    }

    update_all_labels();
    return true;
}

static void update_config_labels(void)
{
    char buf[96];
    set_label(labels.config, LORAWAN_CONFIG_PATH);

    snprintf(buf, sizeof(buf), "%s SB%u %s DR%u",
             config.region ? config.region->name : "N.A",
             config.sub_band,
             config.adr ? "ADR" : "Fixed",
             config.datarate);
    set_label(labels.region, buf);

    if (config.has_dev_eui) {
        format_eui(config.dev_eui, buf, sizeof(buf));
        set_label(labels.dev_eui, buf);
    } else {
        set_label(labels.dev_eui, "Missing");
    }

    format_interval(config.uplink_interval_s, buf, sizeof(buf));
    set_label(labels.interval, buf);
    if (interval_dropdown) {
        lv_dropdown_set_selected(interval_dropdown, interval_index_from_seconds(config.uplink_interval_s));
    }
}

static void update_join_labels(void)
{
    char buf[96];
    const char *join_text = "Not joined";
    if (join_in_progress()) {
        join_text = "Joining";
    } else {
        switch (join_view) {
        case LORAWAN_JOIN_JOINED:
            join_text = joined ? "Joined" : "Not joined";
            break;
        case LORAWAN_JOIN_JOINING:
            join_text = "Joining";
            break;
        case LORAWAN_JOIN_FAILED:
            join_text = "Failed";
            break;
        case LORAWAN_JOIN_CONFIG_INVALID:
            join_text = "Config invalid";
            break;
        case LORAWAN_JOIN_NOT_JOINED:
        default:
            join_text = joined ? "Joined" : "Not joined";
            break;
        }
    }
    set_label(labels.join_state, join_text);
    if (joined && node) {
        snprintf(buf, sizeof(buf), "0x%08lX", (unsigned long)node->getDevAddr());
        set_label(labels.dev_addr, buf);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)node->getFCntUp());
        set_label(labels.fcnt, buf);
    } else {
        set_label(labels.dev_addr, "--");
        set_label(labels.fcnt, "--");
    }
    snprintf(buf, sizeof(buf), "%s %s", USING_RADIO_NAME,
             (hw_get_device_online() & HW_RADIO_ONLINE) ? "Online" : "Offline");
    set_label(labels.radio, buf);
}

static void update_timer_labels(void)
{
    char buf[48];
    if (join_in_progress()) {
        snprintf(buf, sizeof(buf), "Joining");
    } else if (auto_uplink && joined) {
        uint32_t now = millis();
        uint32_t remaining = 0;
        if ((int32_t)(next_uplink_ms - now) > 0) {
            remaining = (next_uplink_ms - now + 999) / 1000;
        }
        snprintf(buf, sizeof(buf), "%lus", (unsigned long)remaining);
    } else {
        snprintf(buf, sizeof(buf), "Auto off");
    }
    set_label(labels.next, buf);
}

static void update_all_labels(void)
{
    update_config_labels();
    update_join_labels();
    update_sample_labels();
    update_timer_labels();
    set_controls_busy(join_in_progress());
}

static void reload_config_cb(lv_event_t *e)
{
    (void)e;
    if (join_in_progress()) {
        set_status("Join in progress");
        return;
    }
    hw_feedback();
    bool created = false;
    bool ok = load_config_file(&created);
    reset_node();
    join_view = LORAWAN_JOIN_NOT_JOINED;
    if (created) {
        set_status("Template created on SD");
    } else if (ok) {
        set_status("Config loaded");
    } else {
        set_status("Config invalid or SD missing");
    }
    update_all_labels();
}

static void join_btn_cb(lv_event_t *e)
{
    (void)e;
    if (join_in_progress()) {
        set_status("Join in progress");
        return;
    }
    hw_feedback();
    start_join_async(false);
}

static void send_btn_cb(lv_event_t *e)
{
    (void)e;
    if (join_in_progress()) {
        set_status("Join in progress");
        return;
    }
    hw_feedback();
    do_send(true);
}

static void reset_session_cb(lv_event_t *e)
{
    (void)e;
    if (join_in_progress()) {
        set_status("Join in progress");
        return;
    }
    hw_feedback();
    memset(lorawan_session, 0, sizeof(lorawan_session));
    if (node) {
        node->clearSession();
    }
    if (ensure_store()) {
        store.remove(LORAWAN_PREF_NONCES_KEY);
    }
    joined = false;
    join_view = LORAWAN_JOIN_NOT_JOINED;
    set_status("Session reset");
    update_all_labels();
}

static void interval_cb(lv_event_t *e)
{
    if (join_in_progress()) {
        set_status("Join in progress");
        return;
    }
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    uint16_t selected = lv_dropdown_get_selected(obj);
    if (selected < sizeof(interval_seconds_list) / sizeof(interval_seconds_list[0])) {
        config.uplink_interval_s = interval_seconds_list[selected];
    }
    if (joined) {
        schedule_next_uplink();
    }
    hw_feedback();
    update_all_labels();
}

static void auto_switch_cb(lv_event_t *e)
{
    if (join_in_progress()) {
        set_status("Join in progress");
        return;
    }
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    auto_uplink = lv_obj_has_state(obj, LV_STATE_CHECKED);
    hw_feedback();

    if (auto_uplink && !joined) {
        disable_auto_uplink();
        set_status("Join first");
        update_all_labels();
        return;
    }
    if (auto_uplink && joined) {
        schedule_next_uplink();
    }
    update_all_labels();
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    poll_join_task();
    if (join_in_progress()) {
        return;
    }

    uint32_t now = millis();
    if ((int32_t)(now - next_sample_ms) >= 0) {
        next_sample_ms = now + 2000;
        collect_sample(last_sample);
        update_sample_labels();
    }
    if (auto_uplink && joined && (int32_t)(now - next_uplink_ms) >= 0) {
        do_send(false);
    }
    update_timer_labels();
}

static void cleanup(void)
{
    if (join_in_progress()) {
        set_status("Join in progress");
        return;
    }
    if (radio_unavailable_msgbox) {
        destroy_msgbox(radio_unavailable_msgbox);
        radio_unavailable_msgbox = NULL;
    }
    if (lorawan_timer) {
        lv_timer_del(lorawan_timer);
        lorawan_timer = NULL;
    }
    reset_node();
    if (store_open) {
        store.end();
        store_open = false;
    }
    if (hw_get_device_online() & HW_RADIO_ONLINE) {
        radio.standby();
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    interval_dropdown = NULL;
    auto_switch = NULL;
    reload_button = NULL;
    join_button = NULL;
    send_button = NULL;
    reset_button = NULL;
    join_task_running = false;
    join_task_done = false;
    join_task_from_auto = false;
#ifdef ARDUINO
    join_task_handle = NULL;
#endif
    memset(&labels, 0, sizeof(labels));
    menu_show();
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    if (join_in_progress()) {
        set_status("Join in progress");
        return;
    }
    cleanup();
}

void ui_lorawan_lr1121_enter(lv_obj_t *parent)
{
    memset(&labels, 0, sizeof(labels));
    memset(&last_sample, 0, sizeof(last_sample));
    join_view = LORAWAN_JOIN_NOT_JOINED;
    join_task_running = false;
    join_task_done = false;
    join_task_from_auto = false;
#ifdef ARDUINO
    join_task_handle = NULL;
#endif
    page_container = ui_create_app_page(parent, "LoRaWAN", back_event_handler);

    config_defaults(config);
    if (!check_radio_available()) {
        show_radio_unavailable_msgbox();
        return;
    }

    lv_obj_t *card = ui_create_card(page_container, "Status");
    add_info_row(card, LV_SYMBOL_WIFI, "Status", "Loading", &labels.status);
    add_info_row(card, LV_SYMBOL_SD_CARD, "Config", LORAWAN_CONFIG_PATH, &labels.config);
    add_info_row(card, LV_SYMBOL_SETTINGS, "Region", "--", &labels.region);
    add_info_row(card, LV_SYMBOL_SETTINGS, "DevEUI", "--", &labels.dev_eui);
    add_info_row(card, LV_SYMBOL_LOOP, "Interval", "--", &labels.interval);
    add_info_row(card, LV_SYMBOL_WIFI, "Radio", "--", &labels.radio);

    card = ui_create_card(page_container, "Session");
    add_info_row(card, LV_SYMBOL_OK, "Join", "Not joined", &labels.join_state);
    add_info_row(card, LV_SYMBOL_WIFI, "DevAddr", "--", &labels.dev_addr);
    add_info_row(card, LV_SYMBOL_UPLOAD, "FCntUp", "--", &labels.fcnt);
    add_info_row(card, LV_SYMBOL_LOOP, "Next", "--", &labels.next);

    card = ui_create_card(page_container, "Uplink Data");
    add_info_row(card, LV_SYMBOL_SETTINGS, "BME280", "--", &labels.bme);
    add_info_row(card, LV_SYMBOL_GPS, "GPS", "--", &labels.gps);
    add_info_row(card, LV_SYMBOL_UPLOAD, "Payload", "--", &labels.payload);
    add_info_row(card, LV_SYMBOL_OK, "Last", "--", &labels.last);
    add_info_row(card, LV_SYMBOL_DOWNLOAD, "Downlink", "--", &labels.downlink);

    card = ui_create_card(page_container, "Control");
    lv_obj_t *row = ui_create_card_dropdown(card, LV_SYMBOL_LOOP, "Interval",
                                            interval_options, 1, interval_cb);
    interval_dropdown = row_widget(row);
    row = ui_create_card_switch(card, LV_SYMBOL_LOOP, "Auto Uplink", false, auto_switch_cb);
    auto_switch = row_widget(row);
    row = ui_create_card_button(card, LV_SYMBOL_REFRESH, "Config", "Reload", reload_config_cb);
    reload_button = row_widget(row);
    row = ui_create_card_button(card, LV_SYMBOL_OK, "Join", "Join", join_btn_cb);
    join_button = row_widget(row);
    row = ui_create_card_button(card, LV_SYMBOL_UPLOAD, "Uplink", "Send", send_btn_cb);
    send_button = row_widget(row);
    row = ui_create_card_button(card, LV_SYMBOL_TRASH, "Session", "Reset", reset_session_cb);
    reset_button = row_widget(row);

    bool created = false;
    bool ok = load_config_file(&created);
    collect_sample(last_sample);
    update_all_labels();
    if (created) {
        set_status("Template created on SD");
    } else if (ok) {
        set_status("Ready");
    } else {
        set_status("Config invalid or SD missing");
    }

    next_sample_ms = millis() + 2000;
    lorawan_timer = lv_timer_create(timer_cb, 1000, NULL);
}

void ui_lorawan_lr1121_exit(lv_obj_t *parent)
{
    (void)parent;
    cleanup();
}

app_t ui_lorawan_lr1121_main = {
    .setup_func_cb = ui_lorawan_lr1121_enter,
    .exit_func_cb = ui_lorawan_lr1121_exit,
    .user_data = NULL,
};

#endif
