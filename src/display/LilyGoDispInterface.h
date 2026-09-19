/**
 * @file      LilyGoDispInterface.h
 * @brief     Declares shared display bus adapters and display input abstractions.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-12
 *
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <SPI.h>

/**
 * @brief Default LEDC channel used for display backlight PWM.
 */
#ifndef LEDC_BACKLIGHT_CHANNEL
#define LEDC_BACKLIGHT_CHANNEL      3
#endif

/**
 * @brief Default LEDC backlight PWM resolution in bits.
 */
#ifndef LEDC_BACKLIGHT_BIT_WIDTH
#define LEDC_BACKLIGHT_BIT_WIDTH    8
#endif

/**
 * @brief Default LEDC backlight PWM frequency in Hz.
 */
#ifndef LEDC_BACKLIGHT_FREQ
#define LEDC_BACKLIGHT_FREQ         1000 //HZ
#endif

/**
 * @brief Default QSPI display bus clock in MHz.
 */
#ifndef CONFIG_QSPI_MAX_FREQ
#define CONFIG_QSPI_MAX_FREQ        45  //MHZ
#endif

/**
 * @brief Default ESP-IDF SPI display bus clock in MHz.
 */
#ifndef CONFIG_SPI_MAX_FREQ
#define CONFIG_SPI_MAX_FREQ         20  //MHZ
#endif

/**
 * @brief Default Arduino SPI display bus clock in MHz.
 */
#ifndef CONFIG_ARDUINO_SPI_MAX_FREQ
#define CONFIG_ARDUINO_SPI_MAX_FREQ         80  //MHZ
#endif

/**
 * @brief Display bus implementation type.
 */
enum DriverBusType {
    SPI_DRIVER,  /**< SPI display bus. */
    QSPI_DRIVER, /**< QSPI display bus. */
};

/**
 * @brief Rotation-specific MADCTL and geometry configuration.
 */
typedef struct  {
    uint8_t madCmd;     /**< MADCTL command value for this rotation. */
    uint16_t width;     /**< Logical width after rotation. */
    uint16_t height;    /**< Logical height after rotation. */
    uint16_t offset_x;  /**< X offset applied to the panel address window. */
    uint16_t offset_y;  /**< Y offset applied to the panel address window. */
} DispRotationConfig_t;

/**
 * @brief Display initialization command descriptor.
 */
typedef struct {
    uint32_t addr;     /**< Command address or opcode. */
    uint8_t param[20]; /**< Command parameters. */
    uint32_t len;      /**< Number of valid bytes in param. */
} disp_cmd_t;

/**
 * @brief Normalized rotary encoder direction.
 */
typedef enum RotaryDir {
    ROTARY_DIR_NONE, /**< No rotary movement. */
    ROTARY_DIR_UP,   /**< Rotary movement in the upward / previous direction. */
    ROTARY_DIR_DOWN, /**< Rotary movement in the downward / next direction. */
} RotaryDir_t;

/**
 * @brief Normalized keyboard key state.
 */
typedef enum KeyboardState {
    KEYBOARD_RELEASED, /**< Key released. */
    KEYBOARD_PRESSED,  /**< Key pressed. */
} KeyboardState_t;

/**
 * @brief Rotary encoder state returned through the display input interface.
 */
typedef struct RotaryMsg {
    RotaryDir_t dir;        /**< Current rotary direction. */
    bool centerBtnPressed;  /**< true while the center button is pressed. */
    int16_t enc_diff;       /**< Signed encoder step delta. */
    bool centerBtnClicked;  /**< true when a center-button click is detected. */
    bool centerBtnReleased; /**< true when the center button is released. */
} RotaryMsg_t;

/**
 * @brief Base display abstraction used by board classes.
 */
class LilyGo_Display
{
public:
    // *INDENT-OFF*
    /**
     * @brief Construct a display abstraction.
     * @param type Display bus type.
     * @param full_refresh true when the panel requires full-frame refreshes.
     */
    LilyGo_Display(DriverBusType type, bool full_refresh) :
        _offset_x(0), _offset_y(0), _rotation(0), _interface(type), _full_refresh(full_refresh){};

    /**
     * @brief Set the display rotation.
     * @param rotation Rotation index supported by the concrete display.
     */
    virtual void setRotation(uint8_t rotation) = 0;

    /**
     * @brief Get the current display rotation.
     * @return Current rotation index.
     */
    virtual uint8_t getRotation() = 0;

    /**
     * @brief Push a rectangular block of RGB565 pixels to the display.
     * @param x1 Left coordinate.
     * @param y1 Top coordinate.
     * @param x2 Right coordinate.
     * @param y2 Bottom coordinate.
     * @param color RGB565 pixel buffer.
     */
    virtual void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color) = 0;

    /**
     * @brief Get the current logical display width.
     * @return Width in pixels.
     */
    virtual uint16_t width() = 0;

    /**
     * @brief Get the current logical display height.
     * @return Height in pixels.
     */
    virtual uint16_t height() = 0;

    /**
     * @brief Check whether RGB565 byte order must be swapped before drawing.
     * @return true when color bytes must be swapped.
     */
    virtual bool needSwapColors() { return false; }

    /**
     * @brief Read the optional rotary input state.
     * @return Rotary message with no movement by default.
     */
    virtual RotaryMsg_t getRotary(){RotaryMsg_t msg = {};return msg;}

    /**
     * @brief Read optional touch points.
     * @param x Receives touch X coordinates.
     * @param y Receives touch Y coordinates.
     * @param get_point Maximum number of points requested.
     * @return Number of touch points read.
     */
    virtual uint8_t getPoint(int16_t *x, int16_t *y, uint8_t get_point){return 0;};

    /**
     * @brief Read an optional keyboard character.
     * @param c Receives the key character.
     * @return Keyboard state or a negative value when no key is available.
     */
    virtual int getKeyChar(char *c){return -1;}

    /**
     * @brief Check whether the display object exposes touch input.
     * @return true when touch input is available.
     */
    virtual bool hasTouch() {return false;}

    /**
     * @brief Check whether the display object exposes rotary input.
     * @return true when rotary input is available.
     */
    virtual bool hasEncoder() { return false; }

    /**
     * @brief Check whether the display object exposes keyboard input.
     * @return true when keyboard input is available.
     */
    virtual bool hasKeyboard() { return false; }

    /**
     * @brief Trigger optional user feedback such as haptics.
     * @param args Optional implementation-specific argument pointer.
     */
    virtual void feedback(void* args = NULL) {}

    /**
     * @brief Check whether the panel requires full refresh.
     * @return true when partial refresh should not be used.
     */
    bool needFullRefresh(){return _full_refresh;}

    /**
     * @brief Check whether the display implementation uses DMA transfers.
     * @return true when DMA drawing is enabled.
     */
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

/**
 * @brief QSPI display adapter using the ESP-IDF SPI master driver.
 */
class LilyGoDispQSPI
{
public:
    /**
     * @brief Construct a QSPI display adapter.
     * @param init_list Panel initialization command table.
     * @param init_len Number of initialization commands.
     * @param width Native panel width.
     * @param height Native panel height.
     */
    LilyGoDispQSPI(const  disp_cmd_t *init_list, uint16_t init_len, uint16_t width, uint16_t height) :
        width(width), height(height), _brightness(0), _spi_dev(NULL), _lock(NULL), _cs(-1),
        _disp_init_cmd(init_list), _disp_init_cmd_len(init_len), _offset_x(0), _offset_y(0),
        _init_width(width), _init_height(height), _te_pin(-1), _use_dma_transaction(false),
        _use_tearing_effect(false), _tx_dma_buf(NULL), _tx_dma_buf_pixels(0)
    {
    };

    /**
     * @brief Destroy the QSPI display adapter.
     */
    ~LilyGoDispQSPI() {};

    /**
     * @brief Enable or disable DMA drawing.
     * @param enable true to use DMA transactions.
     */
    void enableDMA(bool enable)
    {
        _use_dma_transaction = enable;
    }

    /**
     * @brief Enable or disable tearing-effect synchronization.
     * @param enable true to wait for the panel tearing-effect signal.
     */
    void enableTearingEffect(bool enable)
    {
        _use_tearing_effect = enable;
    }

    /**
     * @brief Initialize the QSPI panel bus and panel state.
     * @param rst Reset pin, or -1 if unused.
     * @param cs Chip-select pin.
     * @param te Tearing-effect pin, or -1 if unused.
     * @param sck SPI clock pin.
     * @param d0 QSPI data 0 pin.
     * @param d1 QSPI data 1 pin.
     * @param d2 QSPI data 2 pin.
     * @param d3 QSPI data 3 pin.
     * @param freq_Mhz Bus clock in MHz.
     * @return true if the display was initialized.
     */
    bool init(int rst, int cs, int te, int sck, int d0, int d1, int d2, int d3,  uint32_t freq_Mhz = CONFIG_QSPI_MAX_FREQ);

    /**
     * @brief Release the QSPI display resources.
     */
    void end();

    /**
     * @brief Set panel address-window offsets.
     * @param gap_x X offset in pixels.
     * @param gap_y Y offset in pixels.
     */
    void setGapOffset(uint16_t gap_x, uint16_t gap_y);

    /**
     * @brief Set display rotation.
     * @param rotation Rotation index.
     */
    void setRotation(uint8_t rotation);

    /**
     * @brief Get current display rotation.
     * @return Rotation index.
     */
    uint8_t getRotation();

    /**
     * @brief Push a rectangular RGB565 pixel block.
     * @param x1 Left coordinate.
     * @param y1 Top coordinate.
     * @param x2 Right coordinate.
     * @param y2 Bottom coordinate.
     * @param color RGB565 pixel buffer.
     */
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color);

    /**
     * @brief Put the panel into sleep mode.
     */
    void sleep();

    /**
     * @brief Wake the panel from sleep mode.
     */
    void wakeup();

    /**
     * @brief Set display backlight brightness.
     * @param level Brightness level in the implementation range.
     */
    void setBrightness(uint8_t level);

    /** Current logical display width. */
    uint16_t width, height;
    /** Current backlight brightness level. */
    uint8_t _brightness;
private:
    bool lock(TickType_t xTicksToWait = portMAX_DELAY);
    void unlock();
    void writeCommand(uint32_t cmd, uint8_t *pdat, uint32_t length);
    void setAddrWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);
    void pushColorsNoDMA(uint16_t *data, uint32_t len);
    void pushColorsDMA(uint16_t *data, uint32_t len);

    spi_device_handle_t _spi_dev;
    SemaphoreHandle_t _lock;
    int _cs ;
    const disp_cmd_t *_disp_init_cmd;
    uint16_t _disp_init_cmd_len;
    uint16_t _offset_x = 0;
    uint16_t _offset_y = 0;
    uint16_t _init_width, _init_height;
    int _te_pin = -1;
    bool _use_dma_transaction;
    bool _use_tearing_effect;
    uint16_t *_tx_dma_buf;
    size_t _tx_dma_buf_pixels;
};

/**
 * @brief SPI display adapter using ESP-IDF esp_lcd panel APIs.
 */
class LilyGoDispSPI
{
private:
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    int _backlight;
    uint8_t _rotation = 0;
public:
    /** Current logical display size. */
    uint16_t _width, _height;
    /** Current backlight brightness level. */
    uint8_t _brightness;

    /**
     * @brief Construct an SPI display adapter.
     * @param width Native panel width.
     * @param height Native panel height.
     */
    LilyGoDispSPI( uint16_t width, uint16_t height) : _backlight(-1), _width(width), _height(height), _brightness(0) {};

    /**
     * @brief Destroy the SPI display adapter.
     */
    ~LilyGoDispSPI() {};

    /**
     * @brief Initialize the SPI panel bus and panel state.
     * @param sck SPI clock pin.
     * @param miso SPI MISO pin, or -1 if unused.
     * @param mosi SPI MOSI pin.
     * @param cs Chip-select pin.
     * @param rst Reset pin, or -1 if unused.
     * @param dc Data/command pin.
     * @param backlight Backlight control pin, or -1 if unused.
     * @param freq_Mhz Bus clock in MHz.
     * @return true if the display was initialized.
     */
    bool init(int sck, int miso, int mosi, int cs, int rst, int dc, int backlight, uint32_t freq_Mhz = CONFIG_SPI_MAX_FREQ);

    /** @brief Release display resources. */
    void end();

    /**
     * @brief Set display rotation.
     * @param rotation Rotation index.
     */
    void setRotation(uint8_t rotation);

    /**
     * @brief Get current display rotation.
     * @return Rotation index.
     */
    uint8_t getRotation();

    /**
     * @brief Push a rectangular RGB565 pixel block.
     * @param x1 Left coordinate.
     * @param y1 Top coordinate.
     * @param x2 Right coordinate.
     * @param y2 Bottom coordinate.
     * @param color RGB565 pixel buffer.
     */
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color);

    /** @brief Put the panel into sleep mode. */
    void sleep();

    /** @brief Wake the panel from sleep mode. */
    void wakeup();

    /**
     * @brief Set display backlight brightness.
     * @param level Brightness level in the implementation range.
     */
    void setBrightness(uint8_t level);
};

/**
 * @brief 8-bit command descriptor used by Arduino SPI display drivers.
 */
typedef struct {
    uint8_t cmd;      /**< Command byte. */
    uint8_t data[15]; /**< Command parameter bytes. */
    uint8_t len;      /**< Number of valid bytes in data. */
} CommandTable_t;

/**
 * @brief SPI display adapter using Arduino SPIClass.
 */
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
    const  DispRotationConfig_t *_rotation_configs;

public:
    /** Current logical display size. */
    uint16_t _width, _height;
    /** Current backlight brightness level. */
    uint8_t _brightness;

    /**
     * @brief Construct an Arduino SPI display adapter.
     * @param width Native panel width.
     * @param height Native panel height.
     * @param init_list Panel initialization command table.
     * @param init_list_length Number of initialization commands.
     * @param rotation_config Rotation geometry table.
     */
    LilyGoDispArduinoSPI( uint16_t width, uint16_t height, const CommandTable_t *init_list, size_t init_list_length, const DispRotationConfig_t *rotation_config) :
        _init_width(width), _init_height(height), _init_list(init_list), _init_list_length(init_list_length), _lock(NULL),_rotation_configs(rotation_config)
    {
    };

    /**
     * @brief Destroy the Arduino SPI display adapter.
     */
    ~LilyGoDispArduinoSPI() {};

    /**
     * @brief Initialize the Arduino SPI panel bus and panel state.
     * @param sck SPI clock pin.
     * @param miso SPI MISO pin, or -1 if unused.
     * @param mosi SPI MOSI pin.
     * @param cs Chip-select pin.
     * @param rst Reset pin, or -1 if unused.
     * @param dc Data/command pin.
     * @param backlight Backlight control pin, or -1 if unused.
     * @param freq_Mhz Bus clock in MHz.
     * @param spi SPIClass instance used for transfers.
     * @return true if the display was initialized.
     */
    bool init(int sck, int miso, int mosi, int cs, int rst, int dc, int backlight, uint32_t freq_Mhz = CONFIG_ARDUINO_SPI_MAX_FREQ, SPIClass &spi = SPI);

    /** @brief Release display resources. */
    void end();

    /**
     * @brief Set display rotation.
     * @param rotation Rotation index.
     */
    void setRotation(uint8_t rotation);

    /**
     * @brief Get current display rotation.
     * @return Rotation index.
     */
    uint8_t getRotation();

    /**
     * @brief Push a rectangular RGB565 pixel block.
     * @param x1 Left coordinate.
     * @param y1 Top coordinate.
     * @param x2 Right coordinate.
     * @param y2 Bottom coordinate.
     * @param color RGB565 pixel buffer.
     */
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color);

    /**
     * @brief Push a contiguous RGB565 pixel buffer to the current address window.
     * @param data RGB565 pixel buffer.
     * @param len Number of pixels to write.
     */
    void pushColors(uint16_t *data, uint32_t len);

    /** @brief Put the panel into sleep mode. */
    void sleep();

    /** @brief Wake the panel from sleep mode. */
    void wakeup();

    /**
     * @brief Set display backlight brightness.
     * @param level Brightness level in the implementation range.
     */
    void setBrightness(uint8_t level);

    /**
     * @brief Write a command with optional parameters.
     * @param cmd Command byte.
     * @param data Optional parameter buffer.
     * @param length Number of bytes in data.
     */
    void writeParams(uint8_t cmd, uint8_t *data = NULL, size_t length = 0);

    /**
     * @brief Write one data byte.
     * @param data Data byte to send.
     */
    void writeData(uint8_t data);

    /**
     * @brief Write one command byte.
     * @param cmd Command byte to send.
     */
    void writeCommand(uint8_t cmd);

    /**
     * @brief Set the panel address window.
     * @param xs Left coordinate.
     * @param ys Top coordinate.
     * @param xe Right coordinate.
     * @param ye Bottom coordinate.
     */
    void setAddrWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye);

    /**
     * @brief Lock the display SPI bus.
     * @param xTicksToWait Maximum time to wait for the lock.
     * @return true if the lock was acquired.
     */
    bool lock(TickType_t xTicksToWait = portMAX_DELAY);

    /**
     * @brief Unlock the display SPI bus.
     */
    void unlock();

};
