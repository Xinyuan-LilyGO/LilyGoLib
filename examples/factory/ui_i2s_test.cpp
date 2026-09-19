/**
 * @file      ui_i2s_test.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-05-28
 * Direct I2S/codec output test. Generates PCM without SD, WiFi or decoders.
 */
#include <LilyGoLog.h>
#include "ui_define.h"
#include <math.h>
#if defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

#if !defined(EXCLUDE_I2S_TEST)

static constexpr uint32_t TEST_TONE_HZ = 1000;
static constexpr uint32_t TEST_HIGH_TONE_HZ = 10000;
static constexpr size_t TEST_FRAMES = 512;
static constexpr uint32_t TEST_TASK_STACK_BYTES = 6144;

#define I2S_SIGNAL_LIST "Sine 1k\nHigh sine\nLeft only\nRight only\nL/R invert\nSilence\nDC level\n0x55/0xAA\nImpulse"

typedef enum {
    I2S_TEST_IDLE = 0,
    I2S_TEST_STARTING,
    I2S_TEST_RUNNING,
    I2S_TEST_STOPPING,
    I2S_TEST_STOPPED,
    I2S_TEST_NO_OUTPUT,
    I2S_TEST_NO_BUFFER_MEMORY,
    I2S_TEST_NO_TASK_MEMORY,
    I2S_TEST_OPEN_FAILED,
    I2S_TEST_WRITE_ERROR,
} i2s_test_state_t;

typedef enum {
    I2S_SIGNAL_SINE_1K = 0,
    I2S_SIGNAL_HIGH_SINE,
    I2S_SIGNAL_LEFT_ONLY,
    I2S_SIGNAL_RIGHT_ONLY,
    I2S_SIGNAL_LR_INVERT,
    I2S_SIGNAL_SILENCE,
    I2S_SIGNAL_DC_LEVEL,
    I2S_SIGNAL_BIT_PATTERN,
    I2S_SIGNAL_IMPULSE,
} i2s_signal_t;

static lv_obj_t *page_container = NULL;
static lv_obj_t *state_label = NULL;
static lv_obj_t *rate_label = NULL;
static lv_obj_t *metrics_label = NULL;
static lv_obj_t *volume_label = NULL;
static lv_obj_t *amplitude_label = NULL;
static lv_obj_t *signal_dropdown = NULL;
static lv_timer_t *status_timer = NULL;

static TaskHandle_t test_task = NULL;
static volatile bool stop_requested = false;
static volatile i2s_test_state_t test_state = I2S_TEST_IDLE;
static volatile uint32_t current_source_rate = 0;
static volatile uint32_t current_output_rate = 0;
static volatile uint32_t frames_written = 0;
static volatile uint32_t writes_done = 0;
static volatile uint32_t write_errors = 0;
static volatile uint32_t slow_writes = 0;
static volatile uint32_t max_write_ms = 0;
static volatile uint8_t amplitude_percent = 25;
static volatile uint8_t selected_signal = I2S_SIGNAL_SINE_1K;
static volatile bool test_task_stack_in_psram = false;

static const char *state_text(i2s_test_state_t state)
{
    switch (state) {
    case I2S_TEST_IDLE: return "Idle";
    case I2S_TEST_STARTING: return "Starting";
    case I2S_TEST_RUNNING: return "Running";
    case I2S_TEST_STOPPING: return "Stopping";
    case I2S_TEST_STOPPED: return "Stopped";
    case I2S_TEST_NO_OUTPUT: return "No audio output";
    case I2S_TEST_NO_BUFFER_MEMORY: return "No buffer memory";
    case I2S_TEST_NO_TASK_MEMORY: return "No task memory";
    case I2S_TEST_OPEN_FAILED: return "Open failed";
    case I2S_TEST_WRITE_ERROR: return "Write error";
    default: return "Unknown";
    }
}

static const char *signal_text(uint8_t signal)
{
    switch ((i2s_signal_t)signal) {
    case I2S_SIGNAL_SINE_1K: return "Sine 1k";
    case I2S_SIGNAL_HIGH_SINE: return "High sine";
    case I2S_SIGNAL_LEFT_ONLY: return "Left only";
    case I2S_SIGNAL_RIGHT_ONLY: return "Right only";
    case I2S_SIGNAL_LR_INVERT: return "L/R invert";
    case I2S_SIGNAL_SILENCE: return "Silence";
    case I2S_SIGNAL_DC_LEVEL: return "DC level";
    case I2S_SIGNAL_BIT_PATTERN: return "0x55/0xAA";
    case I2S_SIGNAL_IMPULSE: return "Impulse";
    default: return "Unknown";
    }
}

static uint32_t signal_tone_hz(uint8_t signal, uint32_t output_rate)
{
    switch ((i2s_signal_t)signal) {
    case I2S_SIGNAL_HIGH_SINE:
        if (output_rate > (TEST_HIGH_TONE_HZ * 2)) {
            return TEST_HIGH_TONE_HZ;
        }
        return output_rate / 4;
    case I2S_SIGNAL_SINE_1K:
    case I2S_SIGNAL_LEFT_ONLY:
    case I2S_SIGNAL_RIGHT_ONLY:
    case I2S_SIGNAL_LR_INVERT:
        return TEST_TONE_HZ;
    default:
        return 0;
    }
}

static bool is_44k_family(uint32_t rate)
{
    return (rate % 11025) == 0 && (rate % 8000) != 0;
}

static const char *clock_note(uint32_t source_rate, uint32_t output_rate)
{
    if (source_rate != output_rate) {
        return "Resampled output";
    }
    return is_44k_family(output_rate) ? "Native 44.1 clock" : "Native clock";
}

static uint32_t pack_test_arg(uint32_t source_rate, uint32_t output_rate)
{
    return ((source_rate & 0xFFFF) << 16) | (output_rate & 0xFFFF);
}

static void unpack_test_arg(uint32_t arg, uint32_t *source_rate, uint32_t *output_rate)
{
    *source_rate = (arg >> 16) & 0xFFFF;
    *output_rate = arg & 0xFFFF;
    if (*source_rate == 0) {
        *source_rate = *output_rate;
    }
}

static void reset_stats(uint32_t source_rate, uint32_t output_rate)
{
    current_source_rate = source_rate;
    current_output_rate = output_rate;
    frames_written = 0;
    writes_done = 0;
    write_errors = 0;
    slow_writes = 0;
    max_write_ms = 0;
}

static int16_t *alloc_pcm_buffer(size_t bytes, bool *in_psram)
{
    if (in_psram) {
        *in_psram = false;
    }
#if defined(ARDUINO)
    void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) {
        if (in_psram) {
            *in_psram = true;
        }
        return (int16_t *)ptr;
    }

    ptr = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }
    return (int16_t *)ptr;
#else
    return (int16_t *)malloc(bytes);
#endif
}

static void log_i2s_heap(const char *reason)
{
#if defined(ARDUINO)
    LILYGO_LOG_PRINTF("I2S TEST %s heap internal=%u largest_internal=%u psram=%u largest_psram=%u\n",
                  reason,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    (void)reason;
#endif
}

static void delete_current_test_task(void)
{
#if defined(ARDUINO) && defined(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM)
    if (test_task_stack_in_psram) {
        test_task_stack_in_psram = false;
        vTaskDeleteWithCaps(NULL);
        return;
    }
#endif
    vTaskDelete(NULL);
}

static void i2s_test_task(void *arg)
{
    uint32_t source_rate = 0;
    uint32_t output_rate = 0;
    unpack_test_arg((uint32_t)(uintptr_t)arg, &source_rate, &output_rate);
    AudioOutputIf *out = instance.getAudioOutput();
    int16_t *buffer = NULL;
    bool buffer_in_psram = false;
    const size_t bytes = TEST_FRAMES * 2 * sizeof(int16_t);

    test_state = I2S_TEST_STARTING;

    if (!out) {
        test_state = I2S_TEST_NO_OUTPUT;
        test_task = NULL;
        delete_current_test_task();
        return;
    }

    buffer = alloc_pcm_buffer(bytes, &buffer_in_psram);
    if (!buffer) {
        log_i2s_heap("buffer allocation failed");
        test_state = I2S_TEST_NO_BUFFER_MEMORY;
        test_task = NULL;
        delete_current_test_task();
        return;
    }

    if (!out->open(16, 2, output_rate)) {
        free(buffer);
        test_state = I2S_TEST_OPEN_FAILED;
        test_task = NULL;
        delete_current_test_task();
        return;
    }

    LILYGO_LOG_PRINTF("I2S TEST start source=%lu output=%lu signal=%s mode=%s buffer=%s stack=%s\n",
                  (unsigned long)source_rate,
                  (unsigned long)output_rate,
                  signal_text(selected_signal),
                  clock_note(source_rate, output_rate),
                  buffer_in_psram ? "psram" : "internal",
                  test_task_stack_in_psram ? "psram" : "internal");

    float phase = 0.0f;
    uint32_t last_log_ms = millis();
    uint32_t sample_index = 0;

    test_state = I2S_TEST_RUNNING;
    while (!stop_requested) {
        uint8_t signal = selected_signal;
        uint32_t tone_hz = signal_tone_hz(signal, output_rate);
        float phase_step = tone_hz ?
                           (2.0f * (float)M_PI * (float)tone_hz) / (float)output_rate :
                           0.0f;
        int32_t amp = (32767L * (int32_t)amplitude_percent) / 100L;
        for (size_t i = 0; i < TEST_FRAMES; ++i) {
            int16_t left = 0;
            int16_t right = 0;
            int16_t sample = 0;

            if (tone_hz) {
                sample = (int16_t)(sinf(phase) * (float)amp);
                phase += phase_step;
                if (phase >= (2.0f * (float)M_PI)) {
                    phase -= (2.0f * (float)M_PI);
                }
            }

            switch ((i2s_signal_t)signal) {
            case I2S_SIGNAL_SINE_1K:
            case I2S_SIGNAL_HIGH_SINE:
                left = sample;
                right = sample;
                break;
            case I2S_SIGNAL_LEFT_ONLY:
                left = sample;
                break;
            case I2S_SIGNAL_RIGHT_ONLY:
                right = sample;
                break;
            case I2S_SIGNAL_LR_INVERT:
                left = sample;
                right = (int16_t)-sample;
                break;
            case I2S_SIGNAL_DC_LEVEL:
                left = (int16_t)amp;
                right = (int16_t)amp;
                break;
            case I2S_SIGNAL_BIT_PATTERN:
                left = (int16_t)0x5555;
                right = (int16_t)0xAAAA;
                break;
            case I2S_SIGNAL_IMPULSE:
                if ((sample_index % output_rate) < 2) {
                    left = (int16_t)amp;
                    right = (int16_t)amp;
                }
                break;
            case I2S_SIGNAL_SILENCE:
            default:
                break;
            }

            buffer[(i * 2) + 0] = left;
            buffer[(i * 2) + 1] = right;
            sample_index++;
        }

        uint32_t start_ms = millis();
        int ret = out->write((const uint8_t *)buffer, bytes);
        uint32_t elapsed = millis() - start_ms;

        writes_done = writes_done + 1;
        frames_written += TEST_FRAMES;
        if (elapsed > max_write_ms) max_write_ms = elapsed;
        if (elapsed > 25) slow_writes = slow_writes + 1;
        if (ret != 0 && ret != (int)bytes) {
            write_errors = write_errors + 1;
            test_state = I2S_TEST_WRITE_ERROR;
        } else if (test_state == I2S_TEST_WRITE_ERROR) {
            test_state = I2S_TEST_RUNNING;
        }

        if (millis() - last_log_ms >= 2000) {
            last_log_ms = millis();
            LILYGO_LOG_PRINTF("I2S TEST source=%lu output=%lu signal=%s tone=%lu frames=%lu writes=%lu err=%lu slow=%lu max=%lums amp=%u\n",
                          (unsigned long)source_rate,
                          (unsigned long)output_rate,
                          signal_text(signal),
                          (unsigned long)tone_hz,
                          (unsigned long)frames_written,
                          (unsigned long)writes_done,
                          (unsigned long)write_errors,
                          (unsigned long)slow_writes,
                          (unsigned long)max_write_ms,
                          (unsigned)amplitude_percent);
        }

        taskYIELD();
    }

    test_state = I2S_TEST_STOPPING;
    out->close();
    free(buffer);
    LILYGO_LOG_PRINTLN("I2S TEST stopped");
    test_state = I2S_TEST_STOPPED;
    test_task = NULL;
    delete_current_test_task();
}

static void stop_test(bool wait)
{
    stop_requested = true;
    if (wait) {
        uint32_t start = millis();
        while (test_task && millis() - start < 1500) {
            delay(10);
        }
    }
}

static void start_test(uint32_t source_rate, uint32_t output_rate)
{
    stop_test(true);
    hw_set_play_stop();

    stop_requested = false;
    reset_stats(source_rate, output_rate);
    test_state = I2S_TEST_STARTING;
    uint32_t arg = pack_test_arg(source_rate, output_rate);

    BaseType_t ok = pdFAIL;
    test_task_stack_in_psram = false;

#if defined(ARDUINO) && defined(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM)
    test_task_stack_in_psram = true;
    ok = xTaskCreatePinnedToCoreWithCaps(i2s_test_task, "i2sTest",
                                         TEST_TASK_STACK_BYTES, (void *)(uintptr_t)arg,
                                         2, &test_task, 0,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        test_task_stack_in_psram = false;
        LILYGO_LOG_PRINTF("I2S TEST PSRAM stack task create failed:%d\n", (int)ok);
        log_i2s_heap("psram stack allocation failed");
    }
#endif

    if (ok != pdPASS) {
        ok = xTaskCreatePinnedToCore(i2s_test_task, "i2sTest",
                                     TEST_TASK_STACK_BYTES, (void *)(uintptr_t)arg,
                                     2, &test_task, 0);
    }
    if (ok != pdPASS) {
        test_task = NULL;
        test_state = I2S_TEST_NO_TASK_MEMORY;
        log_i2s_heap("task allocation failed");
    }
}

static void rate_button_event(lv_event_t *e)
{
    uint32_t source_rate = 0;
    uint32_t output_rate = 0;
    unpack_test_arg((uint32_t)(uintptr_t)lv_event_get_user_data(e), &source_rate, &output_rate);
    hw_feedback();
    start_test(source_rate, output_rate);
}

static void stop_button_event(lv_event_t *e)
{
    (void)e;
    hw_feedback();
    stop_test(false);
}

static void volume_slider_event(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    uint8_t volume = (uint8_t)lv_slider_get_value(slider);
    hw_set_volume(volume);
    if (volume_label) {
        lv_label_set_text_fmt(volume_label, "%u%%", volume);
    }
}

static void amplitude_slider_event(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    amplitude_percent = (uint8_t)lv_slider_get_value(slider);
    if (amplitude_label) {
        lv_label_set_text_fmt(amplitude_label, "%u%%", amplitude_percent);
    }
}

static void signal_dropdown_event(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target_obj(e);
    selected_signal = (uint8_t)lv_dropdown_get_selected(dd);
    hw_feedback();
}

static lv_obj_t *create_test_button(lv_obj_t *parent, const char *text,
    uint32_t source_rate, uint32_t output_rate, lv_color_t color)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 82, 34);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 7, 0);
    ui_add_accent_focus_style(btn);
    lv_obj_add_event_cb(btn, rate_button_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)pack_test_arg(source_rate, output_rate));

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return btn;
}

static void status_timer_cb(lv_timer_t *t)
{
    (void)t;
    i2s_test_state_t state = test_state;
    if (state_label) {
        lv_label_set_text(state_label, state_text(state));
    }
    if (rate_label) {
        uint32_t source_rate = current_source_rate;
        uint32_t output_rate = current_output_rate;
        if (output_rate) {
            lv_label_set_text_fmt(rate_label, "src %lu -> out %lu / %s / %s",
                                  (unsigned long)source_rate,
                                  (unsigned long)output_rate,
                                  signal_text(selected_signal),
                                  clock_note(source_rate, output_rate));
        } else {
            lv_label_set_text_fmt(rate_label, "%s / --", signal_text(selected_signal));
        }
    }
    if (metrics_label) {
        lv_label_set_text_fmt(metrics_label, "writes %lu  err %lu  slow %lu  max %lums",
                              (unsigned long)writes_done,
                              (unsigned long)write_errors,
                              (unsigned long)slow_writes,
                              (unsigned long)max_write_ms);
    }
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    stop_test(true);
    if (status_timer) {
        lv_timer_del(status_timer);
        status_timer = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    state_label = NULL;
    rate_label = NULL;
    metrics_label = NULL;
    volume_label = NULL;
    amplitude_label = NULL;
    signal_dropdown = NULL;
    menu_show();
}

void ui_i2s_test_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "I2S Test", back_event_handler);
    ui_app_page_disable_nav_auto_hide(page_container);

    stop_requested = false;
    reset_stats(0, 0);
    test_state = I2S_TEST_IDLE;

    lv_obj_t *card = ui_create_card(page_container, "Direct PCM");
    lv_obj_t *row = ui_create_card_info(card, LV_SYMBOL_AUDIO, "State", "Idle");
    state_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Signal", "--");
    rate_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(card, LV_SYMBOL_LIST, "Metrics", "--");
    metrics_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    if (metrics_label) {
        lv_obj_set_width(metrics_label, 210);
        lv_label_set_long_mode(metrics_label, LV_LABEL_LONG_DOT);
    }

    card = ui_create_card(page_container, "Signal");
    signal_dropdown = lv_dropdown_create(card);
    lv_dropdown_set_options(signal_dropdown, I2S_SIGNAL_LIST);
    lv_dropdown_set_selected(signal_dropdown, selected_signal);
    lv_obj_add_event_cb(signal_dropdown, signal_dropdown_event, LV_EVENT_VALUE_CHANGED, NULL);
    ui_create_card_item(card, LV_SYMBOL_AUDIO, "Wave", signal_dropdown);

    card = ui_create_card(page_container, "Sample Rate");
    lv_obj_t *buttons = lv_obj_create(card);
    lv_obj_set_size(buttons, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(buttons, 0, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_pad_column(buttons, 6, 0);
    lv_obj_set_style_pad_row(buttons, 6, 0);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    create_test_button(buttons, "16k", 16000, 16000, UI_COLOR_ACCENT);
    create_test_button(buttons, "32k", 32000, 32000, lv_color_hex(0x0077CC));
    create_test_button(buttons, "44.1N", 44100, 44100, lv_color_hex(0xAA55FF));
    create_test_button(buttons, "44.1>16", 44100, 16000, lv_color_hex(0x00A884));
    create_test_button(buttons, "44.1>48", 44100, 48000, lv_color_hex(0x2FB344));
    create_test_button(buttons, "48k", 48000, 48000, lv_color_hex(0xFF6B35));

    lv_obj_t *stop_btn = lv_btn_create(buttons);
    lv_obj_set_size(stop_btn, 70, 34);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(stop_btn, 0, 0);
    lv_obj_set_style_radius(stop_btn, 7, 0);
    lv_obj_add_event_cb(stop_btn, stop_button_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "Stop");
    lv_obj_center(stop_label);

    card = ui_create_card(page_container, "Level");
    lv_obj_t *slider = ui_create_card_slider(card, LV_SYMBOL_VOLUME_MAX, "Volume",
                                             0, 100, hw_get_volume(), volume_slider_event);
    lv_obj_t *vol_row = lv_obj_get_parent(slider);
    volume_label = lv_label_create(vol_row);
    lv_label_set_text_fmt(volume_label, "%u%%", hw_get_volume());
    lv_obj_set_style_text_color(volume_label, UI_COLOR_TEXT_SECONDARY, 0);

    slider = ui_create_card_slider(card, LV_SYMBOL_AUDIO, "Amplitude",
                                   5, 80, amplitude_percent, amplitude_slider_event);
    lv_obj_t *amp_row = lv_obj_get_parent(slider);
    amplitude_label = lv_label_create(amp_row);
    lv_label_set_text_fmt(amplitude_label, "%u%%", amplitude_percent);
    lv_obj_set_style_text_color(amplitude_label, UI_COLOR_TEXT_SECONDARY, 0);

    status_timer = lv_timer_create(status_timer_cb, 300, NULL);
}

void ui_i2s_test_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_i2s_test_main = {
    .setup_func_cb = ui_i2s_test_enter,
    .exit_func_cb = ui_i2s_test_exit,
    .user_data = nullptr,
};

#endif
