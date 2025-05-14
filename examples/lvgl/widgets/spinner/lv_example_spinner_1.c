#include <lvgl.h>
#if LV_USE_SPINNER

void lv_example_spinner_1(void)
{
    /*Create a spinner*/
    lv_obj_t *spinner = lv_spinner_create(lv_screen_active());
    lv_obj_set_size(spinner, 100, 100);
    lv_obj_center(spinner);
    lv_spinner_set_anim_params(spinner, 10000, 200);
}

#endif
