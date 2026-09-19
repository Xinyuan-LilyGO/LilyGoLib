/**
 * @file      ui_net_radio.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-08-13
 * 
 * HTTP MP3 network radio player.
 */
#include "ui_define.h"

#if !defined(EXCLUDE_AUDIO_PLAYER)

#define NETWORK_AUDIO_UI_MAX_STREAMS 24
#define NETWORK_AUDIO_CONFIG_PATH_TEXT "/radio_streams.txt"

#ifdef USING_AUDIO_CODEC
#define HAS_VOLUME_SLIDER
#endif

static vector<NetworkAudioStreamParams_t> stream_list;
static lv_timer_t *timer = NULL;
static lv_obj_t *last_play_obj = NULL;
static lv_obj_t *page_container = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *wifi_label = NULL;
static lv_obj_t *count_label = NULL;
static lv_obj_t *wifi_msgbox = NULL;
static bool stream_requested = false;
static bool stream_started = false;
static bool stream_paused = false;
static uint32_t stream_request_time = 0;

static void reset_play_state(const char *status)
{
    if (last_play_obj) {
        lv_label_set_text(last_play_obj, LV_SYMBOL_PLAY);
    }
    last_play_obj = NULL;
    stream_requested = false;
    stream_started = false;
    stream_paused = false;
    if (status_label) {
        lv_label_set_text(status_label, status);
    }
}

static void back_event_handler(lv_event_t *e)
{
    hw_set_play_stop_async();
    if (wifi_msgbox) {
        destroy_msgbox(wifi_msgbox);
        wifi_msgbox = NULL;
    }
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    last_play_obj = NULL;
    if (quit_btn) {
        lv_obj_del_async(quit_btn);
        quit_btn = NULL;
    }
    status_label = NULL;
    wifi_label = NULL;
    count_label = NULL;
    menu_show();
}

static void wifi_msgbox_event(lv_event_t *e)
{
    if (wifi_msgbox) {
        destroy_msgbox(wifi_msgbox);
        wifi_msgbox = NULL;
        back_event_handler(NULL);
    }
}

static void show_wifi_offline_msgbox()
{
    if (wifi_msgbox) {
        return;
    }

    static const char *btns[] = {"OK", ""};
    wifi_msgbox = create_msgbox(lv_scr_act(), "WiFi Offline",
                                "Network radio requires WiFi.\nPlease connect to WiFi first.",
                                btns, wifi_msgbox_event, NULL);
}

static void update_status_timer(lv_timer_t *t)
{
    if (wifi_label) {
        lv_label_set_text(wifi_label, hw_get_wifi_connected() ? "Connected" : "Offline");
    }

    if (!stream_requested || !last_play_obj) {
        return;
    }

    bool running = hw_player_running();
    if (running) {
        stream_started = true;
        if (!stream_paused && status_label) {
            lv_label_set_text(status_label, "Playing");
        }
        return;
    }

    if (stream_started || lv_tick_elaps(stream_request_time) > 10000) {
        reset_play_state(stream_started ? "Stopped" : "Failed");
    } else if (!stream_paused && status_label) {
        lv_label_set_text(status_label, "Connecting");
    }
}

static void stream_play_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *symbol = (lv_obj_t *)lv_event_get_user_data(e);
    if (code != LV_EVENT_CLICKED || !obj || !symbol) {
        return;
    }

    NetworkAudioStreamParams_t *stream = (NetworkAudioStreamParams_t *)lv_obj_get_user_data(obj);
    if (!stream) {
        return;
    }

    const char *text = lv_label_get_text(symbol);
    if (strcmp(text, LV_SYMBOL_PLAY) == 0) {
        if (last_play_obj && last_play_obj != symbol) {
            lv_label_set_text(last_play_obj, LV_SYMBOL_PLAY);
        }
        lv_label_set_text(symbol, LV_SYMBOL_PAUSE);

        if (last_play_obj == symbol && stream_paused) {
            stream_paused = false;
            hw_set_sd_music_resume();
            if (status_label) {
                lv_label_set_text(status_label, "Playing");
            }
            return;
        }

        last_play_obj = symbol;
        stream_requested = true;
        stream_started = false;
        stream_paused = false;
        stream_request_time = lv_tick_get();
        if (status_label) {
            lv_label_set_text(status_label, hw_get_wifi_connected() ? "Connecting" : "WiFi offline");
        }
        hw_set_network_audio_stream_play(stream->name, stream->url);
    } else {
        lv_label_set_text(symbol, LV_SYMBOL_PLAY);
        stream_paused = true;
        hw_set_sd_music_pause();
        if (status_label) {
            lv_label_set_text(status_label, "Paused");
        }
    }
}

static void volume_slider_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        hw_set_volume(lv_slider_get_value(obj));
    }
}

static void create_volume_row(lv_obj_t *card)
{
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_top(row, 6, 0);
    lv_obj_set_style_pad_bottom(row, 6, 0);
    lv_obj_set_style_pad_left(row, 0, 0);
    lv_obj_set_style_pad_right(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_label_create(row);
    lv_label_set_text(icon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(icon, UI_COLOR_ACCENT, 0);

    lv_obj_t *title = lv_label_create(row);
    lv_label_set_text(title, "Vol");
    lv_obj_set_width(title, 34);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_width(slider, 1);
    lv_obj_set_flex_grow(slider, 1);
    lv_slider_set_value(slider, hw_get_volume(), LV_ANIM_OFF);
    lv_slider_set_range(slider, 0, 100);
    ui_prepare_slider_for_encoder(slider);
    lv_obj_add_event_cb(slider, volume_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_height(slider, 8, LV_PART_MAIN);
    lv_obj_set_style_height(slider, 8, LV_PART_INDICATOR);
    lv_obj_set_style_size(slider, 16, 16, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);

    lv_group_t *group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, slider);
    }

    lv_obj_t *div = lv_obj_create(card);
    lv_obj_set_size(div, LV_PCT(95), 1);
    lv_obj_set_style_bg_color(div, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);
}

static void create_stream_row(lv_obj_t *card, NetworkAudioStreamParams_t *stream)
{
#if defined(USING_TOUCHPAD) || defined(HAS_TOUCHSCREEN)
    lv_obj_t *row = ui_create_card_info(card, LV_SYMBOL_AUDIO, stream->name, LV_SYMBOL_PLAY);
    lv_obj_t *label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    lv_obj_set_user_data(row, stream);
    lv_obj_add_event_cb(row, stream_play_event, LV_EVENT_CLICKED, label);
#else
    lv_group_t *group = lv_group_get_default();
    lv_obj_t *btn = lv_btn_create(card);
    lv_obj_set_size(btn, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 8, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(icon, UI_COLOR_ACCENT, 0);

    lv_obj_t *name = lv_label_create(btn);
    lv_label_set_text(name, stream->name);
    lv_obj_set_style_text_color(name, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    lv_obj_t *play = lv_label_create(btn);
    lv_label_set_text(play, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play, UI_COLOR_ACCENT, 0);

    lv_obj_set_user_data(btn, stream);
    lv_obj_add_event_cb(btn, stream_play_event, LV_EVENT_CLICKED, play);
    if (group) {
        lv_group_add_obj(group, btn);
    }
#endif
}

void ui_net_radio_enter(lv_obj_t *parent)
{
    stream_list.clear();
    last_play_obj = NULL;
    status_label = NULL;
    wifi_label = NULL;
    count_label = NULL;
    wifi_msgbox = NULL;
    stream_requested = false;
    stream_started = false;
    stream_paused = false;
    hw_get_network_audio_streams(stream_list);

    page_container = ui_create_app_page(parent, "Net Radio", back_event_handler);

    lv_obj_t *card = ui_create_card(page_container, "Status");
    lv_obj_t *row = ui_create_card_info(card, LV_SYMBOL_WIFI, "WiFi", hw_get_wifi_connected() ? "Connected" : "Offline");
    wifi_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(card, LV_SYMBOL_AUDIO, "State", "Ready");
    status_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(card, LV_SYMBOL_LIST, "Streams", "0");
    count_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    ui_create_card_info(card, LV_SYMBOL_SD_CARD, "Config", NETWORK_AUDIO_CONFIG_PATH_TEXT);
    lv_label_set_text_fmt(count_label, "%u", (uint32_t)stream_list.size());

    card = ui_create_card(page_container, "Streams");
    uint32_t stream_count = stream_list.size();
    if (stream_count > NETWORK_AUDIO_UI_MAX_STREAMS) {
        stream_count = NETWORK_AUDIO_UI_MAX_STREAMS;
    }
    for (uint32_t i = 0; i < stream_count; ++i) {
        create_stream_row(card, &stream_list[i]);
    }

#ifdef HAS_VOLUME_SLIDER
    card = ui_create_card(page_container, "Volume");
    create_volume_row(card);
#endif

    if (!hw_get_wifi_connected()) {
        show_wifi_offline_msgbox();
    }

    timer = lv_timer_create(update_status_timer, 500, NULL);

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        hw_feedback();
        back_event_handler(e);
    }, NULL);
#endif
}

void ui_net_radio_exit(lv_obj_t *parent)
{
}

app_t ui_net_radio_main = {
    .setup_func_cb = ui_net_radio_enter,
    .exit_func_cb = ui_net_radio_exit,
    .user_data = nullptr,
};

#endif /* EXCLUDE_AUDIO_PLAYER */
