/**
 * @file      ui_ble_scanner.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-29
 * 
 * BLE Scanner app based on NimBLE-Arduino observer scan callbacks.
 */
#include "ui_define.h"

#ifndef EXCLUDE_BLE_SCANNER

#include <algorithm>
#include <cstring>

#ifdef ARDUINO
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ROLE_OBSERVER
#define BLE_SCANNER_HAS_NIMBLE 1
#endif
#endif

#ifndef BLE_SCANNER_HAS_NIMBLE
#define BLE_SCANNER_HAS_NIMBLE 0
#endif

#define BLE_SCAN_UI_PERIOD_MS       500
#define BLE_SCAN_WINDOW_MS          4000
#define BLE_SCAN_AUTO_INTERVAL_MS   30000
#define BLE_SCAN_ACTIVE_REFRESH_MS  2000
#define BLE_SCAN_IDLE_REFRESH_MS    3000
#define BLE_SCAN_STALE_MS           45000
#define BLE_SCAN_MAX_DEVICES        48
#define BLE_SCAN_MAX_ROWS           24

static lv_obj_t *page_container = NULL;
static lv_timer_t *ui_timer = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *scan_label = NULL;
static lv_obj_t *count_label = NULL;
static lv_obj_t *total_label = NULL;
static lv_obj_t *named_label = NULL;
static lv_obj_t *strongest_label = NULL;
static lv_obj_t *selected_name_label = NULL;
static lv_obj_t *selected_addr_label = NULL;
static lv_obj_t *selected_rssi_label = NULL;
static lv_obj_t *selected_type_label = NULL;
static lv_obj_t *selected_seen_label = NULL;
static lv_obj_t *selected_service_label = NULL;
static lv_obj_t *selected_mfg_label = NULL;
static lv_obj_t *selected_adv_label = NULL;
static lv_obj_t *active_switch = NULL;
static lv_obj_t *auto_switch = NULL;
static lv_obj_t *named_filter_switch = NULL;
static lv_obj_t *interval_dd = NULL;
static lv_obj_t *device_list_card = NULL;
static lv_obj_t *device_list_cont = NULL;
static lv_obj_t *device_rows[BLE_SCAN_MAX_ROWS];

static bool active_scan_enabled = true;
static bool auto_scan_enabled = true;
static bool named_filter_enabled = true;
static uint32_t scan_interval_ms = BLE_SCAN_AUTO_INTERVAL_MS;
static uint32_t last_list_refresh_ms = 0;
static uint32_t next_auto_scan_ms = 0;

#if BLE_SCANNER_HAS_NIMBLE
typedef struct {
    string address;
    uint8_t address_type;
    string name;
    int8_t rssi;
    int8_t tx_power;
    bool has_tx_power;
    bool connectable;
    bool scannable;
    bool legacy;
    bool has_name;
    bool has_mfg;
    bool has_service_uuid;
    string service_uuid;
    uint8_t service_uuid_count;
    string mfg_summary;
    bool has_appearance;
    uint16_t appearance;
    uint16_t adv_len;
    uint32_t seen_count;
    uint32_t first_seen_ms;
    uint32_t last_seen_ms;
} ble_scan_device_t;

static SemaphoreHandle_t device_mutex = NULL;
static vector<ble_scan_device_t> scan_devices;
static vector<ble_scan_device_t> device_snapshot;
static string selected_address;
static uint32_t raw_device_count = 0;
static uint32_t raw_named_count = 0;
static volatile bool ble_scan_app_active = false;
static volatile bool scan_running = false;
static volatile bool scan_dirty = false;
static volatile bool scan_end_pending_status = false;
static volatile int scan_end_reason = 0;
#endif

static int rssi_to_percent(int8_t rssi)
{
    if (rssi >= -35) return 100;
    if (rssi <= -95) return 0;
    return (int)((rssi + 95) * 100 / 60);
}

static lv_color_t rssi_color(int8_t rssi)
{
    if (rssi >= -55) return lv_color_hex(0x00D4AA);
    if (rssi >= -72) return lv_color_hex(0xFFB800);
    if (rssi >= -85) return lv_color_hex(0xFF6B35);
    return lv_color_hex(0x777777);
}

static const char *addr_type_text(uint8_t type)
{
    switch (type) {
    case 0: return "Public";
    case 1: return "Random";
    case 2: return "RPA Public";
    case 3: return "RPA Random";
    default: return "Unknown";
    }
}

static uint32_t interval_from_dropdown(void)
{
    if (!interval_dd) return BLE_SCAN_AUTO_INTERVAL_MS;
    switch (lv_dropdown_get_selected(interval_dd)) {
    case 0: return 15000;
    case 1: return 30000;
    case 2: return 60000;
    default: return BLE_SCAN_AUTO_INTERVAL_MS;
    }
}

static void set_status(const char *text, lv_color_t color)
{
    if (!status_label) return;
    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, color, 0);
}

static void set_value_label(lv_obj_t *label, const char *text)
{
    if (!label) return;
    lv_label_set_text(label, text ? text : "--");
}

static bool time_reached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

static uint32_t seconds_until(uint32_t now, uint32_t target)
{
    if (time_reached(now, target)) {
        return 0;
    }
    return (target - now + 999) / 1000;
}

static lv_obj_t *add_info_row(lv_obj_t *card, const char *icon, const char *title,
                              const char *value, lv_obj_t **out_label)
{
    lv_obj_t *row = ui_create_card_info(card, icon, title, value);
    if (out_label) {
        *out_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
        if (*out_label) {
            lv_obj_set_width(*out_label, is_screen_small() ? 116 : 170);
            lv_label_set_long_mode(*out_label, LV_LABEL_LONG_DOT);
        }
    }
    return row;
}

static lv_obj_t *card_switch(lv_obj_t *card, const char *icon, const char *title,
                             bool checked, lv_event_cb_t cb)
{
    lv_obj_t *row = ui_create_card_switch(card, icon, title, checked, cb);
    return lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
}

static lv_obj_t *card_dropdown(lv_obj_t *card, const char *icon, const char *title,
                               const char *options, uint8_t sel, lv_event_cb_t cb)
{
    lv_obj_t *row = ui_create_card_dropdown(card, icon, title, options, sel, cb);
    lv_obj_t *dd = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    if (dd) lv_obj_set_width(dd, 110);
    return dd;
}

static void clear_device_rows(void)
{
    for (uint8_t i = 0; i < BLE_SCAN_MAX_ROWS; ++i) {
        if (device_rows[i]) {
            lv_obj_delete(device_rows[i]);
            device_rows[i] = NULL;
        }
    }
}

static lv_obj_t *create_device_list_container(lv_obj_t *parent)
{
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_set_size(list, LV_PCT(100), is_screen_small() ? 162 : 218);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 5, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_CHAIN);
    return list;
}

#if BLE_SCANNER_HAS_NIMBLE
static bool lock_devices(TickType_t ticks)
{
    return device_mutex == NULL || xSemaphoreTake(device_mutex, ticks) == pdTRUE;
}

static void unlock_devices(void)
{
    if (device_mutex) {
        xSemaphoreGive(device_mutex);
    }
}

static string hex_bytes(const string &data, size_t max_bytes)
{
    string out;
    char byte_buf[8];
    size_t limit = data.size() < max_bytes ? data.size() : max_bytes;
    for (size_t i = 0; i < limit; ++i) {
        snprintf(byte_buf, sizeof(byte_buf), "%02X", (uint8_t)data[i]);
        if (!out.empty()) out += " ";
        out += byte_buf;
    }
    if (data.size() > max_bytes) {
        out += " ...";
    }
    return out.empty() ? "--" : out;
}

static string manufacturer_summary(const NimBLEAdvertisedDevice *device)
{
    if (!device->haveManufacturerData()) {
        return "--";
    }

    string data = device->getManufacturerData();
    if (data.empty()) {
        return "--";
    }

    char prefix[24];
    if (data.size() >= 2) {
        uint16_t company_id = (uint8_t)data[0] | ((uint16_t)(uint8_t)data[1] << 8);
        snprintf(prefix, sizeof(prefix), "0x%04X  ", company_id);
    } else {
        snprintf(prefix, sizeof(prefix), "Raw  ");
    }
    return string(prefix) + hex_bytes(data, 10);
}

static string service_summary(const NimBLEAdvertisedDevice *device, uint8_t *count_out)
{
    uint8_t count = device->getServiceUUIDCount();
    if (count_out) *count_out = count;
    if (count == 0) {
        return "--";
    }

    string text = device->getServiceUUID(0).toString();
    if (count > 1) {
        char suffix[12];
        snprintf(suffix, sizeof(suffix), " +%u", count - 1);
        text += suffix;
    }
    return text;
}

static string device_type_text(const ble_scan_device_t &dev)
{
    string text;
    if (dev.connectable) text += "Conn";
    if (dev.scannable) {
        if (!text.empty()) text += " ";
        text += "Scan";
    }
    if (text.empty()) text = "Beacon";
    text += dev.legacy ? " Legacy" : " Ext";
    return text;
}

static void prune_stale_locked(uint32_t now)
{
    scan_devices.erase(
        remove_if(scan_devices.begin(), scan_devices.end(), [now](const ble_scan_device_t &dev) {
            return now - dev.last_seen_ms > BLE_SCAN_STALE_MS;
        }),
        scan_devices.end());
}

static void sort_devices_locked(void)
{
    sort(scan_devices.begin(), scan_devices.end(), [](const ble_scan_device_t &a, const ble_scan_device_t &b) {
        if (a.rssi != b.rssi) return a.rssi > b.rssi;
        if (a.last_seen_ms != b.last_seen_ms) return a.last_seen_ms > b.last_seen_ms;
        return a.address < b.address;
    });
}

static void merge_device_locked(ble_scan_device_t &stored, const ble_scan_device_t &incoming, bool count_seen)
{
    const uint32_t first_seen = stored.first_seen_ms;
    const uint32_t seen_count = stored.seen_count + (count_seen ? 1 : 0);
    const string previous_name = stored.name;
    const bool previous_has_name = stored.has_name;
    const bool previous_has_tx_power = stored.has_tx_power;
    const int8_t previous_tx_power = stored.tx_power;
    const bool previous_connectable = stored.connectable;
    const bool previous_scannable = stored.scannable;
    const bool previous_has_mfg = stored.has_mfg;
    const string previous_mfg_summary = stored.mfg_summary;
    const bool previous_has_service_uuid = stored.has_service_uuid;
    const string previous_service_uuid = stored.service_uuid;
    const uint8_t previous_service_uuid_count = stored.service_uuid_count;
    const bool previous_has_appearance = stored.has_appearance;
    const uint16_t previous_appearance = stored.appearance;
    const uint16_t previous_adv_len = stored.adv_len;

    stored = incoming;
    stored.first_seen_ms = first_seen;
    stored.seen_count = seen_count;
    stored.connectable = incoming.connectable || previous_connectable;
    stored.scannable = incoming.scannable || previous_scannable;

    if (incoming.name.empty() && !previous_name.empty()) {
        stored.name = previous_name;
        stored.has_name = previous_has_name;
    }
    if (!incoming.has_tx_power && previous_has_tx_power) {
        stored.has_tx_power = true;
        stored.tx_power = previous_tx_power;
    }
    if (!incoming.has_mfg && previous_has_mfg) {
        stored.has_mfg = true;
        stored.mfg_summary = previous_mfg_summary;
    }
    if (!incoming.has_service_uuid && previous_has_service_uuid) {
        stored.has_service_uuid = true;
        stored.service_uuid = previous_service_uuid;
        stored.service_uuid_count = previous_service_uuid_count;
    }
    if (!incoming.has_appearance && previous_has_appearance) {
        stored.has_appearance = true;
        stored.appearance = previous_appearance;
    }
    if (incoming.adv_len == 0 && previous_adv_len > 0) {
        stored.adv_len = previous_adv_len;
    }
}

static void upsert_device(const NimBLEAdvertisedDevice *device, bool count_seen)
{
    if (!device || !ble_scan_app_active) {
        return;
    }

    ble_scan_device_t incoming = {};
    incoming.address = device->getAddress().toString();
    incoming.address_type = device->getAddressType();
    incoming.name = device->haveName() ? device->getName() : "";
    incoming.rssi = device->getRSSI();
    incoming.has_tx_power = device->haveTXPower();
    incoming.tx_power = incoming.has_tx_power ? device->getTXPower() : 0;
    incoming.connectable = device->isConnectable();
    incoming.scannable = device->isScannable();
    incoming.legacy = device->isLegacyAdvertisement();
    incoming.has_name = device->haveName();
    incoming.has_mfg = device->haveManufacturerData();
    incoming.mfg_summary = manufacturer_summary(device);
    incoming.has_service_uuid = device->haveServiceUUID();
    incoming.service_uuid = service_summary(device, &incoming.service_uuid_count);
    incoming.has_appearance = device->haveAppearance();
    incoming.appearance = incoming.has_appearance ? device->getAppearance() : 0;
    incoming.adv_len = device->getAdvLength();
    incoming.first_seen_ms = millis();
    incoming.last_seen_ms = incoming.first_seen_ms;
    incoming.seen_count = count_seen ? 1 : 0;

    if (!lock_devices(0)) {
        return;
    }

    prune_stale_locked(incoming.last_seen_ms);
    auto it = find_if(scan_devices.begin(), scan_devices.end(), [&incoming](const ble_scan_device_t &dev) {
        return dev.address == incoming.address && dev.address_type == incoming.address_type;
    });

    if (it == scan_devices.end()) {
        scan_devices.push_back(incoming);
    } else {
        merge_device_locked(*it, incoming, count_seen);
    }

    if (scan_devices.size() > BLE_SCAN_MAX_DEVICES) {
        auto old_it = min_element(scan_devices.begin(), scan_devices.end(), [](const ble_scan_device_t &a,
                                    const ble_scan_device_t &b) {
            if (a.last_seen_ms != b.last_seen_ms) return a.last_seen_ms < b.last_seen_ms;
            return a.rssi < b.rssi;
        });
        if (old_it != scan_devices.end()) {
            scan_devices.erase(old_it);
        }
    }

    sort_devices_locked();
    unlock_devices();
    scan_dirty = true;
}

static void schedule_next_auto_scan(uint32_t delay_ms);

class BleScannerCallbacks : public NimBLEScanCallbacks {
    void onDiscovered(const NimBLEAdvertisedDevice *advertisedDevice) override
    {
        upsert_device(advertisedDevice, true);
    }

    void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override
    {
        upsert_device(advertisedDevice, false);
    }

    void onScanEnd(const NimBLEScanResults &results, int reason) override
    {
        (void)results;
        scan_running = false;
        scan_end_reason = reason;
        scan_end_pending_status = true;
        schedule_next_auto_scan(scan_interval_ms);
        scan_dirty = true;
    }
};

static BleScannerCallbacks scan_callbacks;

static void schedule_next_auto_scan(uint32_t delay_ms)
{
    if (!auto_scan_enabled || !ble_scan_app_active) {
        next_auto_scan_ms = 0;
        return;
    }
    next_auto_scan_ms = millis() + delay_ms;
}

static bool ensure_ble_stack(void)
{
    if (!NimBLEDevice::isInitialized()) {
        if (!NimBLEDevice::init("LilyGo BLE Scanner")) {
            set_status("NimBLE init failed.", lv_color_hex(0xFF4444));
            return false;
        }
    }
    return true;
}

static bool start_scan(void)
{
    if (!ble_scan_app_active) {
        return false;
    }
    if (!ensure_ble_stack()) {
        scan_running = false;
        schedule_next_auto_scan(scan_interval_ms);
        return false;
    }

    NimBLEScan *scan = NimBLEDevice::getScan();
    if (!scan) {
        set_status("Scanner unavailable.", lv_color_hex(0xFF4444));
        scan_running = false;
        schedule_next_auto_scan(scan_interval_ms);
        return false;
    }

    scan->setScanCallbacks(&scan_callbacks, false);
    scan->setActiveScan(active_scan_enabled);
    scan->setInterval(100);
    scan->setWindow(active_scan_enabled ? 100 : 60);
    scan->setMaxResults(0);
    scan->setScanResponseTimeout(active_scan_enabled ? 650 : 0);

    bool ok = scan->start(BLE_SCAN_WINDOW_MS, false, true);
    scan_running = ok;
    next_auto_scan_ms = 0;
    scan_end_pending_status = false;

    if (ok) {
        set_status("Scanning...", UI_COLOR_TEXT_SECONDARY);
        if (scan_label) {
            lv_label_set_text_fmt(scan_label, "%lus %s scan / %lus period",
                                  (unsigned long)(BLE_SCAN_WINDOW_MS / 1000),
                                  active_scan_enabled ? "active" : "passive",
                                  (unsigned long)(scan_interval_ms / 1000));
        }
    } else {
        schedule_next_auto_scan(scan_interval_ms);
        set_status("Scan failed to start.", lv_color_hex(0xFF4444));
        if (scan_label && auto_scan_enabled && next_auto_scan_ms) {
            lv_label_set_text_fmt(scan_label, "Retry in %lus",
                                  (unsigned long)seconds_until(millis(), next_auto_scan_ms));
        }
    }
    return ok;
}

static void update_waiting_label(uint32_t now)
{
    if (!scan_label || scan_running) {
        return;
    }

    if (auto_scan_enabled && next_auto_scan_ms) {
        lv_label_set_text_fmt(scan_label, "Next scan in %lus",
                              (unsigned long)seconds_until(now, next_auto_scan_ms));
    } else {
        lv_label_set_text_fmt(scan_label, "%lus %s scan",
                              (unsigned long)(BLE_SCAN_WINDOW_MS / 1000),
                              active_scan_enabled ? "active" : "passive");
    }
}

static void stop_scan(void)
{
    ble_scan_app_active = false;
    next_auto_scan_ms = 0;
    scan_end_pending_status = false;
    if (NimBLEDevice::isInitialized()) {
        NimBLEScan *scan = NimBLEDevice::getScan();
        if (scan) {
            scan->setScanCallbacks(NULL, false);
            if (scan->isScanning()) {
                scan->stop();
            }
            if (!scan->isScanning()) {
                scan->clearResults();
            }
        }
    }
    scan_running = false;
}

static void snapshot_devices(void)
{
    uint32_t now = lv_tick_get();
    if (!lock_devices(pdMS_TO_TICKS(10))) {
        return;
    }
    prune_stale_locked(now);
    sort_devices_locked();
    device_snapshot = scan_devices;
    raw_device_count = scan_devices.size();
    raw_named_count = 0;
    for (const auto &dev : scan_devices) {
        if (!dev.name.empty()) {
            raw_named_count++;
        }
    }
    unlock_devices();

    if (named_filter_enabled) {
        device_snapshot.erase(
            remove_if(device_snapshot.begin(), device_snapshot.end(), [](const ble_scan_device_t &dev) {
                return dev.name.empty();
            }),
            device_snapshot.end());
    }
}

static void clear_scan_devices(void)
{
    if (lock_devices(pdMS_TO_TICKS(20))) {
        scan_devices.clear();
        unlock_devices();
    }
    device_snapshot.clear();
    raw_device_count = 0;
    raw_named_count = 0;
    selected_address.clear();
    scan_dirty = true;
}

static void update_overview(void)
{
    if (count_label) {
        lv_label_set_text_fmt(count_label, "%u", (unsigned)device_snapshot.size());
    }
    if (total_label) {
        lv_label_set_text_fmt(total_label, "%u", (unsigned)raw_device_count);
    }
    if (named_label) {
        lv_label_set_text_fmt(named_label, "%u", (unsigned)raw_named_count);
    }
    if (strongest_label) {
        if (device_snapshot.empty()) {
            lv_label_set_text(strongest_label, "--");
        } else {
            const ble_scan_device_t &dev = device_snapshot[0];
            lv_label_set_text_fmt(strongest_label, "%s  %d dBm",
                                  dev.name.empty() ? dev.address.c_str() : dev.name.c_str(),
                                  dev.rssi);
        }
    }
}

static int find_selected_index(void)
{
    if (device_snapshot.empty()) {
        selected_address.clear();
        return -1;
    }

    if (!selected_address.empty()) {
        for (size_t i = 0; i < device_snapshot.size(); ++i) {
            if (device_snapshot[i].address == selected_address) {
                return (int)i;
            }
        }
    }

    selected_address = device_snapshot[0].address;
    return 0;
}

static void update_detail(void)
{
    int index = find_selected_index();
    if (index < 0) {
        set_value_label(selected_name_label, "--");
        set_value_label(selected_addr_label, "--");
        set_value_label(selected_rssi_label, "--");
        set_value_label(selected_type_label, "--");
        set_value_label(selected_seen_label, "--");
        set_value_label(selected_service_label, "--");
        set_value_label(selected_mfg_label, "--");
        set_value_label(selected_adv_label, "--");
        return;
    }

    const ble_scan_device_t &dev = device_snapshot[index];
    char buf[96];

    set_value_label(selected_name_label, dev.name.empty() ? "<unnamed>" : dev.name.c_str());
    snprintf(buf, sizeof(buf), "%s  %s", dev.address.c_str(), addr_type_text(dev.address_type));
    set_value_label(selected_addr_label, buf);
    if (dev.has_tx_power) {
        snprintf(buf, sizeof(buf), "%d dBm  TX %d", dev.rssi, dev.tx_power);
    } else {
        snprintf(buf, sizeof(buf), "%d dBm", dev.rssi);
    }
    set_value_label(selected_rssi_label, buf);
    string type_text = device_type_text(dev);
    set_value_label(selected_type_label, type_text.c_str());
    snprintf(buf, sizeof(buf), "%lu adv  %lus ago",
             (unsigned long)dev.seen_count,
             (unsigned long)((millis() - dev.last_seen_ms) / 1000));
    set_value_label(selected_seen_label, buf);
    set_value_label(selected_service_label, dev.service_uuid.c_str());
    set_value_label(selected_mfg_label, dev.mfg_summary.c_str());
    if (dev.has_appearance) {
        snprintf(buf, sizeof(buf), "%u bytes  appearance 0x%04X", dev.adv_len, dev.appearance);
    } else {
        snprintf(buf, sizeof(buf), "%u bytes", dev.adv_len);
    }
    set_value_label(selected_adv_label, buf);
}

static void device_row_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= (int)device_snapshot.size()) {
        return;
    }
    selected_address = device_snapshot[index].address;
    update_detail();

    for (uint8_t i = 0; i < BLE_SCAN_MAX_ROWS; ++i) {
        if (!device_rows[i]) continue;
        bool selected = (i == (uint8_t)index);
        lv_obj_set_style_bg_color(device_rows[i], selected ? UI_COLOR_CARD_FOCUS : UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(device_rows[i], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(device_rows[i], selected ? 1 : 0, 0);
        lv_obj_set_style_border_color(device_rows[i], UI_COLOR_ACCENT, 0);
    }
}

static void row_focus_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    if (obj) {
        lv_group_t *group = (lv_group_t *)lv_obj_get_group(obj);
        if (group) {
            lv_group_set_editing(group, false);
        }
        lv_obj_scroll_to_view_recursive(obj, LV_ANIM_ON);
    }
}

static void build_device_list(void)
{
    clear_device_rows();
    lv_obj_t *list_parent = device_list_cont ? device_list_cont : device_list_card;
    if (!list_parent) {
        return;
    }

    if (device_snapshot.empty()) {
        lv_obj_t *empty = lv_label_create(list_parent);
        device_rows[0] = empty;
        if (named_filter_enabled && raw_device_count > 0 && raw_named_count == 0) {
            lv_label_set_text(empty, "Only unnamed devices hidden by filter.");
        } else if (scan_running) {
            lv_label_set_text(empty, named_filter_enabled ? "Scanning for named devices..." : "Scanning...");
        } else {
            lv_label_set_text(empty, named_filter_enabled ? "No named BLE devices found." : "No BLE devices found.");
        }
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_color(empty, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);
        return;
    }

    int selected_index = find_selected_index();
    uint8_t limit = device_snapshot.size() < BLE_SCAN_MAX_ROWS ? device_snapshot.size() : BLE_SCAN_MAX_ROWS;

    for (uint8_t i = 0; i < limit; ++i) {
        const ble_scan_device_t &dev = device_snapshot[i];
        char meta[96];
        char right_text[24];
        uint32_t age_s = (millis() - dev.last_seen_ms) / 1000;
        snprintf(meta, sizeof(meta), "%s  %s  %lus",
                 dev.address.c_str(), device_type_text(dev).c_str(), (unsigned long)age_s);
        snprintf(right_text, sizeof(right_text), "%d dBm", dev.rssi);

        lv_obj_t *row = lv_obj_create(list_parent);
        device_rows[i] = row;
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, i == selected_index ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row, i == selected_index ? UI_COLOR_CARD_FOCUS : UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_border_width(row, i == selected_index ? 1 : 0, 0);
        lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_top(row, 5, 0);
        lv_obj_set_style_pad_bottom(row, 5, 0);
        lv_obj_set_style_pad_left(row, 0, 0);
        lv_obj_set_style_pad_right(row, 0, 0);
        lv_obj_set_style_pad_column(row, 7, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_set_style_bg_color(row, UI_COLOR_CARD_FOCUS, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(row, 1, LV_STATE_FOCUSED);
        lv_obj_add_event_cb(row, device_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, row_focus_cb, LV_EVENT_FOCUSED, NULL);
        lv_group_t *group = lv_group_get_default();
        if (group && lv_obj_get_group(row) != group) {
            lv_group_add_obj(group, row);
        }

        lv_obj_t *icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_color(icon, rssi_color(dev.rssi), 0);

        lv_obj_t *text_col = lv_obj_create(row);
        lv_obj_set_size(text_col, 1, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(text_col, 1);
        lv_obj_set_style_bg_opa(text_col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(text_col, 0, 0);
        lv_obj_set_style_pad_all(text_col, 0, 0);
        lv_obj_set_style_pad_row(text_col, 2, 0);
        lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *name_label = lv_label_create(text_col);
        lv_label_set_text(name_label, dev.name.empty() ? "<unnamed>" : dev.name.c_str());
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name_label, LV_PCT(100));
        lv_obj_set_style_text_color(name_label, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(name_label, &lv_font_montserrat_12, 0);

        lv_obj_t *meta_label = lv_label_create(text_col);
        lv_label_set_text(meta_label, meta);
        lv_label_set_long_mode(meta_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(meta_label, LV_PCT(100));
        lv_obj_set_style_text_color(meta_label, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(meta_label, &lv_font_montserrat_12, 0);

        lv_obj_t *right = lv_obj_create(row);
        lv_obj_set_size(right, 58, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(right, 0, 0);
        lv_obj_set_style_pad_all(right, 0, 0);
        lv_obj_set_style_pad_row(right, 4, 0);
        lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *rssi_label = lv_label_create(right);
        lv_label_set_text(rssi_label, right_text);
        lv_obj_set_style_text_color(rssi_label, rssi_color(dev.rssi), 0);
        lv_obj_set_style_text_font(rssi_label, &lv_font_montserrat_12, 0);

        lv_obj_t *bar = lv_bar_create(right);
        lv_obj_set_size(bar, 52, 6);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, rssi_to_percent(dev.rssi), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, UI_COLOR_TRACK, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, rssi_color(dev.rssi), LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
    }
}

static void refresh_ui(bool rebuild_list)
{
    snapshot_devices();
    update_overview();
    update_detail();
    if (rebuild_list) {
        build_device_list();
    }

    if (scan_end_pending_status && !scan_running) {
        if (device_snapshot.empty()) {
            if (named_filter_enabled && raw_device_count > 0 && raw_named_count == 0) {
                set_status("Scan complete. Unnamed devices hidden.", UI_COLOR_WARNING);
            } else {
                set_status("Scan complete. No devices.", UI_COLOR_WARNING);
            }
        } else {
            set_status("Scan complete.", UI_COLOR_ACCENT);
        }
        scan_end_pending_status = false;
        scan_end_reason = 0;
    }
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = millis();
    uint32_t refresh_period = scan_running ? BLE_SCAN_ACTIVE_REFRESH_MS : BLE_SCAN_IDLE_REFRESH_MS;
    bool rebuild = (!scan_running && scan_dirty) ||
                   ((scan_dirty || scan_running) && now - last_list_refresh_ms > refresh_period) ||
                   (now - last_list_refresh_ms > BLE_SCAN_IDLE_REFRESH_MS);

    if (auto_scan_enabled && !scan_running && next_auto_scan_ms && time_reached(now, next_auto_scan_ms)) {
        start_scan();
        now = lv_tick_get();
    }
    if (rebuild) {
        last_list_refresh_ms = now;
        scan_dirty = false;
    }
    refresh_ui(rebuild);
    update_waiting_label(now);
}

static void refresh_btn_cb(lv_event_t *e)
{
    (void)e;
    start_scan();
}

static void clear_btn_cb(lv_event_t *e)
{
    (void)e;
    clear_scan_devices();
    start_scan();
}

static void active_switch_cb(lv_event_t *e)
{
    (void)e;
    active_scan_enabled = active_switch && lv_obj_has_state(active_switch, LV_STATE_CHECKED);
    if (scan_running) {
        start_scan();
    } else {
        schedule_next_auto_scan(1000);
        set_status("Scan settings updated.", UI_COLOR_TEXT_SECONDARY);
    }
}

static void auto_switch_cb(lv_event_t *e)
{
    (void)e;
    auto_scan_enabled = auto_switch && lv_obj_has_state(auto_switch, LV_STATE_CHECKED);
    if (auto_scan_enabled) {
        if (!scan_running) {
            schedule_next_auto_scan(1000);
        }
        set_status("Auto refresh on.", UI_COLOR_TEXT_SECONDARY);
    } else if (!auto_scan_enabled) {
        next_auto_scan_ms = 0;
        set_status(scan_running ? "Auto refresh off." : "Ready.", UI_COLOR_TEXT_SECONDARY);
    }
}

static void named_filter_cb(lv_event_t *e)
{
    (void)e;
    named_filter_enabled = named_filter_switch && lv_obj_has_state(named_filter_switch, LV_STATE_CHECKED);
    selected_address.clear();
    scan_dirty = true;
    refresh_ui(true);
    set_status(named_filter_enabled ? "Showing named devices only." : "Showing all devices.", UI_COLOR_TEXT_SECONDARY);
}

static void interval_cb(lv_event_t *e)
{
    (void)e;
    scan_interval_ms = interval_from_dropdown();
    if (auto_scan_enabled && !scan_running) {
        schedule_next_auto_scan(1000);
    }
    set_status("Scan interval updated.", UI_COLOR_TEXT_SECONDARY);
}
#endif /* BLE_SCANNER_HAS_NIMBLE */

#if !BLE_SCANNER_HAS_NIMBLE
/* Keep the controls usable in the desktop build even when NimBLE is unavailable. */
static void refresh_btn_cb(lv_event_t *e) { (void)e; set_status("Demo scan ready.", UI_COLOR_ACCENT); }
static void clear_btn_cb(lv_event_t *e) { (void)e; set_status("Demo list cleared.", UI_COLOR_TEXT_SECONDARY); }
static void active_switch_cb(lv_event_t *e) { (void)e; }
static void auto_switch_cb(lv_event_t *e) { (void)e; }
static void named_filter_cb(lv_event_t *e) { (void)e; }
static void interval_cb(lv_event_t *e) { (void)e; }
#endif

static void cleanup(void)
{
    if (ui_timer) {
        lv_timer_delete(ui_timer);
        ui_timer = NULL;
    }
#if BLE_SCANNER_HAS_NIMBLE
    stop_scan();
    device_snapshot.clear();
    selected_address.clear();
#endif
    clear_device_rows();
}

static void destroy_page(void)
{
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    status_label = NULL;
    scan_label = NULL;
    count_label = NULL;
    total_label = NULL;
    named_label = NULL;
    strongest_label = NULL;
    selected_name_label = NULL;
    selected_addr_label = NULL;
    selected_rssi_label = NULL;
    selected_type_label = NULL;
    selected_seen_label = NULL;
    selected_service_label = NULL;
    selected_mfg_label = NULL;
    selected_adv_label = NULL;
    active_switch = NULL;
    auto_switch = NULL;
    named_filter_switch = NULL;
    interval_dd = NULL;
    device_list_card = NULL;
    device_list_cont = NULL;
    memset(device_rows, 0, sizeof(device_rows));
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    cleanup();
    destroy_page();
    menu_show();
}

void ui_ble_scanner_enter(lv_obj_t *parent)
{
    cleanup();
    page_container = ui_create_app_page(parent, "BLE Scanner", back_event_handler);

    lv_obj_t *card = ui_create_card(page_container, "Overview");
    status_label = lv_label_create(card);
    lv_label_set_text(status_label, "Ready.");
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

    add_info_row(card, LV_SYMBOL_LIST, "Shown", "0", &count_label);
    add_info_row(card, LV_SYMBOL_BLUETOOTH, "All Seen", "0", &total_label);
    add_info_row(card, LV_SYMBOL_LIST, "Named", "0", &named_label);
    add_info_row(card, LV_SYMBOL_WARNING, "Strongest", "--", &strongest_label);

    scan_label = lv_label_create(card);
    lv_label_set_text(scan_label, "No scan yet.");
    lv_obj_set_style_text_color(scan_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(scan_label, &lv_font_montserrat_12, 0);

    card = ui_create_card(page_container, "Controls");
    ui_create_card_button(card, LV_SYMBOL_REFRESH, "Scan", "Now", refresh_btn_cb);
    active_switch = card_switch(card, LV_SYMBOL_BLUETOOTH, "Active Scan", active_scan_enabled, active_switch_cb);
    named_filter_switch = card_switch(card, LV_SYMBOL_LIST, "Named Only", named_filter_enabled, named_filter_cb);
    auto_switch = card_switch(card, LV_SYMBOL_REFRESH, "Auto Refresh", auto_scan_enabled, auto_switch_cb);
    interval_dd = card_dropdown(card, LV_SYMBOL_SETTINGS, "Interval", "15s\n30s\n60s", 1, interval_cb);
    ui_create_card_button(card, LV_SYMBOL_CLOSE, "Clear", "Clear", clear_btn_cb);

    device_list_card = ui_create_card(page_container, "Nearby Devices");
    device_list_cont = create_device_list_container(device_list_card);
    lv_obj_t *empty = lv_label_create(device_list_cont);
    device_rows[0] = empty;
    lv_label_set_text(empty, named_filter_enabled ? "Scanning for named devices..." : "Scanning...");
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(empty, LV_PCT(100));
    lv_obj_set_style_text_color(empty, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);

    card = ui_create_card(page_container, "Selected Device");
    add_info_row(card, LV_SYMBOL_BLUETOOTH, "Name", "--", &selected_name_label);
    add_info_row(card, LV_SYMBOL_SETTINGS, "Address", "--", &selected_addr_label);
    add_info_row(card, LV_SYMBOL_WIFI, "Signal", "--", &selected_rssi_label);
    add_info_row(card, LV_SYMBOL_LIST, "Type", "--", &selected_type_label);
    add_info_row(card, LV_SYMBOL_REFRESH, "Seen", "--", &selected_seen_label);
    add_info_row(card, LV_SYMBOL_LIST, "Service", "--", &selected_service_label);
    add_info_row(card, LV_SYMBOL_SAVE, "Mfg Data", "--", &selected_mfg_label);
    add_info_row(card, LV_SYMBOL_SETTINGS, "Adv", "--", &selected_adv_label);

#if BLE_SCANNER_HAS_NIMBLE
    if (!device_mutex) {
        device_mutex = xSemaphoreCreateMutex();
    }
    clear_scan_devices();
    ble_scan_app_active = true;
    scan_end_reason = 0;
    scan_end_pending_status = false;
    scan_interval_ms = interval_from_dropdown();
    next_auto_scan_ms = 0;
    last_list_refresh_ms = millis();
    ui_timer = lv_timer_create(ui_timer_cb, BLE_SCAN_UI_PERIOD_MS, NULL);
    start_scan();
#else
    set_status("NimBLE observer role unavailable.", lv_color_hex(0xFF4444));
    lv_label_set_text(scan_label, "This build cannot scan BLE devices.");
    if (device_rows[0]) {
        lv_label_set_text(device_rows[0], "BLE scan is not available in this build.");
    }
#endif
}

void ui_ble_scanner_exit(lv_obj_t *parent)
{
    (void)parent;
    cleanup();
}

app_t ui_ble_scanner_main = {
    .setup_func_cb = ui_ble_scanner_enter,
    .exit_func_cb = ui_ble_scanner_exit,
    .user_data = nullptr,
};

#endif /* EXCLUDE_BLE_SCANNER */
