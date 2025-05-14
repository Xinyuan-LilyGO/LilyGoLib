/**
 * @file      ui_audio.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

vector <string>music_list;
static lv_timer_t *timer = NULL;
static lv_obj_t *last_play_obj = NULL;
static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        hw_set_play_stop();
        lv_obj_clean(menu);
        lv_obj_del(menu);
        last_play_obj = NULL;

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu_show();
    }
}


static void audio_play_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *symbol = (lv_obj_t *)lv_event_get_user_data(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t *list = lv_obj_get_parent(obj);
        const char *item = lv_list_get_btn_text(list, obj);

        char *text = lv_label_get_text(symbol);
        if (strcmp(text, LV_SYMBOL_PLAY) == 0) {

            if (last_play_obj != NULL) {
                lv_label_set_text(last_play_obj, LV_SYMBOL_PLAY);
            }

            if (last_play_obj == symbol) {
                lv_label_set_text(symbol, LV_SYMBOL_PAUSE);
                hw_set_sd_music_resume();

            } else {
                lv_label_set_text(symbol, LV_SYMBOL_PAUSE);
                last_play_obj = symbol;
                hw_set_sd_music_play(item);
                if (item) {
                    printf("%s\n", item);
                }

                if (timer) {
                    lv_timer_del(timer);
                }
                timer =  lv_timer_create([](lv_timer_t *t) {
                    if (!hw_player_running()) {
                        if (last_play_obj) {
                            lv_label_set_text(last_play_obj, LV_SYMBOL_PLAY);
                            lv_timer_del(t);
                            timer = NULL;
                            last_play_obj = NULL;
                        }
                    }
                }, 500, NULL);
            }
        } else {
            lv_label_set_text(symbol, LV_SYMBOL_PLAY);
            hw_set_sd_music_pause();
        }
    }
}


void ui_audio_enter(lv_obj_t *parent)
{
    music_list.clear();
    menu = create_menu(parent, back_event_handler);


    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_size(main_page, lv_pct(100), lv_pct(100));

    hw_get_sd_music(music_list);

    if (!music_list.size()) {
        lv_obj_t *cont = lv_obj_create(main_page);
        lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
        lv_obj_center(cont);
        lv_obj_set_style_border_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);

        LV_IMG_DECLARE(img_cry);
        lv_obj_t *img = lv_img_create(cont);
        lv_img_set_src(img, &img_cry);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, lv_pct(10));

        lv_obj_t *label = lv_label_create(cont);
        lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
        lv_obj_set_width(label, LV_PCT(80));

#ifdef HAS_SD_CARD_SOCKET
        lv_label_set_text(label, "No MP3 or WAV files found.\nPlease put the audio files into the SD card.");
#else
        lv_label_set_text(label, "No MP3 or WAV file found in the file system");
#endif

        lv_menu_set_page(menu, main_page);
        lv_obj_align_to(label, img, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

#ifdef USING_TOUCHPAD
        quit_btn  = create_floating_button([](lv_event_t*e) {
            lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
        }, NULL);
#endif

        return;
    }

    /*Create a list*/
    lv_obj_t *list1 = lv_list_create(main_page);
    lv_obj_set_size(list1, lv_pct(100), lv_pct(100));
    lv_obj_center(list1);

    /*Add buttons to the list*/
    lv_obj_t *obj, *label;
    lv_list_add_text(list1, "File");

    for (auto file : music_list) {
        obj = lv_list_add_btn(list1, LV_SYMBOL_AUDIO, file.c_str());
        label = lv_label_create(obj);
        lv_label_set_text(label, LV_SYMBOL_PLAY);
        lv_obj_add_event_cb(obj, audio_play_event, LV_EVENT_CLICKED, label);
    }
    lv_menu_set_page(menu, main_page);


#ifdef USING_TOUCHPAD
    quit_btn  = create_floating_button([](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif

}

void ui_audio_exit(lv_obj_t *parent)
{

}

app_t ui_audio_main = {
    .setup_func_cb = ui_audio_enter,
    .exit_func_cb = ui_audio_exit,
    .user_data = nullptr,
};


