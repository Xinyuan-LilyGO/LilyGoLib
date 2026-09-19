/**
 * @file      ui_nes.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-08-23
 * 
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#if !defined(EXCLUDE_NES)

#include <ctype.h>
#include <stdlib.h>
#include <strings.h>

#ifdef ARDUINO
#include <SD.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#endif

#if defined(ARDUINO) && (defined(ARDUINO_T_DECK_V2) || defined(ARDUINO_T_LORA_PAGER)) && __has_include(<nofrendo.h>)
#define NES_APP_HAS_NOFRENDO 1
#ifdef UNUSED
#undef UNUSED
#endif
extern "C" {
#include <bitmap.h>
#include <event.h>
#include <gui.h>
#include <log.h>
#include <nes/nes.h>
#include <nes/nes_pal.h>
#include <nes/nesinput.h>
#include <nofconfig.h>
#include <nofrendo.h>
#include <osd.h>
#include <vid_drv.h>
}
#else
#define NES_APP_HAS_NOFRENDO 0
#endif

#define NES_ROM_DIR         "/nes"
#define NES_VFS_PREFIX      "/sd"
#define NES_MAX_ROMS        32
#define NES_PATH_MAX_LEN    160
#define NES_FRAME_CHUNK_H   8
#define NES_AUDIO_SAMPLE_RATE 16000
#define NES_AUDIO_FRAG_SAMPLES 256
#define NES_AUDIO_RING_SAMPLES 4096
#define NES_AUDIO_PREFILL_SAMPLES 1024
#define NES_AUDIO_CHANNELS  2
#define NES_AUDIO_BITS      16
#define NES_AUDIO_GAIN_SHIFT 2
#define NES_AUDIO_DEBUG     0
#define NES_AUDIO_TEST_TONE 0
#define NES_AUDIO_TEST_TONE_MS 180
#define NES_AUDIO_TEST_TONE_HZ 600
#define NES_AUDIO_DEBUG_INTERVAL_MS 2000
#define NES_AUDIO_WRITE_SLOW_MS 25

typedef struct {
    char display_path[NES_PATH_MAX_LEN];
    char vfs_path[NES_PATH_MAX_LEN];
    uint64_t size;
} nes_rom_item_t;

static lv_obj_t *page_container = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *rom_count_label = NULL;
static lv_obj_t *rom_list_card = NULL;
static lv_timer_t *state_timer = NULL;
static nes_rom_item_t *rom_items = NULL;
static uint8_t rom_count = 0;

#if NES_APP_HAS_NOFRENDO
static TaskHandle_t nes_task_handle = NULL;
static TimerHandle_t nes_tick_timer = NULL;
static void (*nes_tick_func)(void) = NULL;
static char active_rom_path[NES_PATH_MAX_LEN];
static volatile bool emulator_running = false;
static volatile bool emulator_finished = false;
static volatile bool stop_requested = false;
static volatile int emulator_result = 0;
static volatile uint32_t controller_state = 0xFFFFFFFF;

static uint16_t *frame_buffers[2] = {NULL, NULL};
static int current_buffer = 0;
static size_t frame_buffer_pixels = 0;
static uint16_t deck_palette[256];
static int16_t frame_x = 0;
static int16_t frame_y = 0;
static int16_t frame_w = NES_SCREEN_WIDTH;
static int16_t frame_h = NES_SCREEN_HEIGHT;
static uint8_t dummy_fb[1];
static bitmap_t *dummy_bitmap = NULL;
static TaskHandle_t nes_audio_task_handle = NULL;
static AudioOutputIf *nes_audio_output = NULL;
static bool nes_audio_open = false;
static volatile bool nes_audio_task_running = false;
static void (*nes_audio_callback)(void *buffer, int length) = NULL;
static uint32_t nes_audio_sample_accum = 0;
static portMUX_TYPE nes_audio_mux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t nes_audio_ring_read = 0;
static uint16_t nes_audio_ring_write = 0;
static uint16_t nes_audio_ring_count = 0;
static int16_t nes_audio_mono[NES_AUDIO_FRAG_SAMPLES];
static int16_t nes_audio_stereo[NES_AUDIO_FRAG_SAMPLES * NES_AUDIO_CHANNELS];
static int16_t nes_audio_ring[NES_AUDIO_RING_SAMPLES * NES_AUDIO_CHANNELS];
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
static uint32_t nes_audio_debug_last_ms = 0;
static volatile uint32_t nes_audio_debug_produced = 0;
static volatile uint32_t nes_audio_debug_pushed = 0;
static volatile uint32_t nes_audio_debug_consumed = 0;
static volatile uint32_t nes_audio_debug_dropped = 0;
static volatile uint32_t nes_audio_debug_overflows = 0;
static volatile uint32_t nes_audio_debug_underruns = 0;
static volatile uint32_t nes_audio_debug_partial_reads = 0;
static volatile uint32_t nes_audio_debug_prefill_waits = 0;
static volatile uint32_t nes_audio_debug_frame_calls = 0;
static volatile uint32_t nes_audio_debug_callback_sets = 0;
static volatile uint32_t nes_audio_debug_not_ready = 0;
static volatile uint32_t nes_audio_debug_write_errors = 0;
static volatile uint32_t nes_audio_debug_write_slow = 0;
static volatile uint32_t nes_audio_debug_write_max_ms = 0;
static volatile uint16_t nes_audio_debug_peak = 0;
static volatile uint16_t nes_audio_debug_min_level = NES_AUDIO_RING_SAMPLES;
static volatile uint16_t nes_audio_debug_max_level = 0;
#endif
#endif

static void set_status(const char *text, lv_color_t color)
{
    if (!status_label) return;
    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, color, 0);
}

static lv_obj_t *page_root(void)
{
    if (!page_container) return NULL;
    return (lv_obj_t *)lv_obj_get_user_data(page_container);
}

static void format_size(char *out, size_t out_size, uint64_t bytes)
{
    if (bytes >= 1024ULL * 1024ULL) {
        snprintf(out, out_size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        snprintf(out, out_size, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(out, out_size, "%llu B", (unsigned long long)bytes);
    }
}

static bool has_ext(const char *path, const char *ext)
{
    if (!path || !ext) return false;
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    return plen >= elen && strcasecmp(path + plen - elen, ext) == 0;
}

static const char *base_name(const char *path)
{
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool make_rom_paths(nes_rom_item_t *item, const char *name)
{
    if (!item || !name || name[0] == '\0') return false;

    int written;
    if (name[0] == '/') {
        written = snprintf(item->display_path, sizeof(item->display_path), "%s", name);
    } else {
        written = snprintf(item->display_path, sizeof(item->display_path), "%s/%s", NES_ROM_DIR, name);
    }
    if (written <= 0 || written >= (int)sizeof(item->display_path)) return false;

    written = snprintf(item->vfs_path, sizeof(item->vfs_path), "%s%s", NES_VFS_PREFIX, item->display_path);
    return written > 0 && written < (int)sizeof(item->vfs_path);
}

static bool ensure_sd(void)
{
#ifdef ARDUINO
    return hw_is_sd_insert();
#else
    return false;
#endif
}

static void *nes_alloc(size_t size)
{
#ifdef ARDUINO
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return ptr;
#else
    return malloc(size);
#endif
}

static void nes_free(void *ptr)
{
    if (!ptr) return;
#ifdef ARDUINO
    heap_caps_free(ptr);
#else
    free(ptr);
#endif
}

static bool alloc_rom_items(void)
{
    if (rom_items) return true;
    rom_items = (nes_rom_item_t *)nes_alloc(NES_MAX_ROMS * sizeof(nes_rom_item_t));
    if (!rom_items) return false;
    memset(rom_items, 0, NES_MAX_ROMS * sizeof(nes_rom_item_t));
    return true;
}

static void free_rom_items(void)
{
    nes_free(rom_items);
    rom_items = NULL;
}

static void clear_rom_list(void)
{
    if (rom_list_card) {
        uint32_t cnt = lv_obj_get_child_count(rom_list_card);
        while (cnt > 1) {
            lv_obj_t *child = lv_obj_get_child(rom_list_card, cnt - 1);
            lv_obj_delete(child);
            cnt--;
        }
    }
    if (rom_items) {
        memset(rom_items, 0, NES_MAX_ROMS * sizeof(nes_rom_item_t));
    }
    rom_count = 0;
    if (rom_count_label) lv_label_set_text(rom_count_label, "0");
}

#if NES_APP_HAS_NOFRENDO
static uint16_t swap565(uint16_t color)
{
    return (uint16_t)((color >> 8) | (color << 8));
}

static void clear_display_area(void)
{
    uint16_t w = instance.width();
    uint16_t h = instance.height();
    if (w == 0 || h == 0) return;

    uint16_t *line = (uint16_t *)nes_alloc((size_t)w * sizeof(uint16_t));
    if (!line) return;
    memset(line, 0, (size_t)w * sizeof(uint16_t));
    for (uint16_t y = 0; y < h; ++y) {
        instance.pushColors(0, y, w, 1, line);
    }
    nes_free(line);
}

static bool ensure_frame_buffer(void)
{
    if (frame_w <= 0 || frame_h <= 0) return false;

    size_t required_pixels = (size_t)frame_w * NES_FRAME_CHUNK_H;
    if (frame_buffers[0] && frame_buffer_pixels >= required_pixels) return true;

    // Release old buffers
    for (int i = 0; i < 2; i++) {
        if (frame_buffers[i]) {
            heap_caps_free(frame_buffers[i]);
            frame_buffers[i] = NULL;
        }
    }
    frame_buffer_pixels = 0;

    // Allocate double buffers
    for (int i = 0; i < 2; i++) {
        frame_buffers[i] = (uint16_t *)heap_caps_malloc(required_pixels * sizeof(uint16_t),
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!frame_buffers[i]) {
            frame_buffers[i] = (uint16_t *)heap_caps_malloc(required_pixels * sizeof(uint16_t), MALLOC_CAP_8BIT);
        }
        if (!frame_buffers[i]) return false;
    }

    frame_buffer_pixels = required_pixels;
    current_buffer = 0;
    return true;
}

static void display_init_deck(void)
{
    uint16_t screen_w = instance.width();
    uint16_t screen_h = instance.height();

    frame_x = 0;
    frame_y = 0;
    frame_w = screen_w;
    frame_h = screen_h;

    ensure_frame_buffer();
    clear_display_area();
}

static void display_write_frame_deck(const uint8_t *data[])
{
    if (!data || !ensure_frame_buffer()) return;

    for (int16_t y = 0; y < frame_h; y += NES_FRAME_CHUNK_H) {
        int16_t chunk_h = frame_h - y;
        if (chunk_h > NES_FRAME_CHUNK_H) chunk_h = NES_FRAME_CHUNK_H;

        // Use current buffer for CPU computation
        uint16_t *buf = frame_buffers[current_buffer];

        for (int16_t row = 0; row < chunk_h; ++row) {
            uint32_t src_y = ((uint32_t)(y + row) * NES_SCREEN_HEIGHT) / frame_h;
            if (src_y >= NES_SCREEN_HEIGHT) src_y = NES_SCREEN_HEIGHT - 1;

            const uint8_t *src = data[src_y];
            uint16_t *dst = buf + ((int32_t)row * frame_w);
            uint32_t x_accum = 0;
            uint32_t x_step = ((uint32_t)NES_SCREEN_WIDTH << 16) / frame_w;
            for (int16_t x = 0; x < frame_w; ++x) {
                uint16_t src_x = x_accum >> 16;
                if (src_x >= NES_SCREEN_WIDTH) src_x = NES_SCREEN_WIDTH - 1;
                dst[x] = deck_palette[src[src_x]];
                x_accum += x_step;
            }
        }

        // SPI transfer (blocking)
        instance.pushColors(frame_x, frame_y + y, frame_w, chunk_h, buf);

        // Switch to next buffer for next chunk computation
        current_buffer = 1 - current_buffer;
    }
}

static void set_controller_bit(uint8_t bit, bool pressed)
{
    uint32_t mask = (1UL << bit);
    if (pressed) {
        controller_state &= ~mask;
    } else {
        controller_state |= mask;
    }
}

static void keyboard_cb(int state, char &c)
{
    if (!emulator_running) return;
    if (state != 0 && state != 1) return;

    bool pressed = state == 1;
    char ch = (char)tolower((unsigned char)c);

    switch (ch) {
    case 'w':
        set_controller_bit(0, pressed);
        break;
    case 's':
        set_controller_bit(1, pressed);
        break;
    case 'a':
        set_controller_bit(2, pressed);
        break;
    case 'd':
        set_controller_bit(3, pressed);
        break;
    case ' ':
    case 'e':
        set_controller_bit(4, pressed);
        break;
    case '\n':
    case 'p':
        set_controller_bit(5, pressed);
        break;
    case 'k':
    case 'l':
        set_controller_bit(6, pressed);
        break;
    case 'j':
    case 'h':
        set_controller_bit(7, pressed);
        break;
    case 'q':
    case '\b':
        if (pressed) stop_requested = true;
        break;
    default:
        break;
    }
}

static void disable_lvgl_inputs_for_emulator(void)
{
    lv_indev_t *encoder = lv_get_encoder_indev();
    if (encoder) lv_indev_enable(encoder, false);

    if (hw_has_keyboard()) {
        lv_indev_t *keyboard = lv_get_keyboard_indev();
        if (keyboard) lv_indev_enable(keyboard, false);
        hw_enable_keyboard();
        hw_flush_keyboard();
        hw_set_keyboard_read_callback(keyboard_cb);
    }
}

static void restore_lvgl_inputs_after_emulator(void)
{
    hw_set_keyboard_read_callback(NULL);
    if (hw_has_keyboard()) {
        hw_flush_keyboard();
        disable_keyboard();
    }

    hw_enable_input_devices();
    lv_indev_t *encoder = lv_get_encoder_indev();
    if (encoder) lv_indev_enable(encoder, true);
}

static void request_emulator_stop(void)
{
    if (emulator_running) stop_requested = true;
}

static void nes_task(void *param)
{
    char rom_path[NES_PATH_MAX_LEN];
    snprintf(rom_path, sizeof(rom_path), "%s", active_rom_path);

    char *argv[1];
    argv[0] = rom_path;

    int result = nofrendo_main(1, argv);
    main_eject();
    vid_shutdown();
    osd_shutdown();

    emulator_result = result;
    emulator_running = false;
    emulator_finished = true;
    nes_task_handle = NULL;
    vTaskDelete(NULL);
}

static bool start_emulator(const char *vfs_path)
{
    if (!vfs_path || emulator_running) return false;
    if (!hw_has_keyboard()) {
        set_status("Keyboard is not available.", lv_color_hex(0xFF4444));
        return false;
    }

    snprintf(active_rom_path, sizeof(active_rom_path), "%s", vfs_path);
    controller_state = 0xFFFFFFFF;
    stop_requested = false;
    emulator_finished = false;
    emulator_result = 0;

    lv_obj_t *root = page_root();
    if (root) lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_refr_now(NULL);
    clear_display_area();

    disable_lvgl_inputs_for_emulator();
    emulator_running = true;

    BaseType_t ok = xTaskCreatePinnedToCore(nes_task, "nesTask", 8192, NULL, tskIDLE_PRIORITY,
                                            &nes_task_handle, 0);
    if (ok != pdPASS) {
        emulator_running = false;
        restore_lvgl_inputs_after_emulator();
        if (root) lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
        set_status("Failed to start emulator task.", lv_color_hex(0xFF4444));
        return false;
    }
    return true;
}

extern "C" void *mem_alloc(int size, bool prefer_fast_memory)
{
    if (size <= 0) return NULL;
    uint32_t caps = prefer_fast_memory ? (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *ptr = heap_caps_malloc(size, caps);
    if (!ptr) ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return ptr;
}

#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
static void nes_audio_debug_reset(void)
{
    nes_audio_debug_last_ms = millis();
    nes_audio_debug_produced = 0;
    nes_audio_debug_pushed = 0;
    nes_audio_debug_consumed = 0;
    nes_audio_debug_dropped = 0;
    nes_audio_debug_overflows = 0;
    nes_audio_debug_underruns = 0;
    nes_audio_debug_partial_reads = 0;
    nes_audio_debug_prefill_waits = 0;
    nes_audio_debug_frame_calls = 0;
    nes_audio_debug_callback_sets = 0;
    nes_audio_debug_not_ready = 0;
    nes_audio_debug_write_errors = 0;
    nes_audio_debug_write_slow = 0;
    nes_audio_debug_write_max_ms = 0;
    nes_audio_debug_peak = 0;
    nes_audio_debug_min_level = NES_AUDIO_RING_SAMPLES;
    nes_audio_debug_max_level = 0;
}

static void nes_audio_debug_observe_level(uint16_t level)
{
    if (level < nes_audio_debug_min_level) nes_audio_debug_min_level = level;
    if (level > nes_audio_debug_max_level) nes_audio_debug_max_level = level;
}
#endif

static void nes_audio_ring_reset(void)
{
    portENTER_CRITICAL(&nes_audio_mux);
    nes_audio_ring_read = 0;
    nes_audio_ring_write = 0;
    nes_audio_ring_count = 0;
    portEXIT_CRITICAL(&nes_audio_mux);
}

static uint16_t nes_audio_ring_level(void)
{
    uint16_t level;
    portENTER_CRITICAL(&nes_audio_mux);
    level = nes_audio_ring_count;
    portEXIT_CRITICAL(&nes_audio_mux);
    return level;
}

#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
static void nes_audio_debug_report(void)
{
    uint32_t now = millis();
    uint32_t elapsed = now - nes_audio_debug_last_ms;
    if (elapsed < NES_AUDIO_DEBUG_INTERVAL_MS) return;

    uint16_t level = nes_audio_ring_level();
    uint32_t produced = nes_audio_debug_produced;
    uint32_t pushed = nes_audio_debug_pushed;
    uint32_t consumed = nes_audio_debug_consumed;
    uint32_t dropped = nes_audio_debug_dropped;
    uint32_t overflows = nes_audio_debug_overflows;
    uint32_t underruns = nes_audio_debug_underruns;
    uint32_t partial_reads = nes_audio_debug_partial_reads;
    uint32_t prefill_waits = nes_audio_debug_prefill_waits;
    uint32_t frame_calls = nes_audio_debug_frame_calls;
    uint32_t callback_sets = nes_audio_debug_callback_sets;
    uint32_t not_ready = nes_audio_debug_not_ready;
    uint32_t write_errors = nes_audio_debug_write_errors;
    uint32_t write_slow = nes_audio_debug_write_slow;
    uint32_t write_max_ms = nes_audio_debug_write_max_ms;
    uint16_t peak = nes_audio_debug_peak;
    uint16_t min_level = nes_audio_debug_min_level;
    uint16_t max_level = nes_audio_debug_max_level;

    LILYGO_LOG_PRINTF("NES AUDIO DBG %lums ring=%u/%u min=%u max=%u prod=%lu push=%lu cons=%lu drop=%lu ovf=%lu under=%lu part=%lu prewait=%lu framecb=%lu setcb=%lu cb=%u notready=%lu peak=%u werr=%lu wslow=%lu wmax=%lums\n",
                  (unsigned long)elapsed,
                  level, NES_AUDIO_RING_SAMPLES, min_level, max_level,
                  (unsigned long)produced, (unsigned long)pushed, (unsigned long)consumed,
                  (unsigned long)dropped, (unsigned long)overflows,
                  (unsigned long)underruns, (unsigned long)partial_reads,
                  (unsigned long)prefill_waits, (unsigned long)frame_calls,
                  (unsigned long)callback_sets, nes_audio_callback ? 1 : 0,
                  (unsigned long)not_ready, peak,
                  (unsigned long)write_errors,
                  (unsigned long)write_slow, (unsigned long)write_max_ms);

    nes_audio_debug_reset();
    nes_audio_debug_observe_level(level);
}
#endif

static void nes_audio_ring_push(const int16_t *samples, uint16_t frames)
{
    if (!samples || frames == 0) return;
    uint16_t pushed = frames;
    uint16_t dropped = 0;

    if (frames > NES_AUDIO_RING_SAMPLES) {
        dropped = frames - NES_AUDIO_RING_SAMPLES;
        samples += (frames - NES_AUDIO_RING_SAMPLES) * NES_AUDIO_CHANNELS;
        frames = NES_AUDIO_RING_SAMPLES;
        pushed = frames;
    }

    portENTER_CRITICAL(&nes_audio_mux);

    while ((uint16_t)(NES_AUDIO_RING_SAMPLES - nes_audio_ring_count) < frames) {
        nes_audio_ring_read = (uint16_t)((nes_audio_ring_read + 1) % NES_AUDIO_RING_SAMPLES);
        nes_audio_ring_count--;
        dropped++;
    }

    for (uint16_t i = 0; i < frames; ++i) {
        uint16_t dst = (uint16_t)(nes_audio_ring_write * NES_AUDIO_CHANNELS);
        uint16_t src = (uint16_t)(i * NES_AUDIO_CHANNELS);
        nes_audio_ring[dst] = samples[src];
        nes_audio_ring[dst + 1] = samples[src + 1];
        nes_audio_ring_write = (uint16_t)((nes_audio_ring_write + 1) % NES_AUDIO_RING_SAMPLES);
        nes_audio_ring_count++;
    }

    uint16_t level = nes_audio_ring_count;
    portEXIT_CRITICAL(&nes_audio_mux);

#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
    nes_audio_debug_pushed += pushed;
    if (dropped) {
        nes_audio_debug_dropped += dropped;
        nes_audio_debug_overflows++;
    }
    nes_audio_debug_observe_level(level);
#endif
}

static uint16_t nes_audio_ring_pop(int16_t *out, uint16_t max_frames)
{
    if (!out || max_frames == 0) return 0;

    portENTER_CRITICAL(&nes_audio_mux);

    uint16_t frames = nes_audio_ring_count;
    if (frames > max_frames) frames = max_frames;

    for (uint16_t i = 0; i < frames; ++i) {
        uint16_t src = (uint16_t)(nes_audio_ring_read * NES_AUDIO_CHANNELS);
        uint16_t dst = (uint16_t)(i * NES_AUDIO_CHANNELS);
        out[dst] = nes_audio_ring[src];
        out[dst + 1] = nes_audio_ring[src + 1];
        nes_audio_ring_read = (uint16_t)((nes_audio_ring_read + 1) % NES_AUDIO_RING_SAMPLES);
        nes_audio_ring_count--;
    }

    portEXIT_CRITICAL(&nes_audio_mux);
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
    nes_audio_debug_consumed += frames;
    nes_audio_debug_observe_level(nes_audio_ring_level());
#endif
    return frames;
}

static void nes_audio_task(void *param)
{
    (void)param;
    int16_t chunk[NES_AUDIO_FRAG_SAMPLES * NES_AUDIO_CHANNELS];
    bool primed = false;

    while (nes_audio_task_running) {
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
        nes_audio_debug_report();
#endif
        if (!primed) {
            if (nes_audio_ring_level() < NES_AUDIO_PREFILL_SAMPLES) {
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
                nes_audio_debug_prefill_waits++;
#endif
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }
            primed = true;
        }

        uint16_t frames = nes_audio_ring_pop(chunk, NES_AUDIO_FRAG_SAMPLES);
        if (frames == 0) {
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
            nes_audio_debug_underruns++;
#endif
            primed = false;
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        if (frames < NES_AUDIO_FRAG_SAMPLES) {
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
            nes_audio_debug_partial_reads++;
#endif
            memset(chunk + (frames * NES_AUDIO_CHANNELS), 0,
                   (NES_AUDIO_FRAG_SAMPLES - frames) * NES_AUDIO_CHANNELS * sizeof(int16_t));
            primed = false;
        }

        if (nes_audio_output) {
            size_t bytes = (size_t)NES_AUDIO_FRAG_SAMPLES * NES_AUDIO_CHANNELS * sizeof(int16_t);
            uint32_t write_start = millis();
            int result = nes_audio_output->write((const uint8_t *)chunk, bytes);
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
            uint32_t write_ms = millis() - write_start;
            if (write_ms > nes_audio_debug_write_max_ms) nes_audio_debug_write_max_ms = write_ms;
            if (write_ms >= NES_AUDIO_WRITE_SLOW_MS) nes_audio_debug_write_slow++;
#endif
            if (result < 0) {
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
                nes_audio_debug_write_errors++;
#endif
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    nes_audio_task_handle = NULL;
    vTaskDelete(NULL);
}

static void nes_audio_stop_task(void)
{
    nes_audio_task_running = false;
    uint32_t start = millis();
    while (nes_audio_task_handle && millis() - start < 1200) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (nes_audio_task_handle) {
        LILYGO_LOG_PRINTLN("NES audio task stop timeout.");
    }
}

#if NES_AUDIO_TEST_TONE
static void nes_audio_play_test_tone(void)
{
    if (!nes_audio_output) return;

    int16_t chunk[NES_AUDIO_FRAG_SAMPLES * NES_AUDIO_CHANNELS];
    uint32_t total_frames = (uint32_t)NES_AUDIO_SAMPLE_RATE * NES_AUDIO_TEST_TONE_MS / 1000;
    uint16_t half_period = NES_AUDIO_SAMPLE_RATE / (NES_AUDIO_TEST_TONE_HZ * 2);
    if (half_period == 0) half_period = 1;

    LILYGO_LOG_PRINTF("NES audio test tone: %lu frames, %d Hz\n",
                  (unsigned long)total_frames, NES_AUDIO_TEST_TONE_HZ);

    uint32_t frame = 0;
    while (frame < total_frames) {
        uint16_t frames = NES_AUDIO_FRAG_SAMPLES;
        if (total_frames - frame < frames) frames = (uint16_t)(total_frames - frame);

        for (uint16_t i = 0; i < frames; ++i) {
            int16_t sample = (((frame + i) / half_period) & 1) ? 9000 : -9000;
            chunk[(i * NES_AUDIO_CHANNELS) + 0] = sample;
            chunk[(i * NES_AUDIO_CHANNELS) + 1] = sample;
        }
        if (frames < NES_AUDIO_FRAG_SAMPLES) {
            memset(chunk + (frames * NES_AUDIO_CHANNELS), 0,
                   (NES_AUDIO_FRAG_SAMPLES - frames) * NES_AUDIO_CHANNELS * sizeof(int16_t));
        }

        int result = nes_audio_output->write((const uint8_t *)chunk, sizeof(chunk));
        if (result < 0) {
            LILYGO_LOG_PRINTF("NES audio test tone write failed: %d\n", result);
            break;
        }
        frame += frames;
    }

    memset(chunk, 0, sizeof(chunk));
    nes_audio_output->write((const uint8_t *)chunk, sizeof(chunk));
}
#endif

static int osd_init_sound(void)
{
    nes_audio_callback = NULL;
    nes_audio_sample_accum = 0;
    nes_audio_ring_reset();
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
    nes_audio_debug_reset();
#endif
    nes_audio_open = false;
    nes_audio_task_running = false;
    nes_audio_task_handle = NULL;
    nes_audio_output = instance.getAudioOutput();
    if (!nes_audio_output) {
        LILYGO_LOG_PRINTLN("NES audio output is not available.");
        return 0;
    }

    if (!nes_audio_output->open(NES_AUDIO_BITS, NES_AUDIO_CHANNELS, NES_AUDIO_SAMPLE_RATE)) {
        LILYGO_LOG_PRINTLN("NES audio output open failed; continuing without sound.");
        nes_audio_output = NULL;
        return 0;
    }

    nes_audio_output->setVolume(85);
    delay(20);
#if NES_AUDIO_TEST_TONE
    nes_audio_play_test_tone();
#endif

    nes_audio_open = true;
    nes_audio_task_running = true;
    if (xTaskCreatePinnedToCore(nes_audio_task, "nesAudio", 4096, NULL,
                                tskIDLE_PRIORITY + 3, &nes_audio_task_handle, 0) != pdPASS) {
        LILYGO_LOG_PRINTLN("NES audio task start failed; continuing without sound.");
        nes_audio_task_running = false;
        nes_audio_output->close();
        nes_audio_open = false;
        nes_audio_output = NULL;
        return 0;
    }

#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
    LILYGO_LOG_PRINTF("NES audio open: %d Hz, %d-bit, %d channels\n",
                  NES_AUDIO_SAMPLE_RATE, NES_AUDIO_BITS, NES_AUDIO_CHANNELS);
#endif
    return 0;
}

static void osd_stopsound(void)
{
    nes_audio_callback = NULL;
    nes_audio_stop_task();
    if (nes_audio_open && nes_audio_output) {
        memset(nes_audio_stereo, 0, sizeof(nes_audio_stereo));
        for (uint8_t i = 0; i < 2; ++i) {
            nes_audio_output->write((const uint8_t *)nes_audio_stereo, sizeof(nes_audio_stereo));
        }
        nes_audio_output->close();
    }
    nes_audio_ring_reset();
    nes_audio_open = false;
    nes_audio_output = NULL;
}

static void do_audio_frame(void)
{
    if (!nes_audio_open || !nes_audio_output || !nes_audio_callback) {
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
        nes_audio_debug_not_ready++;
#endif
        return;
    }

    nes_audio_sample_accum += NES_AUDIO_SAMPLE_RATE;
    int left = nes_audio_sample_accum / NES_REFRESH_RATE;
    nes_audio_sample_accum %= NES_REFRESH_RATE;

    while (left > 0 && !stop_requested) {
        int samples = left;
        if (samples > NES_AUDIO_FRAG_SAMPLES) samples = NES_AUDIO_FRAG_SAMPLES;

        nes_audio_callback(nes_audio_mono, samples);
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
        nes_audio_debug_produced += samples;
#endif
        for (int i = 0; i < samples; ++i) {
            int16_t sample = (int16_t)(nes_audio_mono[i] >> NES_AUDIO_GAIN_SHIFT);
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
            uint16_t mag = sample < 0 ? (uint16_t) - sample : (uint16_t)sample;
            if (mag > nes_audio_debug_peak) nes_audio_debug_peak = mag;
#endif
            nes_audio_stereo[(i * NES_AUDIO_CHANNELS) + 0] = sample;
            nes_audio_stereo[(i * NES_AUDIO_CHANNELS) + 1] = sample;
        }

        nes_audio_ring_push(nes_audio_stereo, (uint16_t)samples);
        left -= samples;
    }
}

extern "C" void osd_setsound(void (*playfunc)(void *buffer, int length))
{
    nes_audio_callback = playfunc;
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
    nes_audio_debug_callback_sets++;
#endif
}

extern "C" void osd_audio_frame(void)
{
#if LILYGO_DEBUG_ENABLED && NES_AUDIO_DEBUG
    nes_audio_debug_frame_calls++;
#endif
    do_audio_frame();
}

static int video_init(int width, int height)
{
    (void)width;
    (void)height;
    return 0;
}

static void video_shutdown(void)
{
}

static int video_set_mode(int width, int height)
{
    (void)width;
    (void)height;
    return 0;
}

static void video_set_palette(rgb_t *pal)
{
    if (!pal) return;
    for (int i = 0; i < 256; ++i) {
        uint16_t color = (uint16_t)((pal[i].b >> 3) | ((pal[i].g >> 2) << 5) | ((pal[i].r >> 3) << 11));
        deck_palette[i] = swap565(color);
    }
}

static void video_clear(uint8 color)
{
    (void)color;
    clear_display_area();
}

static bitmap_t *video_lock_write(void)
{
    dummy_bitmap = bmp_createhw((uint8 *)dummy_fb, NES_SCREEN_WIDTH, NES_SCREEN_HEIGHT, NES_SCREEN_WIDTH * 2);
    return dummy_bitmap;
}

static void video_free_write(int num_dirties, rect_t *dirty_rects)
{
    (void)num_dirties;
    (void)dirty_rects;
    if (dummy_bitmap) bmp_destroy(&dummy_bitmap);
}

static void video_custom_blit(bitmap_t *bmp, int num_dirties, rect_t *dirty_rects)
{
    (void)num_dirties;
    (void)dirty_rects;
    if (bmp) display_write_frame_deck((const uint8_t **)bmp->line);
}

static viddriver_t deck_video_driver = {
    "DeckV2 LCD",
    video_init,
    video_shutdown,
    video_set_mode,
    video_set_palette,
    video_clear,
    video_lock_write,
    video_free_write,
    video_custom_blit,
    false
};

extern "C" void osd_getvideoinfo(vidinfo_t *info)
{
    info->default_width = NES_SCREEN_WIDTH;
    info->default_height = NES_SCREEN_HEIGHT;
    info->driver = &deck_video_driver;
}

extern "C" void osd_getsoundinfo(sndinfo_t *info)
{
    info->sample_rate = NES_AUDIO_SAMPLE_RATE;
    info->bps = NES_AUDIO_BITS;
}

extern "C" int osd_init(void)
{
    if (osd_init_sound()) return -1;
    display_init_deck();
    return ensure_frame_buffer() ? 0 : -1;
}

extern "C" void osd_shutdown(void)
{
    osd_stopsound();
    if (nes_tick_timer) {
        xTimerStop(nes_tick_timer, 0);
        xTimerDelete(nes_tick_timer, 0);
        nes_tick_timer = NULL;
        nes_tick_func = NULL;
    }
    if (dummy_bitmap) bmp_destroy(&dummy_bitmap);
}

static char configfilename[] = "/sd/nes/nofrendo.cfg";

extern "C" int osd_main(int argc, char *argv[])
{
    (void)argc;
    config.filename = configfilename;
    return main_loop(argv[0], system_autodetect);
}

static void nes_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (nes_tick_func) nes_tick_func();
}

extern "C" int osd_installtimer(int frequency, void *func, int funcsize, void *counter, int countersize)
{
    (void)funcsize;
    (void)counter;
    (void)countersize;
    if (frequency <= 0 || !func) return -1;

    nes_tick_func = (void (*)(void))func;
    TickType_t period = configTICK_RATE_HZ / frequency;
    if (period == 0) period = 1;

    if (nes_tick_timer) {
        xTimerStop(nes_tick_timer, 0);
        xTimerDelete(nes_tick_timer, 0);
    }
    nes_tick_timer = xTimerCreate("nesTick", period, pdTRUE, NULL, nes_timer_cb);
    if (!nes_tick_timer) return -1;
    xTimerStart(nes_tick_timer, 0);
    return 0;
}

extern "C" void osd_getinput(void)
{
    if (stop_requested) {
        stop_requested = false;
        main_soft_quit();
        return;
    }

    static const int ev[16] = {
        event_joypad1_up, event_joypad1_down, event_joypad1_left, event_joypad1_right,
        event_joypad1_select, event_joypad1_start, event_joypad1_a, event_joypad1_b,
        event_state_save, event_state_load, 0, 0,
        0, 0, 0, 0
    };
    static uint32_t old_state = 0xFFFFFFFF;
    uint32_t state = controller_state;
    uint32_t changed = state ^ old_state;
    old_state = state;

    for (int i = 0; i < 16; ++i) {
        if ((changed & 1U) && ev[i]) {
            event_t handler = event_get(ev[i]);
            if (handler) handler((state & 1U) ? INP_STATE_BREAK : INP_STATE_MAKE);
        }
        changed >>= 1;
        state >>= 1;
    }
}

extern "C" void osd_getmouse(int *x, int *y, int *button)
{
    if (x) *x = 0;
    if (y) *y = 0;
    if (button) *button = 0;
}

extern "C" void osd_fullname(char *fullname, const char *shortname)
{
    if (!fullname || !shortname) return;
    strncpy(fullname, shortname, PATH_MAX);
    fullname[PATH_MAX - 1] = '\0';
}

extern "C" char *osd_newextension(char *string, const char *ext)
{
    if (!string || !ext) return string;
    size_t len = strlen(string);
    size_t ext_len = strlen(ext);
    if (len < 4 || ext_len == 0) return string;
    char *dot = strrchr(string, '.');
    if (!dot) dot = string + len;
    snprintf(dot, PATH_MAX - (dot - string), "%s", ext);
    return string;
}

extern "C" int osd_makesnapname(char *filename, int len)
{
    if (filename && len > 0) filename[0] = '\0';
    return -1;
}
#endif

static void emulator_state_timer_cb(lv_timer_t *timer)
{
    (void)timer;
#if NES_APP_HAS_NOFRENDO
    if (!emulator_finished) return;
    emulator_finished = false;

    restore_lvgl_inputs_after_emulator();
    clear_display_area();

    lv_obj_t *root = page_root();
    if (root) {
        lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(root);
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Emulator exited (%d).", (int)emulator_result);
    set_status(msg, emulator_result == 0 ? UI_COLOR_ACCENT : lv_color_hex(0xFF4444));
#endif
}

#if NES_APP_HAS_NOFRENDO
static void release_frame_buffer(void)
{
    for (int i = 0; i < 2; i++) {
        if (frame_buffers[i]) {
            heap_caps_free(frame_buffers[i]);
            frame_buffers[i] = NULL;
        }
    }
    frame_buffer_pixels = 0;
}
#endif

static void start_rom_cb(lv_event_t *e)
{
    nes_rom_item_t *item = (nes_rom_item_t *)lv_event_get_user_data(e);
    if (!item) return;

#if NES_APP_HAS_NOFRENDO
    char msg[96];
    snprintf(msg, sizeof(msg), "Starting %s", base_name(item->display_path));
    set_status(msg, UI_COLOR_ACCENT);
    start_emulator(item->vfs_path);
#else
    set_status("NES core is not available in this build.", lv_color_hex(0xFF4444));
#endif
}

static void add_empty_row(const char *text)
{
    if (!rom_list_card) return;
    lv_obj_t *lbl = lv_label_create(rom_list_card);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
}

static void create_rom_row(nes_rom_item_t *item)
{
    lv_obj_t *row = lv_obj_create(rom_list_card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    lv_obj_set_style_pad_bottom(row, 4, 0);
    lv_obj_set_style_pad_left(row, 0, 0);
    lv_obj_set_style_pad_right(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(row, UI_COLOR_CARD_FOCUS, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_80, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(row, 1, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(row, start_rom_cb, LV_EVENT_CLICKED, item);

    lv_group_t *group = lv_group_get_default();
    if (group && lv_obj_get_group(row) != group) {
        lv_group_add_obj(group, row);
    }

    lv_obj_t *icon = lv_label_create(row);
    lv_label_set_text(icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(icon, UI_COLOR_ACCENT, 0);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, base_name(item->display_path));
    lv_obj_set_style_text_color(name, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 1);
    lv_obj_set_flex_grow(name, 1);

    char size_buf[24];
    format_size(size_buf, sizeof(size_buf), item->size);
    lv_obj_t *size = lv_label_create(row);
    lv_label_set_text(size, size_buf);
    lv_obj_set_style_text_color(size, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(size, &lv_font_montserrat_12, 0);
}

static void build_rom_list(void)
{
    clear_rom_list();
    if (!rom_list_card) return;

#if !NES_APP_HAS_NOFRENDO
    add_empty_row("NES core not linked");
    set_status("NES core is not available in this build.", lv_color_hex(0xFF4444));
    return;
#else
    if (!ensure_sd()) {
        add_empty_row("No SD card detected");
        set_status("SD card is not available.", lv_color_hex(0xFF4444));
        return;
    }

    if (!alloc_rom_items()) {
        add_empty_row("No RAM for ROM list");
        set_status("No RAM for ROM list.", lv_color_hex(0xFF4444));
        return;
    }

    if (!SD.exists(NES_ROM_DIR)) {
        SD.mkdir(NES_ROM_DIR);
    }

    File root = SD.open(NES_ROM_DIR);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        add_empty_row("Cannot open /nes");
        set_status("Cannot open /nes on SD.", lv_color_hex(0xFF4444));
        return;
    }

    File file = root.openNextFile();
    while (file && rom_count < NES_MAX_ROMS) {
        if (!file.isDirectory() && has_ext(file.name(), ".nes")) {
            nes_rom_item_t *item = &rom_items[rom_count];
            memset(item, 0, sizeof(*item));
            if (make_rom_paths(item, file.name())) {
                item->size = file.size();
                create_rom_row(item);
                rom_count++;
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();

    if (rom_count == 0) {
        add_empty_row("Copy .nes files to /nes");
        set_status("No ROM files found.", UI_COLOR_TEXT_SECONDARY);
    } else {
        char msg[48];
        snprintf(msg, sizeof(msg), "%u ROM file%s found.", rom_count, rom_count == 1 ? "" : "s");
        set_status(msg, UI_COLOR_ACCENT);
    }

    if (rom_count_label) lv_label_set_text_fmt(rom_count_label, "%u", rom_count);
#endif
}

static lv_obj_t *add_info_row(lv_obj_t *card, const char *icon, const char *title,
                              const char *value, lv_obj_t **out_label)
{
    lv_obj_t *row = ui_create_card_info(card, icon, title, value);
    if (out_label) *out_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    return row;
}

static void refresh_btn_cb(lv_event_t *e)
{
    (void)e;
    build_rom_list();
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
#if NES_APP_HAS_NOFRENDO
    if (emulator_running) {
        request_emulator_stop();
        return;
    }
#endif

    if (state_timer) {
        lv_timer_delete(state_timer);
        state_timer = NULL;
    }
    clear_rom_list();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    status_label = NULL;
    rom_count_label = NULL;
    rom_list_card = NULL;
    free_rom_items();
#if NES_APP_HAS_NOFRENDO
    release_frame_buffer();
#endif
    menu_show();
}

void ui_nes_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "NES", back_event_handler);

    lv_obj_t *card = ui_create_card(page_container, "Library");
    status_label = lv_label_create(card);
    lv_label_set_text(status_label, "Checking SD card...");
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    add_info_row(card, LV_SYMBOL_SD_CARD, "Folder", NES_ROM_DIR, NULL);
    add_info_row(card, LV_SYMBOL_FILE, "ROMs", "0", &rom_count_label);
    ui_create_card_button(card, LV_SYMBOL_REFRESH, "Scan", "Refresh", refresh_btn_cb);

    rom_list_card = ui_create_card(page_container, "ROMs");
    state_timer = lv_timer_create(emulator_state_timer_cb, 200, NULL);

    build_rom_list();
}

app_t ui_nes_main = {
    .setup_func_cb = ui_nes_enter,
    .exit_func_cb = NULL,
    .user_data = NULL,
};

#endif
