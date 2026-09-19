/**
 * @file      ui_msg.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include <LilyGoLog.h>
#include "ui_define.h"

static lv_obj_t *msgbox = NULL;

static void msgbox_event(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_current_target(e);
    LILYGO_LOG_PRINTF("msgbox event destroy\n");
    destroy_msgbox(msgbox);
    msgbox = NULL;
    set_low_power_mode_flag(true);
}

void ui_msg_pop_up(const char *title_txt, const char *msg_txt)
{

    set_low_power_mode_flag(false);

    static const char *btns[] = {"Close", ""};
    if (msgbox) {
        LILYGO_LOG_PRINTF("msg box has create!\n");
        return;
    }
    LILYGO_LOG_PRINTF("create_msgbox..\n");
    msgbox = create_msgbox(lv_scr_act(), title_txt,
                           msg_txt, btns,
                           msgbox_event, NULL);
}



static lv_timer_t *processBarTimer = NULL;
static uint32_t prevTick = 0;

static void _ui_process_bar_cb(lv_timer_t*t)
{
    lv_obj_t *bar = (lv_obj_t *)lv_timer_get_user_data(t);

    char wifi_ip[24];
    hw_get_ip_address(wifi_ip, sizeof(wifi_ip));
    bool connected = hw_get_wifi_connected() && strcmp(wifi_ip, "N.A") != 0 && strcmp(wifi_ip, "0.0.0.0") != 0;

    if (connected || lv_tick_get() > prevTick) {

        lv_obj_t *cont =  lv_obj_get_parent(bar);
        lv_obj_del(cont);
        lv_timer_del(processBarTimer);
        processBarTimer = NULL;
        enable_input_devices();
        lv_disp_trig_activity(NULL);

        if (connected) {

            char wifi_ssid[WIFI_SSID_MAX_LEN];
            hw_get_wifi_ssid(wifi_ssid, sizeof(wifi_ssid));

            char msg[128];
            snprintf(msg, sizeof(msg), "Connected %s,IP:%s", wifi_ssid, wifi_ip);
            ui_msg_pop_up("WiFi", msg);

        } else {

            wl_status_t status = hw_get_wifi_status();
            switch (status) {
#ifdef ARDUINO
#if ESP_IDF_VERSION <ESP_IDF_VERSION_VAL(5,0,0)
            case WL_IDLE_STATUS:
#else
            case WL_STOPPED:
#endif
                ui_msg_pop_up("WiFi", "Connection failed, WIFI is not activated.");
                break;
#endif
            case WL_NO_SSID_AVAIL:
                ui_msg_pop_up("WiFi", "Connection failed. The SSID was not found.");
                break;
            case WL_CONNECT_FAILED:
                ui_msg_pop_up("WiFi", "Connection failed, please check the password.");
                break;
            default:
                ui_msg_pop_up("WiFi", "Connection failed.");
                break;
            }
        }

        set_low_power_mode_flag(true);

        return ;
    }
    int32_t val = lv_bar_get_value(bar) + 5;
    lv_bar_set_value(bar, val, LV_ANIM_ON);
}


void ui_show_wifi_process_bar()
{
    if (processBarTimer) {
        LILYGO_LOG_PRINTF("Timer is running");
        return;
    }
    prevTick = lv_tick_get() + 10000;
    disable_input_devices();
    lv_obj_t *bar = ui_create_process_bar(lv_scr_act(), "WiFi connecting...");
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    processBarTimer = lv_timer_create(_ui_process_bar_cb, 500, bar);
    lv_timer_ready(processBarTimer);
}
