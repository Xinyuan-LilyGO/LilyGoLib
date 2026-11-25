/**
 * @file      ui_ble_scan_filter.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-25
 *
 * BLE scan visibility rules for signal strength, advertised names and search.
 */
#pragma once

struct UIBleScanFilter {
    int min_rssi = -127;
    bool with_name = false;
    char name[64] = {};

    bool matches(int rssi, const char *device_name) const
    {
        if (rssi < min_rssi) return false;
        if (!device_name) device_name = "";
        if (with_name && !device_name[0]) return false;
        if (!name[0]) return true;
        // ASCII case-insensitive substring search; UTF-8 bytes remain intact.
        for (auto *start = device_name; *start; ++start) {
            auto *text = start;
            auto *query = name;
            while (*text && *query && lower(*text) == lower(*query)) { ++text; ++query; }
            if (!*query) return true;
        }
        return false;
    }

private:
    static unsigned char lower(unsigned char ch)
    {
        return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
    }
};
