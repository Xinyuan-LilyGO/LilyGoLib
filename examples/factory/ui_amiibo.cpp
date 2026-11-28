/**
 * @file      ui_amiibo.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-25
 *
 * Factory Amiibo library and NTAG215 emulation page.  It reads NTAG215
 * .nfc files and raw 540-byte Amiibo backups using a persistent path index.
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#if defined(ARDUINO) && defined(USING_ST25R3916) && !defined(EXCLUDE_NFC_MFC) && \
    !defined(EXCLUDE_AMIIBO)

#include <SD.h>
#if defined(USING_FATFS)
#include <FFat.h>
#endif
#include <algorithm>
#include <vector>
#include "nfc/LilyGoNfcType2.h"

LV_IMG_DECLARE(img_nfc_card);

namespace {
constexpr uint16_t kAmiiboPages = 135;
constexpr size_t kMaxItems = 2048;
constexpr size_t kPageSize = 16;
constexpr uint8_t kScanEntriesPerTick = 8;
constexpr char kCachePath[] = "/amiibo/.amiibo_index.aidx";
constexpr char kCacheTempPath[] = "/amiibo/.amiibo_index.tmp";
constexpr char kCacheBackupPath[] = "/amiibo/.amiibo_index.old";
constexpr uint8_t kCacheVersion = 1;
constexpr size_t kCacheHeaderSize = 16;
constexpr uint8_t kCacheEntriesPerTick = 64;
struct AmiiboItem {
    char path[144] = {};
    char source[8] = {};
    char name[56] = {};
};

enum class LoadPhase : uint8_t {
    MountCheck,
    MountTry,
    CacheRead,
    CacheWrite,
    Scan,
    Ready,
    Failed,
};

struct ScanWork {
    fs::FS *filesystem = nullptr;
    char path[144] = {};
    char source[8] = {};
    uint8_t depth = 0;
};

static lv_obj_t *page = nullptr;
static lv_obj_t *preview = nullptr;
static lv_obj_t *list = nullptr;
static lv_obj_t *status = nullptr;
static lv_obj_t *prompt = nullptr;
static lv_obj_t *loading_spinner = nullptr;
static lv_obj_t *stop_button = nullptr;
static lv_obj_t *pager_label = nullptr;
static lv_obj_t *count_label = nullptr;
static lv_obj_t *navigation = nullptr;
static lv_obj_t *settings_button = nullptr;
static lv_obj_t *settings_page = nullptr;
static size_t list_page = 0;
static std::vector<uint8_t> image_bytes;
static lv_image_dsc_t image_descriptor = {};
static lv_timer_t *timer = nullptr;
static std::vector<AmiiboItem> items;
static LilyGoNfcType2Dump active_dump = {};
static int selected = -1;
static bool running = false;
static lv_event_cb_t return_callback = nullptr;
static LoadPhase load_phase = LoadPhase::MountCheck;
static bool sd_ready = false;
static uint32_t mount_frequencies[8] = {};
static uint8_t mount_count = 0;
static uint8_t mount_index = 0;
static std::vector<ScanWork> scan_queue;
static File scan_directory;
static fs::FS *scan_filesystem = nullptr;
static uint8_t scan_depth = 0;
static char scan_source[8] = {};
static bool scan_directory_open = false;
static uint32_t scanned_files = 0;
static bool force_rescan = false;
static uint8_t storage_index = 0;
static fs::FS *storage_fs = nullptr;
static const char *storage_source = "";
static size_t storage_item_begin = 0;
static bool scan_complete = true;
static File cache_file;
static uint32_t cache_count = 0;
static uint32_t cache_cursor = 0;
static uint32_t cache_hash = 0;
static uint32_t cache_expected_hash = 0;

static void set_label(lv_obj_t *label, const char *text, lv_color_t color = UI_COLOR_TEXT_PRIMARY);
static void build_list();
static void refresh_preview(int index);
static void close_prompt();
static void display_items_ready();
static void begin_storage_load(bool refresh_cache);
static void open_settings(lv_event_t *event);
static void next_storage();

static void dismiss_prompt_cb(lv_event_t *)
{
    close_prompt();
}

static bool has_extension(const char *path, const char *extension)
{
    const char *dot = path ? strrchr(path, '.') : nullptr;
    return dot && !strcasecmp(dot, extension);
}

static bool is_art_directory(const char *path)
{
    if (!path) return false;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return !strcasecmp(base, "images");
}

static void copy_name(char *out, size_t capacity, const char *path)
{
    const char *base = path ? strrchr(path, '/') : nullptr;
    base = base ? base + 1 : (path ? path : "Amiibo");
    snprintf(out, capacity, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    for (char *p = out; *p; ++p) if (*p == '_') *p = ' ';
}

static bool parse_bytes(const String &value, uint8_t *out, size_t count)
{
    char text[128] = {};
    value.substring(0, sizeof(text) - 1).toCharArray(text, sizeof(text));
    char *save = nullptr;
    char *token = strtok_r(text, " \t\r\n", &save);
    size_t index = 0;
    while (token && index < count) {
        char *end = nullptr;
        const long parsed = strtol(token, &end, 16);
        if (!end || *end || parsed < 0 || parsed > 255) return false;
        out[index++] = (uint8_t)parsed;
        token = strtok_r(nullptr, " \t\r\n", &save);
    }
    return index == count && token == nullptr;
}

static void init_dump(LilyGoNfcType2Dump &dump)
{
    dump = {};
    dump.uidLen = 7;
    dump.atqa[0] = 0x44;
    dump.atqa[1] = 0x00;
    dump.sak = 0;
    dump.model = LILYGO_NFC_TYPE2_NTAG215;
    dump.pageCount = kAmiiboPages;
    dump.geometryKnown = true;
    dump.versionValid = true;
    const uint8_t version[8] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x11, 0x03};
    memcpy(dump.version, version, sizeof(version));
}

static bool load_bin(File &file, LilyGoNfcType2Dump &dump)
{
    if (file.size() != kAmiiboPages * 4U && file.size() != 572U) return false;
    init_dump(dump);
    uint8_t data[kAmiiboPages * 4U];
    if (file.read(data, sizeof(data)) != sizeof(data)) return false;
    for (uint16_t page_index = 0; page_index < kAmiiboPages; ++page_index) {
        memcpy(dump.pages[page_index], data + page_index * 4U, 4);
        dump.pageRead[page_index] = true;
    }
    memcpy(dump.uid, dump.pages[0], 3);
    memcpy(dump.uid + 3, dump.pages[1], 4);
    dump.pagesRead = kAmiiboPages;
    return lilygoNfcType2CanEmulate(dump);
}

static bool load_text(File &file, LilyGoNfcType2Dump &dump)
{
    if (file.size() > 16384U) return false;
    init_dump(dump);
    bool have_uid = false;
    bool have_version = false;
    uint16_t total = 0;
    bool have_atqa = false, have_sak = false;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        const int separator = line.indexOf(':');
        if (separator < 0) continue;
        const String key = line.substring(0, separator);
        const String value = line.substring(separator + 1);
        if (key == "UID") have_uid = parse_bytes(value, dump.uid, 7);
        else if (key == "ATQA") have_atqa = parse_bytes(value, dump.atqa, 2);
        else if (key == "SAK") {
            uint8_t byte = 0;
            have_sak = parse_bytes(value, &byte, 1); dump.sak = byte;
        } else if (key == "Mifare version") {
            have_version = parse_bytes(value, dump.version, 8);
            dump.versionValid = have_version;
        } else if (key == "Pages total") {
            String number = value; number.trim();
            if (number != "135") return false;
            total = kAmiiboPages;
        } else if (key.startsWith("Page ")) {
            const String number = key.substring(5);
            char *end = nullptr;
            const long page_index = strtol(number.c_str(), &end, 10);
            if (!end || end == number.c_str() || *end) return false;
            if (page_index < 0 || page_index >= kAmiiboPages || dump.pageRead[page_index] ||
                    !parse_bytes(value, dump.pages[page_index], 4)) return false;
            dump.pageRead[page_index] = true;
        }
    }
    dump.pageCount = total ? total : kAmiiboPages;
    if (dump.pageCount != kAmiiboPages || total != kAmiiboPages || !have_uid || !have_version || !have_atqa || !have_sak) return false;
    dump.model = lilygoNfcType2Identify(dump.version, &dump.pageCount);
    dump.geometryKnown = dump.model == LILYGO_NFC_TYPE2_NTAG215;
    dump.pagesRead = 0;
    for (uint16_t i = 0; i < dump.pageCount; ++i) if (dump.pageRead[i]) ++dump.pagesRead;
    // Text exports can carry a UID that accidentally includes BCC0 and
    // omits the final serial byte. Manufacturer pages are authoritative;
    // CanEmulate still verifies both BCC bytes without altering source pages.
    if (!dump.pageRead[0] || !dump.pageRead[1]) return false;
    memcpy(dump.uid, dump.pages[0], 3);
    memcpy(dump.uid + 3, dump.pages[1], 4);
    // Text exports use both numeric and wire byte order for the same ATQA.
    // Normalize only the known NTAG215 value; reject other identities below.
    if (dump.atqa[0] == 0x00 && dump.atqa[1] == 0x44) {
        dump.atqa[0] = 0x44;
        dump.atqa[1] = 0x00;
    }
    return lilygoNfcType2CanEmulate(dump);
}

static void enqueue_scan(fs::FS &filesystem, const char *path, const char *source, uint8_t depth)
{
    if (!path || !*path || depth > 4) return;
    if (scan_queue.size() >= 64) {
        scan_complete = false;
        LILYGO_LOG_W("[Amiibo] scan queue full, skipping source:%s path:%s\n",
                     source ? source : "", path);
        return;
    }
    ScanWork work;
    work.filesystem = &filesystem;
    snprintf(work.path, sizeof(work.path), "%s", path);
    snprintf(work.source, sizeof(work.source), "%s", source ? source : "");
    work.depth = depth;
    scan_queue.push_back(work);
    LILYGO_LOG_PRINTF("[Amiibo] queued directory source:%s path:%s depth:%u\n",
                      work.source, work.path, unsigned(work.depth));
}

static void close_scan_directory()
{
    if (scan_directory_open) scan_directory.close();
    scan_directory_open = false;
    scan_filesystem = nullptr;
    scan_depth = 0;
    scan_source[0] = '\0';
}

static bool open_next_scan_directory()
{
    while (!scan_directory_open && !scan_queue.empty()) {
        const ScanWork work = scan_queue.back();
        scan_queue.pop_back();
        if (!work.filesystem) continue;
        scan_directory = work.filesystem->open(work.path, FILE_READ);
        if (!scan_directory || !scan_directory.isDirectory()) {
            LILYGO_LOG_W("[Amiibo] directory open failed source:%s path:%s\n", work.source, work.path);
            scan_directory.close();
            continue;
        }
        scan_filesystem = work.filesystem;
        scan_depth = work.depth;
        snprintf(scan_source, sizeof(scan_source), "%s", work.source);
        scan_directory_open = true;
        LILYGO_LOG_I("[Amiibo] scanning directory source:%s path:%s\n", work.source, work.path);
    }
    return scan_directory_open;
}

static bool scan_one_entry()
{
    if (items.size() >= kMaxItems) {
        scan_complete = false;
        close_scan_directory();
        scan_queue.clear();
        return false;
    }
    if (!open_next_scan_directory()) return false;
    File entry = scan_directory.openNextFile();
    if (!entry) {
        close_scan_directory();
        return !scan_queue.empty();
    }
    const char *entry_path = entry.path();
    if (entry.isDirectory()) {
        char child[144] = {};
        snprintf(child, sizeof(child), "%s", entry_path ? entry_path : "");
        const uint8_t child_depth = scan_depth + 1;
        entry.close();
        if (is_art_directory(child)) {
            LILYGO_LOG_I("[Amiibo] skipping artwork directory path:%s\n", child);
        } else {
            enqueue_scan(*scan_filesystem, child, scan_source, child_depth);
        }
    } else if (entry_path && (has_extension(entry_path, ".bin") || has_extension(entry_path, ".nfc"))) {
        AmiiboItem item;
        if (strlen(entry_path) >= sizeof(item.path)) {
            scan_complete = false;
            LILYGO_LOG_W("[Amiibo] path exceeds index limit source:%s", scan_source);
            entry.close();
            return true;
        }
        snprintf(item.path, sizeof(item.path), "%s", entry_path);
        snprintf(item.source, sizeof(item.source), "%s", scan_source);
        copy_name(item.name, sizeof(item.name), entry_path);
        items.push_back(item);
        ++scanned_files;
        if ((items.size() & 0x1fU) == 0) {
            LILYGO_LOG_I("[Amiibo] scan progress source:%s files:%lu\n",
                         item.source, (unsigned long)items.size());
        }
        entry.close();
    } else {
        entry.close();
    }
    if (scanned_files && (scanned_files & 0x0fU) == 0 && status) {
        char message[64] = {};
        snprintf(message, sizeof(message), "Scanning Amiibo (%lu)", (unsigned long)scanned_files);
        set_label(status, message, UI_COLOR_TEXT_SECONDARY);
        if (count_label) {
            char count[20] = {};
            snprintf(count, sizeof(count), "Total %lu", (unsigned long)items.size());
            set_label(count_label, count, UI_COLOR_TEXT_SECONDARY);
        }
    }
    return true;
}

static void reset_scan()
{
    close_scan_directory();
    scan_queue.clear();
    scanned_files = 0;
}

static void queue_storage_scan()
{
    reset_scan();
    scan_complete = true;
    enqueue_scan(*storage_fs, "/amiibo", storage_source, 0);
    enqueue_scan(*storage_fs, "/Amiibo NFC", storage_source, 0);
    if (storage_fs == &SD) enqueue_scan(SD, "/Amiibo Bin", "SD", 0);
    load_phase = LoadPhase::Scan;
    set_label(status, "Scanning Amiibo...", UI_COLOR_TEXT_SECONDARY);
    LILYGO_LOG_I("[Amiibo] storage scan started source:%s queued:%u", storage_source, unsigned(scan_queue.size()));
}

static uint32_t cache_u32(const uint8_t *bytes)
{
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
           (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

static void cache_put_u32(uint8_t *bytes, uint32_t value)
{
    for (uint8_t i = 0; i < 4; ++i) bytes[i] = uint8_t(value >> (8 * i));
}

static void cache_hash_bytes(const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) cache_hash = (cache_hash ^ bytes[i]) * 16777619U;
}

static bool cache_path_valid(const char *path)
{
    if (!path || strstr(path, "/../") || strstr(path, "/./")) return false;
    const bool root = !strncmp(path, "/amiibo/", 8) || !strncmp(path, "/Amiibo NFC/", 12) ||
                      (storage_fs == &SD && !strncmp(path, "/Amiibo Bin/", 12));
    return root && (has_extension(path, ".nfc") || has_extension(path, ".bin"));
}

static bool begin_cache_read()
{
    // Recover the previous complete index if replacement was interrupted.
    if (!storage_fs->exists(kCachePath) && storage_fs->exists(kCacheBackupPath))
        storage_fs->rename(kCacheBackupPath, kCachePath);
    cache_file = storage_fs->open(kCachePath, FILE_READ);
    if (!cache_file) return false;
    uint8_t header[kCacheHeaderSize] = {};
    if (cache_file.isDirectory() || cache_file.read(header, sizeof(header)) != sizeof(header) ||
            memcmp(header, "AIDX", 4) || header[4] != kCacheVersion || header[5] || header[6] || header[7]) {
        cache_file.close();
        LILYGO_LOG_W("[Amiibo] invalid cache header source:%s", storage_source);
        return false;
    }
    cache_count = cache_u32(header + 8);
    cache_expected_hash = cache_u32(header + 12);
    const size_t size = cache_file.size();
    if (cache_count > kMaxItems || size < kCacheHeaderSize + cache_count * 3U ||
            size > kCacheHeaderSize + cache_count * (sizeof(AmiiboItem::path) + 1U)) {
        cache_file.close();
        LILYGO_LOG_W("[Amiibo] invalid cache size/count source:%s", storage_source);
        return false;
    }
    cache_cursor = 0;
    cache_hash = 2166136261U;
    load_phase = LoadPhase::CacheRead;
    set_label(status, "Loading library...", UI_COLOR_TEXT_SECONDARY);
    LILYGO_LOG_I("[Amiibo] loading cache source:%s records:%lu bytes:%u",
                 storage_source, (unsigned long)cache_count, unsigned(size));
    return true;
}

static void cache_read_failed()
{
    cache_file.close();
    items.resize(storage_item_begin);
    LILYGO_LOG_W("[Amiibo] cache corrupt source:%s record:%lu; rebuilding", storage_source, (unsigned long)cache_cursor);
    queue_storage_scan();
}

static void cache_read_tick()
{
    const uint32_t started = lv_tick_get();
    for (uint8_t i = 0; i < kCacheEntriesPerTick && cache_cursor < cache_count; ++i) {
        uint8_t length_bytes[2] = {};
        AmiiboItem item;
        if (cache_file.read(length_bytes, 2) != 2) { cache_read_failed(); return; }
        const size_t length = size_t(length_bytes[0]) | (size_t(length_bytes[1]) << 8);
        if (!length || length >= sizeof(item.path) ||
                cache_file.read(reinterpret_cast<uint8_t *>(item.path), length) != length ||
                memchr(item.path, '\0', length) || !cache_path_valid(item.path)) {
            cache_read_failed(); return;
        }
        cache_hash_bytes(length_bytes, 2);
        cache_hash_bytes(reinterpret_cast<const uint8_t *>(item.path), length);
        snprintf(item.source, sizeof(item.source), "%s", storage_source);
        copy_name(item.name, sizeof(item.name), item.path);
        if (items.size() < kMaxItems) items.push_back(item);
        ++cache_cursor;
        if (lv_tick_elaps(started) >= 12) break;
    }
    if (cache_cursor != cache_count) return;
    if (cache_hash != cache_expected_hash || cache_file.position() != cache_file.size()) {
        cache_read_failed(); return;
    }
    cache_file.close();
    LILYGO_LOG_I("[Amiibo] cache loaded source:%s records:%lu; directory scan skipped", storage_source, (unsigned long)cache_count);
    next_storage();
}

static void cache_write_failed()
{
    cache_file.close();
    storage_fs->remove(kCacheTempPath);
    LILYGO_LOG_W("[Amiibo] cache write failed source:%s; keeping current list and previous index", storage_source);
    next_storage();
}

static void cache_write_tick()
{
    const uint32_t started = lv_tick_get();
    for (uint8_t i = 0; i < kCacheEntriesPerTick && cache_cursor < cache_count; ++i) {
        const char *path = items[storage_item_begin + cache_cursor].path;
        const size_t length = strlen(path);
        const uint8_t length_bytes[2] = {uint8_t(length), uint8_t(length >> 8)};
        if (cache_file.write(length_bytes, 2) != 2 ||
                cache_file.write(reinterpret_cast<const uint8_t *>(path), length) != length) {
            cache_write_failed(); return;
        }
        cache_hash_bytes(length_bytes, 2);
        cache_hash_bytes(reinterpret_cast<const uint8_t *>(path), length);
        ++cache_cursor;
        if (lv_tick_elaps(started) >= 12) break;
    }
    if (cache_cursor != cache_count) return;
    uint8_t hash_bytes[4] = {};
    cache_put_u32(hash_bytes, cache_hash);
    if (!cache_file.seek(12) || cache_file.write(hash_bytes, 4) != 4) { cache_write_failed(); return; }
    cache_file.flush();
    cache_file.close();
    storage_fs->remove(kCacheBackupPath);
    const bool had_cache = storage_fs->exists(kCachePath);
    if (had_cache && !storage_fs->rename(kCachePath, kCacheBackupPath)) { cache_write_failed(); return; }
    if (!storage_fs->rename(kCacheTempPath, kCachePath)) {
        if (had_cache) storage_fs->rename(kCacheBackupPath, kCachePath);
        cache_write_failed(); return;
    }
    storage_fs->remove(kCacheBackupPath);
    LILYGO_LOG_I("[Amiibo] cache saved source:%s records:%lu path:%s", storage_source, (unsigned long)cache_count, kCachePath);
    next_storage();
}

static void finish_scan()
{
    close_scan_directory();
    scan_queue.clear();
    LILYGO_LOG_I("[Amiibo] scan finished source:%s files:%lu complete:%u", storage_source,
                 (unsigned long)scanned_files, scan_complete ? 1U : 0U);
    if (!scan_complete || (storage_fs == &SD && !hw_is_sd_insert())) { next_storage(); return; }
    if (!storage_fs->exists("/amiibo") && !storage_fs->mkdir("/amiibo")) {
        LILYGO_LOG_W("[Amiibo] cannot create cache directory source:%s", storage_source);
        next_storage(); return;
    }
    storage_fs->remove(kCacheTempPath);
    cache_file = storage_fs->open(kCacheTempPath, FILE_WRITE);
    cache_count = items.size() - storage_item_begin;
    cache_cursor = 0;
    cache_hash = 2166136261U;
    uint8_t header[kCacheHeaderSize] = {'A', 'I', 'D', 'X', kCacheVersion};
    cache_put_u32(header + 8, cache_count);
    if (!cache_file || cache_file.write(header, sizeof(header)) != sizeof(header)) { cache_write_failed(); return; }
    set_label(status, "Saving library...", UI_COLOR_TEXT_SECONDARY);
    load_phase = LoadPhase::CacheWrite;
}

static void next_storage()
{
    storage_fs = nullptr;
    while (storage_index < 2) {
        const uint8_t current = storage_index++;
        if (current == 0 && sd_ready) { storage_fs = &SD; storage_source = "SD"; }
#if defined(USING_FATFS)
        if (current == 1 && FFat.totalBytes() > 0) { storage_fs = &FFat; storage_source = "FAT"; }
#endif
        if (!storage_fs) continue;
        storage_item_begin = items.size();
        if (!force_rescan && begin_cache_read()) return;
        queue_storage_scan();
        return;
    }
    display_items_ready();
}

static void display_items_ready()
{
    if (items.empty() && sd_ready && !hw_is_sd_insert()) {
        sd_ready = false;
        LILYGO_LOG_W("[Amiibo] SD became unavailable during storage scan\n");
    }
    std::sort(items.begin(), items.end(), [](const AmiiboItem &a, const AmiiboItem &b) {
        return strcasecmp(a.path, b.path) < 0;
    });
    load_phase = items.empty() ? LoadPhase::Failed : LoadPhase::Ready;
    if (loading_spinner) lv_obj_add_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(navigation, LV_OBJ_FLAG_HIDDEN);
    if (settings_button) lv_obj_remove_state(settings_button, LV_STATE_DISABLED);
    build_list();
    if (count_label) {
        char count[20] = {};
        snprintf(count, sizeof(count), "Total %u", unsigned(items.size()));
        set_label(count_label, count, UI_COLOR_ACCENT);
    }
    LILYGO_LOG_I("[Amiibo] library ready items:%u sd_ready:%u", unsigned(items.size()), sd_ready ? 1U : 0U);
    if (items.empty()) {
        if (!sd_ready) {
            set_label(status, "No SD card detected", UI_COLOR_WARNING);
            prompt = create_msgbox(page, "Amiibo storage",
                                   "No SD card detected.\nInsert a card and reopen Amiibo.",
                                   nullptr, dismiss_prompt_cb, nullptr);
            LILYGO_LOG_W("[Amiibo] no SD card and no Amiibo files found\n");
        } else {
            set_label(status, "No Amiibo files found", UI_COLOR_WARNING);
        }
        return;
    }
    set_label(status, "");
    refresh_preview(0);
}

static void mount_tick()
{
    if (hw_is_sd_insert() && hw_get_sd_size() > 0.0f) {
        sd_ready = true;
        LILYGO_LOG_I("[Amiibo] SD already mounted size:%.2f\n", double(hw_get_sd_size()));
        next_storage();
        return;
    }
    if (mount_count == 0) {
        mount_count = hw_get_sd_mount_freq_list(mount_frequencies, 8);
        mount_index = 0;
        LILYGO_LOG_I("[Amiibo] SD mount candidates:%u card_inserted:%u\n",
                     unsigned(mount_count), hw_is_sd_card_inserted() ? 1U : 0U);
        if (mount_count == 0) {
            sd_ready = false;
            next_storage();
            return;
        }
    }
    if (mount_index >= mount_count) {
        sd_ready = false;
        LILYGO_LOG_W("[Amiibo] SD mount failed after %u attempts\n", unsigned(mount_count));
        next_storage();
        return;
    }
    const uint32_t frequency = mount_frequencies[mount_index++];
    char message[64] = {};
    snprintf(message, sizeof(message), "Mounting SD %u/%u", unsigned(mount_index), unsigned(mount_count));
    set_label(status, message, UI_COLOR_ACCENT);
    lv_refr_now(nullptr);
    const bool mounted = frequency && hw_mount_sd(frequency);
    const bool ready = mounted && hw_is_sd_insert() && hw_get_sd_size() > 0.0f;
    LILYGO_LOG_I("[Amiibo] SD mount attempt:%u/%u freq:%lu mounted:%u ready:%u\n",
                 unsigned(mount_index), unsigned(mount_count), (unsigned long)frequency,
                 mounted ? 1U : 0U, ready ? 1U : 0U);
    if (ready) {
        sd_ready = true;
        next_storage();
    }
}

static void set_label(lv_obj_t *label, const char *text, lv_color_t color)
{
    if (!label) return;
    lv_label_set_text(label, text ? text : "--");
    lv_obj_set_style_text_color(label, color, 0);
    if (label == status) {
        if (text && text[0]) lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void focus_style(lv_obj_t *object)
{
    ui_add_accent_focus_style(object);
    lv_obj_set_style_radius(object, 7, 0);
    lv_obj_add_flag(object, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
}

static void stop_emulation()
{
    if (running || LilyGoNfc.isRunning()) {
        hw_stop_nfc();
        running = false;
        LILYGO_LOG_I("[Amiibo] emulation stopped\n");
    }
}

static fs::FS *item_fs(int index)
{
    if (index < 0 || index >= (int)items.size()) return nullptr;
    if (!strcmp(items[index].source, "SD")) return &SD;
#if defined(USING_FATFS)
    return &FFat;
#else
    return nullptr;
#endif
}

static bool load_item(int index)
{
    if (index < 0 || index >= (int)items.size()) return false;
    LILYGO_LOG_I("[Amiibo] loading selected index:%d source:%s path:%s\n",
                 index, items[index].source, items[index].path);
    fs::FS *filesystem = item_fs(index);
    if (!filesystem) {
        LILYGO_LOG_E("[Amiibo] storage unavailable source:%s\n", items[index].source);
        return false;
    }
    File file = filesystem->open(items[index].path, FILE_READ);
    if (!file || file.isDirectory()) {
        LILYGO_LOG_E("[Amiibo] file open failed source:%s path:%s\n", items[index].source, items[index].path);
        return false;
    }
    active_dump = {};
    const bool valid = has_extension(items[index].path, ".bin") ? load_bin(file, active_dump) : load_text(file, active_dump);
    if (!valid || !lilygoNfcType2DumpComplete(active_dump)) {
        LILYGO_LOG_W("[Amiibo] invalid NTAG215 dump path:%s valid:%u pages:%u/%u\n",
                     items[index].path, valid ? 1U : 0U,
                     unsigned(active_dump.pagesRead), unsigned(active_dump.pageCount));
        return false;
    }
    // Amiibo uses the UID-derived NTAG215 password. Masked credential pages
    // in read backups are not literal zero passwords; derive only in this app.
    const uint8_t *uid = active_dump.uid;
    active_dump.password[0] = 0xAA ^ uid[1] ^ uid[3];
    active_dump.password[1] = 0x55 ^ uid[2] ^ uid[4];
    active_dump.password[2] = 0xAA ^ uid[3] ^ uid[5];
    active_dump.password[3] = 0x55 ^ uid[4] ^ uid[6];
    active_dump.pack[0] = active_dump.pack[1] = 0x80;
    active_dump.credentialsKnown = true;
    LILYGO_LOG_I("[Amiibo] selected dump loaded uid:%02x%02x%02x%02x%02x%02x%02x pages:%u\n",
                 active_dump.uid[0], active_dump.uid[1], active_dump.uid[2], active_dump.uid[3],
                 active_dump.uid[4], active_dump.uid[5], active_dump.uid[6], unsigned(active_dump.pagesRead));
    return true;
}

static void refresh_preview(int index)
{
    if (load_phase != LoadPhase::Ready || running || index < 0 || index >= (int)items.size()) return;
    selected = index;
    lv_image_set_src(preview, &img_nfc_card);
    lv_image_cache_drop(&image_descriptor);
    image_bytes.clear();
    set_label(status, "");
    const bool valid = load_item(index);
    if (!valid) { set_label(status, "File unavailable. Refresh cache.", UI_COLOR_WARNING); return; }
    // Role art is keyed by the eight-byte character ID in pages 21 and 22.
    char art_path[80];
    snprintf(art_path, sizeof(art_path), "/amiibo/images/%02x%02x%02x%02x-%02x%02x%02x%02x.png",
             active_dump.pages[21][0], active_dump.pages[21][1], active_dump.pages[21][2], active_dump.pages[21][3],
             active_dump.pages[22][0], active_dump.pages[22][1], active_dump.pages[22][2], active_dump.pages[22][3]);
    File art = item_fs(index)->open(art_path, FILE_READ);
    if (!art || art.size() < 24 || art.size() > 128 * 1024) return;
    image_bytes.resize(art.size());
    if (art.read(image_bytes.data(), image_bytes.size()) != image_bytes.size()) { image_bytes.clear(); return; }
    const auto *b = image_bytes.data();
    if (memcmp(b, "\x89PNG\r\n\x1a\n", 8)) { image_bytes.clear(); return; }
    const uint32_t width = (uint32_t(b[16]) << 24) | (uint32_t(b[17]) << 16) | (uint32_t(b[18]) << 8) | b[19];
    const uint32_t height = (uint32_t(b[20]) << 24) | (uint32_t(b[21]) << 16) | (uint32_t(b[22]) << 8) | b[23];
    if (!width || !height || width > 512 || height > 512) return;
    image_descriptor = {};
    image_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_descriptor.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    image_descriptor.header.w = width;
    image_descriptor.header.h = height;
    image_descriptor.data = b;
    image_descriptor.data_size = image_bytes.size();
    lv_image_set_src(preview, &image_descriptor);
}

static void close_prompt()
{
    if (prompt) {
        lv_obj_t *box = prompt;
        prompt = nullptr;
        destroy_msgbox(box);
    }
}

static void start_selected()
{
    if (selected < 0 || selected >= (int)items.size() || running) return;
    LILYGO_LOG_I("[Amiibo] emulation requested index:%d\n", selected);
    if (!load_item(selected)) {
        set_label(status, "Invalid NTAG215 image", UI_COLOR_WARNING);
        LILYGO_LOG_E("[Amiibo] emulation aborted because selected dump failed\n");
        return;
    }
    LilyGoNfcEmulationConfig config = {};
    config.kind = LILYGO_NFC_EMULATION_URL;
    config.tech = LILYGO_NFC_EMULATION_TECH_NFCA_T2T;
    config.templateName = items[selected].name;
    config.type2Dump = &active_dump;
    config.discoveryDurationMs = 1000;
    if (!hw_start_nfc_emulation(config)) {
        hw_stop_nfc();
        set_label(status, "Amiibo emulation unavailable", UI_COLOR_WARNING);
        LILYGO_LOG_E("[Amiibo] NFC emulation start failed\n");
        return;
    }
    running = true;
    lv_obj_remove_flag(stop_button, LV_OBJ_FLAG_HIDDEN);
    lv_group_focus_obj(stop_button);
    set_label(status, "Emulating", UI_COLOR_ACCENT);
    LILYGO_LOG_I("[Amiibo] emulation started index:%d\n", selected);
}

static void confirm_cb(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target_obj(event);
    lv_obj_t *label = button ? lv_obj_get_child(button, 0) : nullptr;
    const bool yes = label && !strcmp(lv_label_get_text(label), "Yes");
    close_prompt();
    if (yes) start_selected();
}

static void item_clicked(lv_event_t *event)
{
    const int index = (int)(uintptr_t)lv_event_get_user_data(event);
    if (running || index < 0 || index >= (int)items.size()) return;
    refresh_preview(index);
    char message[128] = {};
    snprintf(message, sizeof(message), "Emulate %s?\nThe NTAG215 image will be presented over NFC.", items[index].name);
    static const char *buttons[] = {"Yes", "No", ""};
    prompt = create_msgbox(page, "Amiibo", message, buttons, confirm_cb, nullptr);
}

static void begin_storage_load(bool refresh_cache)
{
    close_prompt();
    stop_emulation();
    cache_file.close();
    reset_scan();
    load_phase = LoadPhase::MountCheck;
    force_rescan = refresh_cache;
    sd_ready = false;
    storage_index = 0;
    storage_fs = nullptr;
    storage_item_begin = 0;
    mount_count = mount_index = 0;
    selected = -1;
    list_page = 0;
    // Remove focused controls before replacing the data their callbacks use.
    lv_obj_clean(list);
    items.clear();
    lv_image_set_src(preview, &img_nfc_card);
    lv_image_cache_drop(&image_descriptor);
    image_bytes.clear();
    lv_obj_add_flag(stop_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(navigation, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(settings_button, LV_STATE_DISABLED);
    set_label(count_label, "Total 0", UI_COLOR_TEXT_SECONDARY);
    set_label(status, refresh_cache ? "Refreshing library..." : "Opening library...", UI_COLOR_ACCENT);
    LILYGO_LOG_I("[Amiibo] library requested refresh:%u cache:%s", refresh_cache ? 1U : 0U, kCachePath);
}

static void refresh_cache_cb(lv_event_t *)
{
    if (settings_page) ui_destroy_settings_page(settings_page);
    begin_storage_load(true);
}

static void open_settings(lv_event_t *)
{
    if (settings_page || (load_phase != LoadPhase::Ready && load_phase != LoadPhase::Failed)) return;
    settings_page = ui_create_settings_page(page, "Amiibo settings");
    lv_obj_add_event_cb(settings_page, [](lv_event_t *) { settings_page = nullptr; }, LV_EVENT_DELETE, nullptr);
    auto *caption = lv_label_create(settings_page);
    lv_obj_set_width(caption, LV_PCT(100));
    lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
    lv_label_set_text(caption, "Refresh after adding, moving or deleting Amiibo files.");
    lv_obj_set_style_text_color(caption, UI_COLOR_TEXT_PRIMARY, 0);
    auto *button = lv_button_create(settings_page);
    lv_obj_set_size(button, LV_PCT(100), 34);
    lv_obj_set_style_bg_color(button, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_text_color(button, UI_COLOR_TEXT_PRIMARY, 0);
    focus_style(button);
    auto *label = lv_label_create(button);
    lv_label_set_text(label, LV_SYMBOL_REFRESH " Refresh cache");
    lv_obj_center(label);
    lv_obj_add_event_cb(button, refresh_cache_cb, LV_EVENT_CLICKED, nullptr);
    if (auto *group = lv_group_get_default()) lv_group_add_obj(group, button);
    lv_group_focus_obj(button);
}

static void cleanup()
{
    if (settings_page) ui_destroy_settings_page(settings_page);
    close_prompt();
    stop_emulation();
    if (timer) { lv_timer_del(timer); timer = nullptr; }
    cache_file.close();
    reset_scan();
    load_phase = LoadPhase::MountCheck;
    sd_ready = false;
    storage_fs = nullptr;
    storage_index = 0;
    force_rescan = false;
    std::vector<ScanWork>().swap(scan_queue);
    mount_count = mount_index = 0;
    memset(mount_frequencies, 0, sizeof(mount_frequencies));
    std::vector<AmiiboItem>().swap(items);
    if (preview) lv_image_set_src(preview, &img_nfc_card);
    lv_image_cache_drop(&image_descriptor);
    std::vector<uint8_t>().swap(image_bytes);
    selected = -1;
    if (page) { ui_destroy_app_page(page); page = nullptr; }
    preview = list = status = loading_spinner = stop_button = pager_label = count_label = navigation = settings_button = nullptr;
    set_low_power_mode_flag(true);
}

static void back_cb(lv_event_t *event) { auto callback = return_callback; cleanup(); if (callback) callback(event); else menu_show(); }

static void poll(lv_timer_t *)
{
    if (load_phase == LoadPhase::CacheRead) { cache_read_tick(); return; }
    if (load_phase == LoadPhase::CacheWrite) { cache_write_tick(); return; }
    if (load_phase == LoadPhase::MountCheck || load_phase == LoadPhase::MountTry) {
        load_phase = LoadPhase::MountTry;
        mount_tick();
        return;
    }
    if (load_phase == LoadPhase::Scan) {
        bool progressed = false;
        for (uint8_t i = 0; i < kScanEntriesPerTick; ++i) {
            const bool step = scan_one_entry();
            progressed |= step;
            if (!scan_directory_open && scan_queue.empty()) break;
        }
        if (!progressed && !scan_directory_open && scan_queue.empty()) finish_scan();
        return;
    }
    if (!running) return;
    const LilyGoNfcEmulationStatus &state = LilyGoNfc.emulationStatus();
    if (state.state == LILYGO_NFC_STATE_ERROR) {
        hw_stop_nfc(); running = false;
        lv_obj_add_flag(stop_button, LV_OBJ_FLAG_HIDDEN);
        set_label(status, state.errorText[0] ? state.errorText : "Amiibo emulation failed", UI_COLOR_WARNING);
    } else {
        set_label(status, state.readerActive ? "Reader connected" : "Emulating", UI_COLOR_ACCENT);
    }
}

static lv_obj_t *make_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_set_width(panel, 0);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_style_bg_color(panel, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 4, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static void build_list()
{
    if (!list) return;
    lv_obj_clean(list);
    char page_text[32];
    snprintf(page_text, sizeof(page_text), "%u/%u", unsigned(list_page + 1), unsigned(std::max<size_t>(1, (items.size() + kPageSize - 1) / kPageSize)));
    set_label(pager_label, page_text);
    if (items.empty()) {
        lv_obj_t *empty = lv_label_create(list);
        lv_label_set_text(empty, "Copy .nfc/.bin\nto /amiibo");
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_color(empty, UI_COLOR_TEXT_SECONDARY, 0);
        return;
    }
    for (size_t i = list_page * kPageSize; i < std::min(items.size(), (list_page + 1) * kPageSize); ++i) {
        lv_obj_t *button = lv_btn_create(list);
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_set_height(button, 32);
        lv_obj_set_style_bg_color(button, UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_text_color(button, UI_COLOR_TEXT_PRIMARY, 0);
        focus_style(button);
        lv_obj_add_event_cb(button, item_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(button, [](lv_event_t *e) { refresh_preview((int)(uintptr_t)lv_event_get_user_data(e)); }, LV_EVENT_FOCUSED, (void *)(uintptr_t)i);
        if (auto *group = lv_group_get_default()) lv_group_add_obj(group, button);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, items[i].name);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_center(label);
    }
}

static void enter(lv_obj_t *parent)
{
    cleanup();
    set_low_power_mode_flag(false);
    page = ui_create_app_page(parent, "Amiibo", back_cb);
    settings_button = ui_app_page_add_header_button(page, LV_SYMBOL_SETTINGS, open_settings);
    lv_obj_set_style_text_font(page, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_all(page, 4, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 6, 0);
    lv_obj_t *body = lv_obj_create(page);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_height(body, 1);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_column(body, 6, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = make_panel(body);
    lv_obj_set_width(left, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(left, 0);
    lv_obj_set_width(left, LV_PCT(46));
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    preview = lv_image_create(left);
    lv_image_set_src(preview, &img_nfc_card);
    lv_obj_set_width(preview, LV_PCT(100));
    lv_obj_set_height(preview, 0); lv_obj_set_flex_grow(preview, 1);
    lv_image_set_inner_align(preview, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_t *right = make_panel(body);
    lv_obj_set_width(right, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 3, 0);
    status = lv_label_create(right);
    // Keep list rows and Stop reachable when an error exceeds one line.
    lv_label_set_long_mode(status, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(status, LV_PCT(100));
    loading_spinner = lv_spinner_create(right);
    lv_obj_set_size(loading_spinner, 28, 28);
    lv_obj_set_style_arc_color(loading_spinner, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(loading_spinner, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    list = lv_obj_create(right); lv_obj_set_width(list, LV_PCT(100)); lv_obj_set_height(list, 1); lv_obj_set_flex_grow(list, 1); lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(list, 0, 0); lv_obj_set_style_pad_all(list, 0, 0); lv_obj_set_style_pad_row(list, 3, 0); lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN); lv_obj_set_scroll_dir(list, LV_DIR_VER);

    auto *controls = lv_obj_create(right);
    navigation = controls;
    lv_obj_set_size(controls, LV_PCT(100), 26);
    lv_obj_set_style_pad_all(controls, 0, 0); lv_obj_set_style_border_width(controls, 0, 0);
    lv_obj_set_style_pad_column(controls, 2, 0);
    lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
    for (int direction : {-1, 1}) {
        auto *button = lv_button_create(controls);
        lv_obj_set_size(button, 24, 24); focus_style(button);
        lv_obj_set_style_pad_all(button, 0, 0);
        auto *label = lv_label_create(button); lv_label_set_text(label, direction < 0 ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT); lv_obj_center(label);
        lv_obj_add_event_cb(button, [](lv_event_t *e) {
            if (running || load_phase != LoadPhase::Ready) return;
            const int delta = (int)(intptr_t)lv_event_get_user_data(e);
            const int next = int(list_page) + delta;
            if (next >= 0 && size_t(next) * kPageSize < items.size()) { list_page = size_t(next); build_list(); }
        }, LV_EVENT_CLICKED, (void *)(intptr_t)direction);
        if (auto *group = lv_group_get_default()) lv_group_add_obj(group, button);
    }
    pager_label = lv_label_create(controls); lv_obj_set_flex_grow(pager_label, 1);
    lv_obj_set_width(pager_label, 0);
    lv_obj_set_style_text_align(pager_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(pager_label, LV_LABEL_LONG_DOT);
    auto *summary = lv_obj_create(right);
    lv_obj_set_size(summary, LV_PCT(100), 26);
    lv_obj_set_style_pad_all(summary, 0, 0);
    lv_obj_set_style_pad_column(summary, 2, 0);
    lv_obj_set_style_border_width(summary, 0, 0);
    lv_obj_set_style_bg_opa(summary, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(summary, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(summary, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(summary, LV_OBJ_FLAG_SCROLLABLE);
    stop_button = lv_button_create(summary); lv_obj_set_size(stop_button, 44, 24); focus_style(stop_button);
    lv_obj_set_style_pad_all(stop_button, 0, 0);
    auto *stop_label = lv_label_create(stop_button); lv_label_set_text(stop_label, "Stop"); lv_obj_center(stop_label);
    lv_obj_add_event_cb(stop_button, [](lv_event_t *) { stop_emulation(); set_label(status, "Stopped"); lv_obj_add_flag(stop_button, LV_OBJ_FLAG_HIDDEN); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(stop_button, LV_OBJ_FLAG_HIDDEN);
    if (auto *group = lv_group_get_default()) lv_group_add_obj(group, stop_button);
    count_label = lv_label_create(summary);
    lv_obj_set_width(count_label, 0); lv_obj_set_flex_grow(count_label, 1);
    lv_obj_set_style_text_align(count_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(count_label, LV_LABEL_LONG_DOT);
    begin_storage_load(false);
    timer = lv_timer_create(poll, 80, nullptr);
}

static void launcher_enter(lv_obj_t *parent) { return_callback = nullptr; enter(parent); }
static void exit(lv_obj_t *parent) { (void)parent; cleanup(); return_callback = nullptr; }
} // namespace

void ui_amiibo_open(lv_obj_t *parent, lv_event_cb_t back) { return_callback = back; enter(parent); }
void ui_amiibo_close() { cleanup(); return_callback = nullptr; }

app_t ui_amiibo_main = { .setup_func_cb = launcher_enter, .exit_func_cb = exit, .user_data = nullptr };

#else
void ui_amiibo_enter(lv_obj_t *) {}
void ui_amiibo_exit(lv_obj_t *) {}
app_t ui_amiibo_main = { .setup_func_cb = ui_amiibo_enter, .exit_func_cb = ui_amiibo_exit, .user_data = nullptr };
#endif
