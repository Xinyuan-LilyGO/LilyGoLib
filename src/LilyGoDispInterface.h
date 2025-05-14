/**
 * @file      LilyGoDispInterface.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-12
 *
 */

#pragma once

#include <stdint.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/spi_master.h"
#include <SPI.h>

#ifndef LEDC_BACKLIGHT_CHANNEL
#define LEDC_BACKLIGHT_CHANNEL      3
#endif

#ifndef LEDC_BACKLIGHT_BIT_WIDTH
#define LEDC_BACKLIGHT_BIT_WIDTH    8
#endif

#ifndef LEDC_BACKLIGHT_FREQ
#define LEDC_BACKLIGHT_FREQ         1000 //HZ
#endif

#ifndef CONFIG_QSPI_MAX_FREQ
#define CONFIG_QSPI_MAX_FREQ        45  //MHZ
#endif

#ifndef CONFIG_SPI_MAX_FREQ
#define CONFIG_SPI_MAX_FREQ         20  //MHZ
#endif

#ifndef CONFIG_ARDUINO_SPI_MAX_FREQ
#define CONFIG_ARDUINO_SPI_MAX_FREQ         80  //MHZ
#endif

enum DriverBusType {
    SPI_DRIVER,
    QSPI_DRIVER,
};

typedef struct {
    uint32_t addr;
    uint8_t param[20];
    uint32_t len;
} disp_cmd_t;

typedef enum RotaryDir {
    ROTARY_DIR_NONE,
    ROTARY_DIR_UP,
    ROTARY_DIR_DOWN,
} RotaryDir_t;

typedef enum KeyboardState {
    KEYBOARD_RELEASED,
    KEYBOARD_PRESSED,
} KeyboardState_t;

typedef struct RotaryMsg {
    RotaryDir_t dir;
    bool centerBtnPressed;
} RotaryMsg_t;

class LilyGo_Display
{
public:
    // *INDENT-OFF*
    LilyGo_Display(DriverBusType type, bool full_refresh) :
        _offset_x(0), _offset_y(0), _rotation(0), _interface(type), _full_refresh(full_refresh){};
    virtual void setRotation(uint8_t rotation) = 0;
    virtual uint8_t getRotation() = 0;
    virtual void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color) = 0;
    virtual uint16_t width() = 0;
    virtual uint16_t height() = 0;

    virtual RotaryMsg_t getRotary(){RotaryMsg_t msg;return msg;}
    virtual uint8_t getPoint(int16_t *x, int16_t *y, uint8_t get_point){return 0;};
    virtual int getKeyChar(char *c){return -1;}
    virtual bool hasTouch() {return false;}
    virtual bool hasEncoder() { return false; }
    virtual bool hasKeyboard() { return false; }
    virtual void feedback(void* args = NULL) {}
    bool needFullRefresh(){return _full_refresh;}
    virtual bool useDMA(){return false;}
    // *INDENT-ON*
protected:
    uint16_t _offset_x ;
    uint16_t _offset_y ;
    uint8_t _rotation;
    DriverBusType _interface;
    bool _full_refresh;
    bool _useDMA;
};

class LilyGoDispQSPI
{
public:
    LilyGoDispQSPI(const  disp_cmd_t *init_list, uint16_t init_len, uint16_t width, uint16_t height) :
        _spi_dev(NULL), _cs(-1), _disp_init_cmd(init_list), _disp_init_cmd_len(init_len), width(width), height(height),
        _offset_x(0), _offset_y(0), _init_width(width), _init_height(height)
    {
    };

    ~LilyGoDispQSPI() {};

    void enableDMA(bool enable){ _use_dma_transaction = enable;}
    void enableTearingEffect(bool enable){_use_tearing_effect = enable;}

    bool init(int rst, int cs, int te, int sck, int d0, int d1, int d2, int d3,  uint32_t freq_Mhz = CONFIG_QSPI_MAX_FREQ);
    void end();
    void setGapOffset(uint16_t gap_x, uint16_t gap_y);

    void setRotation(uint8_t rotation);
    uint8_t getRotation();

    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color);

    void sleep();
    void wakeup();
    void setBrightness(uint8_t level);

    uint16_t width, height;
    uint8_t _brightness;
private:
    void writeCommand(uint32_t cmd, uint8_t *pdat, uint32_t length);
    void setAddrWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);
    void pushColorsNoDMA(uint16_t *data, uint32_t len);
    void pushColorsDMA(uint16_t *data, uint32_t len);

    spi_device_handle_t _spi_dev;
    int _cs ;
    const disp_cmd_t *_disp_init_cmd;
    uint16_t _disp_init_cmd_len;
    uint16_t _offset_x = 0;
    uint16_t _offset_y = 0;
    uint16_t _init_width, _init_height;
    bool _use_dma_transaction;
    bool _use_tearing_effect;
};

class LilyGoDispSPI
{
private:
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    int _backlight;
    uint8_t _rotation = 0;
public:
    uint16_t _width, _height;
    uint8_t _brightness;
    LilyGoDispSPI( uint16_t width, uint16_t height) :  _width(width), _height(height) {};
    ~LilyGoDispSPI() {};
    bool init(int sck, int miso, int mosi, int cs, int rst, int dc, int backlight, uint32_t freq_Mhz = CONFIG_SPI_MAX_FREQ);
    void end();
    void setRotation(uint8_t rotation);
    uint8_t getRotation();
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color);
    void sleep();
    void wakeup();
    void setBrightness(uint8_t level);
};

typedef struct {
    uint8_t cmd;
    uint8_t data[15];
    uint8_t len;
} CommandTable_t;

class LilyGoDispArduinoSPI
{
private:
    SPIClass *_spi = NULL;
    int _cs = -1, _dc = -1;
    int _backlight = -1;
    uint32_t _spi_freq = 40 * 1000U * 1000U;
    uint16_t _offset_x = 0;
    uint16_t _offset_y = 0;
    uint8_t _rotation = 0;

    uint16_t _init_width = 0;
    uint16_t _init_height = 0;
    const CommandTable_t *_init_list;
    size_t _init_list_length;
    xSemaphoreHandle _lock;
public:
    uint16_t _width, _height;
    uint8_t _brightness;
    LilyGoDispArduinoSPI( uint16_t width, uint16_t height, const CommandTable_t *init_list, size_t init_list_length) :
        _init_width(width), _init_height(height), _init_list(init_list), _init_list_length(init_list_length), _lock(NULL) {};
    ~LilyGoDispArduinoSPI() {};
    bool init(int sck, int miso, int mosi, int cs, int rst, int dc, int backlight, uint32_t freq_Mhz = CONFIG_ARDUINO_SPI_MAX_FREQ, SPIClass &spi = SPI);
    void end();
    void setRotation(uint8_t rotation);
    uint8_t getRotation();
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color);
    void pushColors(uint16_t *data, uint32_t len);
    void sleep();
    void wakeup();
    void setBrightness(uint8_t level);
    void writeParams(uint8_t cmd, uint8_t *data = NULL, size_t length = 0);
    void writeData(uint8_t data);
    void writeCommand(uint8_t cmd);
    void setAddrWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);
    bool lock(TickType_t xTicksToWait = portMAX_DELAY);
    void unlock();

};






















