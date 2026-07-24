/*******************************************************************************
 * Size: 12 px
 * Bpp: 4
 * Opts: --bpp 4 --size 12 --no-compress --stride 1 --align 1 --font JetBrainsMono-Regular.ttf --symbols ␣←●↓↑✓✗⚡○ --format lvgl -o usr_montserrat_12.c
 ******************************************************************************/

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif



#ifndef USR_MONTSERRAT_12
#define USR_MONTSERRAT_12 1
#endif

#if USR_MONTSERRAT_12

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+2190 "←" */
    0x0, 0x1, 0x0, 0x0, 0xb, 0x80, 0x0, 0x8,
    0xb0, 0x0, 0x4, 0xe4, 0x33, 0x33, 0xae, 0xbb,
    0xbb, 0xb0, 0xd5, 0x0, 0x0, 0x2, 0xe2, 0x0,
    0x0, 0x4, 0x70, 0x0,

    /* U+2191 "↑" */
    0x0, 0x1a, 0x20, 0x0, 0x2d, 0xfe, 0x40, 0x3e,
    0x5e, 0x4d, 0x57, 0x30, 0xe1, 0x18, 0x0, 0xe,
    0x10, 0x0, 0x0, 0xe1, 0x0, 0x0, 0xe, 0x10,
    0x0, 0x0, 0xe1, 0x0, 0x0, 0xe, 0x10, 0x0,

    /* U+2193 "↓" */
    0x0, 0xa, 0x0, 0x0, 0x0, 0xe1, 0x0, 0x0,
    0xe, 0x10, 0x0, 0x0, 0xe1, 0x0, 0x0, 0xe,
    0x10, 0x5, 0x10, 0xe1, 0x6, 0x5d, 0x2e, 0x2b,
    0x80, 0x4d, 0xed, 0x70, 0x0, 0x3e, 0x50, 0x0,
    0x0, 0x0, 0x0,

    /* U+2423 "␣" */
    0x1, 0x0, 0x0, 0x24, 0xc0, 0x0, 0x2f, 0x4f,
    0xff, 0xff, 0xf0,

    /* U+25CB "○" */
    0x3, 0xa9, 0x99, 0x0, 0x39, 0x0, 0x2, 0xa0,
    0xa0, 0x0, 0x0, 0x45, 0x90, 0x0, 0x0, 0x8,
    0x90, 0x0, 0x0, 0x18, 0x82, 0x0, 0x0, 0x73,
    0xa, 0x40, 0x17, 0x70, 0x0, 0x59, 0x83, 0x0,

    /* U+25CF "●" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x6, 0xdf, 0xe8,
    0x0, 0x6, 0xff, 0xff, 0xf9, 0x0, 0xef, 0xff,
    0xff, 0xf1, 0x1f, 0xff, 0xff, 0xff, 0x40, 0xff,
    0xff, 0xff, 0xf3, 0xa, 0xff, 0xff, 0xfd, 0x0,
    0x1b, 0xff, 0xfd, 0x20, 0x0, 0x3, 0x64, 0x0,
    0x0,

    /* U+26A1 "⚡" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x2, 0x30, 0x0,
    0x0, 0xa0, 0x0, 0x0, 0xb6, 0x0, 0x0, 0x9e,
    0x0, 0x0, 0x7f, 0xc8, 0x85, 0x4f, 0xff, 0xfd,
    0x10, 0x11, 0xbe, 0x20, 0x0, 0x4e, 0x20, 0x0,
    0xc, 0x20, 0x0, 0x6, 0x30, 0x0, 0x0, 0x20,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0,

    /* U+2713 "✓" */
    0x0, 0x0, 0x1, 0x20, 0x0, 0x0, 0xb7, 0x0,
    0x0, 0x6c, 0x2, 0x60, 0x2e, 0x20, 0x3e, 0x7c,
    0x70, 0x0, 0x3e, 0xc0, 0x0, 0x0, 0x22, 0x0,
    0x0,

    /* U+2717 "✗" */
    0x12, 0x0, 0x1, 0x34, 0xe2, 0x1, 0xd6, 0x4,
    0xe4, 0xd7, 0x0, 0x5, 0xf8, 0x0, 0x1, 0xd9,
    0xe2, 0x1, 0xd7, 0x4, 0xe2, 0x47, 0x0, 0x4,
    0x70
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 28, .adv_w = 115, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 60, .adv_w = 115, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 95, .adv_w = 115, .box_w = 7, .box_h = 3, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 106, .adv_w = 115, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 138, .adv_w = 115, .box_w = 9, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 179, .adv_w = 115, .box_w = 7, .box_h = 13, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 225, .adv_w = 115, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 250, .adv_w = 115, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x1, 0x3, 0x293, 0x43b, 0x43f, 0x511, 0x583,
    0x587
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 8592, .range_length = 1416, .glyph_id_start = 1,
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
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif

};

extern const lv_font_t lv_font_montserrat_12;


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t usr_montserrat_12 = {
#else
lv_font_t usr_montserrat_12 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 13,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .static_bitmap = 0,
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = &lv_font_montserrat_12,
#endif
    .user_data = NULL,
};



#endif /*#if USR_MONTSERRAT_12*/
