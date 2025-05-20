/**
 * @file      LilyGoDispInterface.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-12
 *
 */
#include "LilyGoDispInterface.h"
#include <Arduino.h>
#include <vector>
#include <bsp_lcd/esp_lcd_st7796.h>

#define DISP_CMD_MADCTL       (0x36) // Memory data access control
#define DISP_CMD_CASET        (0x2A) // Set column address
#define DISP_CMD_RASET        (0x2B) // Set row address
#define DISP_CMD_RAMWR        (0x2C) // Write frame memory
#define DISP_CMD_SLPIN        (0x10) // Go into sleep mode (DC/DC, oscillator, scanning stopped, but memory keeps content)
#define DISP_CMD_SLPOUT       (0x11)
#define DISP_CMD_MAD_MY       (0x80)
#define DISP_CMD_MAD_MX       (0x40)
#define DISP_CMD_MAD_MV       (0x20)
#define DISP_CMD_MAD_ML       (0x10)
#define DISP_CMD_MAD_MH       (0x04)
#define DISP_MAD_RGB          (0x00)
#define DISP_CMD_RGB          (0x08)
#define DISP_CMD_BRIGHTNESS   (0x51)


#define SEND_BUF_SIZE        (16384)


#ifdef ARDUINO_T_LORA_PAGER
#define SPI_DEV_HOST_ID     SPI2_HOST
#else
#define SPI_DEV_HOST_ID     SPI3_HOST
#endif

void LilyGoDispQSPI::end()
{
    spi_bus_remove_device(_spi_dev);
    spi_bus_free(SPI_DEV_HOST_ID);
}

void LilyGoDispQSPI::setGapOffset(uint16_t gap_x, uint16_t gap_y)
{
    _offset_x = gap_x;
    _offset_y = gap_y;
}

static volatile bool disp_tearing_effect = false;
static volatile uint8_t frame_count = 0;

static  void ICACHE_RAM_ATTR disp_te_isr()
{
    disp_tearing_effect = true;
    frame_count++;
}

bool LilyGoDispQSPI:: init(int rst, int cs, int te, int sck, int d0, int d1, int d2, int d3,  uint32_t freq_Mhz)
{
    assert(_disp_init_cmd);

    _cs  = cs;

    pinMode(cs, OUTPUT);

    if (_use_tearing_effect) {
        if (te != -1) {
            attachInterrupt(te, disp_te_isr, RISING);
        }
    }

    if (rst != -1) {
        pinMode(rst, OUTPUT);
        digitalWrite(rst, HIGH);
        delay(200);
        digitalWrite(rst, LOW);
        delay(300);
        digitalWrite(rst, HIGH);
        delay(200);
    }

    spi_bus_config_t spi_config = {
        .data0_io_num = d0,
        .data1_io_num = d1,
        .sclk_io_num  = sck,
        .data2_io_num = d2,
        .data3_io_num = d3,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = (0x40000) + 8,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
    };

    log_d("RST:%d D0:%d D1:%u D2:%d D3:%d SCK:%d CS:%d",rst, d0, d1, d2, d3, sck, cs);

    spi_device_interface_config_t dev_config = {
        .command_bits = 8,
        .address_bits = 24,
        .mode = SPI_MODE0,
        .clock_speed_hz = static_cast<int>(freq_Mhz * 1000U * 1000U),
        .spics_io_num = -1,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 17,
        .pre_cb = NULL,
        .post_cb = NULL,
    };
    esp_err_t ret = spi_bus_initialize(SPI_DEV_HOST_ID, &spi_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        log_e("spi_bus_initialize fail!");
        assert(0);
    }
    ret = spi_bus_add_device(SPI_DEV_HOST_ID, &dev_config, &_spi_dev);
    if (ret != ESP_OK) {
        log_e("spi_bus_add_device fail!");
        assert(0);
    }

    for (int i = 0; i < 2; ++i) {
        const disp_cmd_t *t = _disp_init_cmd;
        for (uint32_t i = 0; i < _disp_init_cmd_len; i++) {
            writeCommand(t[i].addr, (uint8_t *)t[i].param, t[i].len & 0x1F);
            if (t[i].len & 0x80) {
                delay(120);
            }
        }
    }

    setRotation(0);

    std::vector<uint16_t> draw_buf(width * height * 2, 0x0);
    pushColors(0, 0, width, height, draw_buf.data());

    return true;
}

void LilyGoDispQSPI::setRotation(uint8_t rotation)
{
    uint8_t gbr = DISP_MAD_RGB;

    switch (rotation) {
    case 0: // Portrait
        if (_init_width == 502) {
            _offset_x = 22;
            _offset_y = 0;
        }
        width = _init_height;
        height = _init_width;
        gbr = DISP_MAD_RGB;
        break;
    case 1: // Landscape (Portrait + 90)
        if (_init_width == 502) {
            _offset_x = 10;
            _offset_y = 22;
        }
        width = _init_width;
        height = _init_height;
        gbr = DISP_CMD_MAD_MX  | gbr;
        break;
    case 2: // Inverter portrait
        if (_init_width == 502) {
            _offset_x = 22;
            _offset_y = 0;
        }
        width = _init_height;
        height = _init_width;
        gbr = DISP_CMD_MAD_MX | DISP_CMD_MAD_MY | gbr;
        break;
    case 3: // Inverted landscape
        if (_init_width == 502) {
            _offset_x = 10;
            _offset_y = 22;
        }
        width = _init_width;
        height = _init_height;
        gbr =  DISP_CMD_MAD_MY | gbr;
        break;
    default:
        break;
    }

    // _cfg.rotation = r;
    // log_d("disp_set_rotation Gap x-> %02d  y-> %02d\n", _offset_x, _offset_y);
    // log_d("disp_set_rotation 0X36 -> %02X\n", gbr);
    writeCommand(DISP_CMD_MADCTL, &gbr, 1);
}

uint8_t LilyGoDispQSPI::getRotation()
{
    return 0;
}

void LilyGoDispQSPI::setAddrWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
    xs += _offset_x;
    ys += _offset_y;
    xe += _offset_x;
    ye += _offset_y;
    disp_cmd_t t[3] = {
        {
            DISP_CMD_CASET, {
                highByte(xs), lowByte(xs),
                highByte(xe), lowByte(xe),
            }, 0x04
        },
        {
            DISP_CMD_RASET, {
                highByte(ys), lowByte(ys),
                highByte(ye), lowByte(ye),
            }, 0x04
        },
        {
            DISP_CMD_RAMWR, {
                0x00
            }, 0x00
        },
    };

    for (uint32_t i = 0; i < 3; i++) {
        writeCommand(t[i].addr, t[i].param, t[i].len);
    }
}

// Push (aka write pixel) colours to the TFT (use setAddrWindow() first)
void LilyGoDispQSPI::pushColorsNoDMA(uint16_t *data, uint32_t len)
{
    bool first_send = true;
    bool frame_top = 1;
    uint16_t *p = data;
    assert(p);
    assert(_spi_dev);
    digitalWrite(_cs, LOW);
    do {
        size_t chunk_size = len;
        spi_transaction_ext_t t = {0};
        memset(&t, 0, sizeof(t));
        if (first_send) {
            frame_top = 1;
            t.base.flags = SPI_TRANS_MODE_QIO;
            t.base.cmd = 0x32 ;
            t.base.addr = 0x002C00;
            first_send = 0;
        } else {
            t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
            t.command_bits = 0;
            t.address_bits = 0;
            t.dummy_bits = 0;
        }
        if (chunk_size > SEND_BUF_SIZE) {
            chunk_size = SEND_BUF_SIZE;
        }
        t.base.tx_buffer = p;
        t.base.length = chunk_size * 16;

        if (_use_tearing_effect) {
            if (frame_top == 1) {
                if (disp_tearing_effect == true) {
                    disp_tearing_effect = false;
                }
                while (disp_tearing_effect == false) ;
                frame_top = 0;
            }
        }

        esp_err_t  err = spi_device_polling_transmit(_spi_dev, (spi_transaction_t *)&t);
        if (err != ESP_OK) {
            log_e("failed , err :%d", err);
        }
        len -= chunk_size;
        p += chunk_size;
    } while (len > 0);
    digitalWrite(_cs, HIGH);
}

void LilyGoDispQSPI::pushColorsDMA(uint16_t *data, uint32_t len)
{
    bool first_send = true;
    bool frame_top = 1;
    uint16_t *p = data;
    assert(p);
    assert(_spi_dev);

    digitalWrite(_cs, LOW);

    while (len > 0) {
        size_t chunk_size = len;
        if (chunk_size > SEND_BUF_SIZE) {
            chunk_size = SEND_BUF_SIZE;
        }

        spi_transaction_ext_t t = {0};
        memset(&t, 0, sizeof(t));

        if (first_send) {
            frame_top = 1;
            t.base.flags = SPI_TRANS_MODE_QIO;
            t.base.cmd = 0x32;
            t.base.addr = 0x002C00;
            first_send = 0;
        } else {
            t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
            t.command_bits = 0;
            t.address_bits = 0;
            t.dummy_bits = 0;
        }
        t.base.tx_buffer = data;
        t.base.length = chunk_size * 16;

        if (_use_tearing_effect) {
            if (frame_top == 1) {
                if (disp_tearing_effect == true) {
                    disp_tearing_effect = false;
                }
                while (disp_tearing_effect == false) ;
                frame_top = 0;
            }
        }

        esp_err_t ret = spi_device_queue_trans(_spi_dev, &t.base, portMAX_DELAY);
        if (ret != ESP_OK) {
            log_e("DMA transfer failed!");
        }
        spi_transaction_t *trans_result;
        ret = spi_device_get_trans_result(_spi_dev, &trans_result, portMAX_DELAY);
        if (ret != ESP_OK) {
            log_e("DMA SPI transfer failed!");
        }
        data += chunk_size;
        len -= chunk_size;
    }
    digitalWrite(_cs, HIGH);
}

void LilyGoDispQSPI::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color)
{
    //acquire the bus to send polling transactions faster
    esp_err_t  err ;
    do {
        err = spi_device_acquire_bus(_spi_dev, portMAX_DELAY);
    } while (err != ESP_OK);
    setAddrWindow(x1, y1, x1 + x2 - 1, y1 + y2 - 1);
    if (_use_dma_transaction) {
        pushColorsDMA(color, x2 * y2);
    } else {
        pushColorsNoDMA(color, x2 * y2);
    }
    spi_device_release_bus(_spi_dev);
}

void LilyGoDispQSPI::writeCommand(uint32_t cmd, uint8_t *pdat, uint32_t length)
{
    digitalWrite(_cs, LOW);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.flags = (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR);
    t.cmd = 0x02;
    t.addr = cmd << 8;
    if (length != 0) {
        t.tx_buffer = pdat;
        t.length = 8 * length;
    } else {
        t.tx_buffer = NULL;
        t.length = 0;
    }
    spi_device_polling_transmit(_spi_dev, &t);
    digitalWrite(_cs, HIGH);
}

void LilyGoDispQSPI::sleep()
{
    writeCommand(DISP_CMD_SLPIN, NULL, 0);
}

void LilyGoDispQSPI::wakeup()
{
    writeCommand(DISP_CMD_SLPOUT, NULL, 0);
}

void LilyGoDispQSPI::setBrightness(uint8_t level)
{
    if (_brightness == level)return;
    if (!level) {
        // sleep();
    }
    if (_brightness == 0 && level != 0) {
        // writeCommand(0x11, NULL, 0);
    }
    _brightness = level;
    writeCommand(DISP_CMD_BRIGHTNESS, &level, 1);
}


//**  @brief  SPI SPI SPI  SPI SPI SPI  SPI SPI SPI
void LilyGoDispSPI::end()
{
    assert(panel_handle);
    esp_lcd_panel_del(panel_handle);
    esp_lcd_panel_io_del(io_handle);
    spi_bus_free(SPI_DEV_HOST_ID);
}


/*
Arduino IDF 3.0.4
v5.1.4
*/
bool LilyGoDispSPI::init(int sck, int miso, int mosi, int cs, int rst, int dc, int backlight, uint32_t freq_Mhz)
{
    spi_bus_config_t spi_config = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = 240 * 80 * sizeof(uint16_t),
        .flags = 0,
        .intr_flags = 0
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_DEV_HOST_ID, &spi_config, SPI_DMA_CH_AUTO));

    log_d( "Install panel IO");
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = cs,
        .dc_gpio_num = dc,
        .spi_mode = 0,
        .pclk_hz = (freq_Mhz * 1000U * 1000U),
        .trans_queue_depth = 2,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_DEV_HOST_ID, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = rst,
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
#else
        .color_space = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
#endif
        .bits_per_pixel = 16,
    };

#if defined(ARDUINO_T_WATCH_S3)
    log_d( "Install ST7789 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
#elif defined(ARDUINO_T_LORA_PAGER)
    log_d( "Install ST7796 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io_handle, &panel_config, &panel_handle));
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));


#if defined(ARDUINO_T_WATCH_S3)

#if 0
    const disp_cmd_t vendor_cmd[] = {
        {0x11, {0}, 0x80},
        {0x13, {0}, 0x80},
        {0x36, {0x08}, 0x01},
        {0xB6, {0x0A, 0x82}, 0x02},
        {0xB0, {0x00, 0xE0}, 0x02},
        {0x3A, {0x55}, 0x01},
        {0x80, {0}, 0x10},
        {0xB2, {0x0c, 0x0c, 0x00, 0x33, 0x33}, 0x05},
        {0xB7, {0x35}, 0x01},
        {0xBB, {0x28}, 0x01},
        {0xC0, {0x0C}, 0x01},
        {0xC2, {0x01, 0xFF}, 0x02},
        {0xC3, {0x10}, 0x01},
        {0xC4, {0x20}, 0x01},
        {0xC6, {0x0f}, 0x01},
        {0xD0, {0xa4, 0xa1}, 0x02},
        {0xE0, {0xd0, 0x00, 0x02, 0x07, 0x0a, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0e, 0x12, 0x14, 0x17}, 0x0e},
        {0xE1, {0xd0, 0x00, 0x02, 0x07, 0x0a, 0x28, 0x31, 0x54, 0x47, 0x0e, 0x1c, 0x17, 0x1b, 0x1e}, 0x0e},
        {0x21, {0}, 0x00},
        {0x2A, {0x00, 0x00, 0x00, 0xEF}, 0x04},
        {0x2B, {0x00, 0x00, 0x01, 0x3F}, 0x84},
        {0x29, {0}, 0x80},
        {0xFF, {0xFF}, 0xFF}
    };
#else
    const disp_cmd_t vendor_cmd[] = {
        {0x11, {0}, 0x80},
        {0xB2, {0x1F, 0x1F, 0x00, 0x33, 0x33}, 0x05},
        {0x35, {0x00}, 0x01},
        {0x36, {0x00}, 0x01},
        {0x3A, {0x05}, 0x01},
        {0xB7, {0x00}, 0x01},
        {0xBB, {0x36}, 0x01},
        {0xC0, {0x2C}, 0x01},
        {0xC2, {0x01}, 0x01},
        {0xC3, {0x13}, 0x01},
        {0xC4, {0x20}, 0x01},
        {0xC6, {0x13}, 0x01},
        {0xD6, {0xA1}, 0x01},
        {0xD0, {0xA4, 0xA1}, 0x02},
        {0xD6, {0xA1}, 0x01},
        {0xE0, {0xF0, 0x08, 0x0E, 0x09, 0x08, 0x04, 0x2F, 0x33, 0x45, 0x36, 0x13, 0x12, 0x2A, 0x2D}, 14},
        {0xE1, {0xF0, 0x0E, 0x12, 0x0C, 0x0A, 0x15, 0x2E, 0x32, 0x44, 0x39, 0x17, 0x18, 0x2B, 0x2F}, 14},
        {0xE4, {0x1D, 0x00, 0x00}, 0x03},
        {0xFF, {0xFF}, 0xFF}
    };
#endif
    // vendor specific initialization, it can be different between manufacturers
    // should consult the LCD supplier for initialization sequence code
    int cmd = 0;
    while (vendor_cmd[cmd].len != 0xff) {
        esp_lcd_panel_io_tx_param(io_handle, vendor_cmd[cmd].addr, vendor_cmd[cmd].param, vendor_cmd[cmd].len & 0x7F);
        if (vendor_cmd[cmd].len & 0x80) {
            delay(120);
        }
        cmd++;
    }
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
#if defined(ARDUINO_T_WATCH_S3)
    esp_lcd_panel_set_gap(panel_handle, 0, 0);
    esp_lcd_panel_swap_xy(panel_handle, false);
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
#elif defined(ARDUINO_T_LORA_PAGER)
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    esp_lcd_panel_swap_xy(panel_handle, true);
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
    esp_lcd_panel_set_gap(panel_handle, 0, 49);
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    _backlight = backlight;

    if (_backlight != -1) {
        log_d("Init LEDC : %d", _backlight);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
        ledcAttach(backlight, LEDC_BACKLIGHT_FREQ, LEDC_BACKLIGHT_BIT_WIDTH);
#else
        ledcSetup(LEDC_BACKLIGHT_CHANNEL, LEDC_BACKLIGHT_FREQ, LEDC_BACKLIGHT_BIT_WIDTH);
        ledcAttachPin(backlight, LEDC_BACKLIGHT_CHANNEL);
#endif
    }

    std::vector<uint16_t> draw_buf(_width * _height * 2, 0x000);
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, _width, _height, draw_buf.data());

    return true;
}

void LilyGoDispSPI::setRotation(uint8_t rotation)
{
    _rotation = rotation % 4; // Limit the range of values to 0-3
    assert(panel_handle);
    switch (_rotation) {
    case 0:
        esp_lcd_panel_set_gap(panel_handle, 0, 80);
        esp_lcd_panel_swap_xy(panel_handle, false);
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, true));
        break;
    case 2:
        esp_lcd_panel_set_gap(panel_handle, 0, 0);
        esp_lcd_panel_swap_xy(panel_handle, false);
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
        break;
    case 1:
        esp_lcd_panel_set_gap(panel_handle, 0, 0);
        esp_lcd_panel_swap_xy(panel_handle, true);
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
        break;
    default:
        esp_lcd_panel_set_gap(panel_handle, 80, 0);
        esp_lcd_panel_swap_xy(panel_handle, true);
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, true));
        break;
    }
}

uint8_t LilyGoDispSPI::getRotation()
{
    return _rotation;
}

void LilyGoDispSPI::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color)
{
    assert(panel_handle);
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2, y2, color);
}

void LilyGoDispSPI::sleep()
{
    assert(io_handle);
    esp_lcd_panel_io_tx_param(io_handle, DISP_CMD_SLPIN, NULL, 0);
}

void LilyGoDispSPI::wakeup()
{
    assert(io_handle);
    esp_lcd_panel_io_tx_param(io_handle, DISP_CMD_SLPOUT, NULL, 0);
}

//** Other */
void LilyGoDispSPI::setBrightness(uint8_t level)
{
    if (_backlight == -1) {
        return;
    }
    if (!level) {
        sleep();
    }
    if (!_brightness && level != 0) {
        wakeup();
    }
    _brightness = level;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(_backlight, _brightness);
#else
    ledcWrite(LEDC_BACKLIGHT_CHANNEL, _brightness);
#endif
}

//**  @brief  Arduino SPI
#define TFT_MAD_COLOR_ORDER     DISP_CMD_RGB

bool LilyGoDispArduinoSPI::lock(TickType_t xTicksToWait)
{
    return xSemaphoreTake(_lock, xTicksToWait) == pdTRUE;
}

void LilyGoDispArduinoSPI::unlock()
{
    xSemaphoreGive(_lock);
}

void LilyGoDispArduinoSPI::setBrightness(uint8_t level)
{

}

bool LilyGoDispArduinoSPI::init(int sck,
                                int miso,
                                int mosi,
                                int cs,
                                int rst,
                                int dc,
                                int backlight,
                                uint32_t freq_Mhz,
                                SPIClass &spi)
{

    _lock = xSemaphoreCreateMutex();

    _spi = &spi;

    if (rst != -1) {
        pinMode(rst, OUTPUT);
        digitalWrite(rst, LOW); delay(20);
        digitalWrite(rst, HIGH); delay(120);
    }
    _width = _init_width;
    _height = _init_height;

    _cs = cs;
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);

    _dc = dc;
    pinMode(_dc, OUTPUT);
    digitalWrite(_dc, HIGH);

    _backlight = backlight;
    if (_backlight != -1) {
        pinMode(_backlight, OUTPUT);
        digitalWrite(_backlight, HIGH);
    }
    _spi->begin(sck, miso, mosi);


    const CommandTable_t *t = _init_list;
    for (uint32_t i = 0; i < _init_list_length; i++) {
        writeParams(t[i].cmd, (uint8_t *)t[i].data, t[i].len & 0x1F);
        if (t[i].len & 0x80) {
            delay(120);
        }
    }

    setRotation(0);

    _spi_freq = freq_Mhz * 1000U * 1000U;

    log_v("_init_width:%u _init_height:%u", _init_width, _init_height);
    std::vector<uint16_t> draw_buf(_width * _height * 2, 0x0000);
    pushColors( 0, 0, _width, _height, draw_buf.data());
    xSemaphoreGive(_lock);
    return true;
}

void LilyGoDispArduinoSPI::end()
{
    // Share bus not deinit
}

uint8_t LilyGoDispArduinoSPI::getRotation()
{
    return _rotation;
}

void LilyGoDispArduinoSPI::setRotation(uint8_t rotation)
{
    _rotation = rotation % 4; // Limit the range of values to 0-3
    writeCommand(DISP_CMD_MADCTL);
    switch (_rotation) {
    case 0:
        writeData(DISP_CMD_MAD_MX | DISP_CMD_MAD_MY | DISP_CMD_MAD_MV | TFT_MAD_COLOR_ORDER);
        _width  = _init_height;
        _height = _init_width;
        if (_init_width == 222) {
            _offset_x = 0;
            _offset_y = 49;
        }
        break;
    case 1:
        writeData(DISP_CMD_MAD_MX | TFT_MAD_COLOR_ORDER);
        _width  = _init_width;
        _height = _init_height;
        if (_init_width == 222) {
            _offset_x = 49;
            _offset_y = 0;
        }
        break;
    case 2:
        writeData(DISP_CMD_MAD_MV | TFT_MAD_COLOR_ORDER);
        _width  = _init_height;
        _height = _init_width;
        if (_init_width == 222) {
            _offset_x = 0;
            _offset_y = 49;
        }
        break;
    case 3:
        writeData(DISP_CMD_MAD_MY | TFT_MAD_COLOR_ORDER);
        _width  = _init_width;
        _height = _init_height;
        if (_init_width == 222) {
            _offset_x = 49;
            _offset_y = 0;
        }
        break;
    }
}

void LilyGoDispArduinoSPI::pushColors(uint16_t *data, uint32_t len)
{
    xSemaphoreTake(_lock, portMAX_DELAY);
    digitalWrite(_cs, LOW);
    _spi->beginTransaction(SPISettings(_spi_freq, MSBFIRST, SPI_MODE0));
    digitalWrite(_dc, HIGH);
    _spi->writeBytes((const uint8_t *)data, len * sizeof(uint16_t));
    _spi->endTransaction();
    digitalWrite(_cs, HIGH);
    xSemaphoreGive(_lock);
}

void LilyGoDispArduinoSPI::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color)
{
    setAddrWindow(x1, y1, x1 + x2 - 1, y1 + y2 - 1);
    pushColors(color, x2 * y2);
}

void LilyGoDispArduinoSPI::sleep()
{
    writeCommand(0x10);
}

void LilyGoDispArduinoSPI::wakeup()
{
    writeCommand(0x11);
}


void LilyGoDispArduinoSPI::setAddrWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
    xs += _offset_x;
    ys += _offset_y;
    xe += _offset_x;
    ye += _offset_y;
    CommandTable_t t[3] = {
        {0x2a, {uint8_t(xs >> 8), (uint8_t)xs, (uint8_t)(xe >> 8), (uint8_t) xe}, 0x04},
        {0x2b, {uint8_t(ys >> 8), (uint8_t)ys, (uint8_t)(ye >> 8), (uint8_t) ye}, 0x04},
        {0x2c, {0x00}, 0x00},
    };
    for (uint32_t i = 0; i < 3; i++) {
        writeParams(t[i].cmd, t[i].data, t[i].len);
    }
}

void LilyGoDispArduinoSPI::writeCommand(uint8_t cmd)
{
    xSemaphoreTake(_lock, portMAX_DELAY);
    digitalWrite(_cs, LOW);
    _spi->beginTransaction(SPISettings(_spi_freq, MSBFIRST, SPI_MODE0));
    digitalWrite(_dc, LOW);
    _spi->write(cmd);
    digitalWrite(_dc, HIGH);
    _spi->endTransaction();
    digitalWrite(_cs, HIGH);
    xSemaphoreGive(_lock);
}

void LilyGoDispArduinoSPI::writeData(uint8_t data)
{
    xSemaphoreTake(_lock, portMAX_DELAY);
    digitalWrite(_cs, LOW);
    _spi->beginTransaction(SPISettings(_spi_freq, MSBFIRST, SPI_MODE0));
    digitalWrite(_dc, HIGH);
    _spi->write(data);
    _spi->endTransaction();
    digitalWrite(_cs, HIGH);
    xSemaphoreGive(_lock);
}

void LilyGoDispArduinoSPI::writeParams(uint8_t cmd, uint8_t *data, size_t length)
{
    writeCommand(cmd);
    for (int i = 0; i < length; i++) {
        writeData(data[i]);
    }
}
