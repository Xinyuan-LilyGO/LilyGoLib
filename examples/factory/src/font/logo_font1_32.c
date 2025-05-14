/*******************************************************************************
 * Size: 32 px
 * Bpp: 1
 * Opts: --bpp 1 --size 32 --no-compress --font HFSnowy-2.ttf --symbols LoRa Pager --format lvgl -o logo_font1_32.c
 ******************************************************************************/

#include "lvgl.h"

#ifndef LOGO_FONT1_32
#define LOGO_FONT1_32 1
#endif

#if LOGO_FONT1_32

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+004C "L" */
    0xff, 0x3, 0xc0, 0x3c, 0x3, 0x40, 0x3c, 0x3,
    0xc0, 0x34, 0x3, 0xc0, 0x3c, 0x3, 0xc0, 0x34,
    0x3, 0xc0, 0x3c, 0x3, 0x40, 0x2c, 0x3, 0xc0,
    0x34, 0x3, 0xc0, 0x3c, 0x3, 0xc0, 0x34, 0x13,
    0xc1, 0x3c, 0x1f, 0xff,

    /* U+0050 "P" */
    0xff, 0x3, 0xcc, 0x34, 0x23, 0xc2, 0x3c, 0x13,
    0x41, 0x3c, 0x13, 0x41, 0x3c, 0x12, 0xc1, 0x34,
    0x33, 0xc2, 0x3c, 0x22, 0xc4, 0x3c, 0x83, 0x70,
    0x34, 0x2, 0xc0, 0x3c, 0x3, 0x40, 0x3c, 0x3,
    0xc0, 0x34, 0xf, 0xf0,

    /* U+0052 "R" */
    0xff, 0x80, 0xf1, 0x3, 0x42, 0xf, 0x4, 0x3c,
    0x10, 0xd0, 0x43, 0xc1, 0xb, 0x4, 0x3c, 0x10,
    0xd0, 0x43, 0xc2, 0xf, 0x8, 0x34, 0x40, 0xf2,
    0x3, 0xf0, 0xf, 0x20, 0x34, 0x80, 0xf2, 0x3,
    0xc4, 0xd, 0x10, 0x3c, 0x58, 0xf1, 0x73, 0x42,
    0x7f, 0x8e,

    /* U+0061 "a" */
    0x3d, 0xd4, 0xc7, 0x13, 0xc4, 0xb1, 0x34, 0x4b,
    0x13, 0x44, 0xf1, 0x3c, 0x4f, 0x13, 0xcc, 0x77,
    0xf, 0x70,

    /* U+0065 "e" */
    0x3c, 0x72, 0x51, 0xf1, 0xb1, 0xd1, 0xfe, 0xb0,
    0xd0, 0xf0, 0xf1, 0x51, 0x73, 0x1e,

    /* U+0067 "g" */
    0x3d, 0xd4, 0xc7, 0x13, 0xc4, 0xd1, 0x3c, 0x4f,
    0x12, 0xc4, 0xd1, 0x3c, 0x4d, 0x11, 0xcc, 0x77,
    0xf, 0x40, 0x11, 0xc4, 0x99, 0x20, 0x48, 0x12,
    0x8, 0x46, 0xf, 0x0,

    /* U+006F "o" */
    0x3e, 0x39, 0x9c, 0x5a, 0x3f, 0xf, 0x87, 0x43,
    0xe1, 0xb0, 0xe8, 0x7c, 0x2e, 0x25, 0x31, 0xf0,

    /* U+0072 "r" */
    0xfd, 0xc6, 0xc8, 0xf9, 0x1e, 0x3, 0xc0, 0x68,
    0xf, 0x1, 0xe0, 0x3c, 0x6, 0x80, 0xf0, 0x1e,
    0x3, 0xc1, 0xfe, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 131, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 211, .box_w = 12, .box_h = 24, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 37, .adv_w = 222, .box_w = 12, .box_h = 24, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 73, .adv_w = 242, .box_w = 14, .box_h = 24, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 115, .adv_w = 181, .box_w = 10, .box_h = 14, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 133, .adv_w = 155, .box_w = 8, .box_h = 14, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 147, .adv_w = 174, .box_w = 10, .box_h = 22, .ofs_x = 1, .ofs_y = -8},
    {.bitmap_index = 175, .adv_w = 176, .box_w = 9, .box_h = 14, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 191, .adv_w = 173, .box_w = 11, .box_h = 14, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x2c, 0x30, 0x32, 0x41, 0x45, 0x47, 0x4f,
    0x52
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 83, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 9, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t logo_font1_32 = {
#else
lv_font_t logo_font1_32 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 32,          /*The maximum line height required by the font*/
    .base_line = 8,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -9,
    .underline_thickness = 2,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if LOGO_FONT1_32*/

