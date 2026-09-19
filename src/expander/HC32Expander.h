/**
 * @file      HC32Expander.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @date      2026-06-16
 */
#pragma once

#include <Arduino.h>
#include <Wire.h>

class HC32Expander
{
public:
    /* ─── IRQ Status Bits ─── */
    static constexpr uint8_t IRQ_BUTTON_SINGLE_CLICK  = (1 << 0);
    static constexpr uint8_t IRQ_BUTTON_DOUBLE_CLICK  = (1 << 1);
    static constexpr uint8_t IRQ_BUTTON2_SINGLE_CLICK = (1 << 2);
    static constexpr uint8_t IRQ_BUTTON2_DOUBLE_CLICK = (1 << 3);
    static constexpr uint8_t IRQ_BUTTON2_TRIPLE_CLICK = (1 << 4);
    static constexpr uint8_t IRQ_BUTTON2_LONG_PRESS   = (1 << 5);
    static constexpr uint8_t IRQ_GPIO_CHANGE          = (1 << 6);
    static constexpr uint8_t IRQ_RTC_ALARM            = (1 << 7);

    /* ─── Pin Function Modes ─── */
    enum PinFunction : uint8_t {
        FUNC_GPIO    = 0,
        FUNC_PWM     = 1,
        FUNC_ADC     = 2,
        FUNC_AW9364  = 3,
    };

    /* ─── IRQ Edge Modes (REG_GPIO_IRQ_EDGE values) ─── */
    enum IrqEdge : uint8_t {
        EDGE_HIGH_LEVEL = 0x00,
        EDGE_LOW_LEVEL  = 0x01,
        EDGE_RISING     = 0x02,
        EDGE_FALLING    = 0x03,
    };

    /* ─── Special Commands ─── */
    enum Command : uint8_t {
        CMD_SLEEP_ENABLE     = 0x01,
        CMD_SLEEP_BTN_WAKEUP = 0x02,
        CMD_ENTER_BOOTLOADER = 0x03,
    };

    /**
     * @brief  Construct the driver.
     * @param  sda       SDA pin number
     * @param  scl       SCL pin number
     * @param  irq       IRQ input pin (active-low, external pull-up required)
     * @param  rst       Reset output pin (active-low)
     * @param  addr      I2C slave address (default 0x49)
     * @param  wire      TwoWire instance (default &Wire)
     */
    HC32Expander(int sda, int scl, int irq, int rst,
                 uint8_t addr = 0x49, TwoWire *wire = &Wire);

    /**
     * @brief  Initialize I2C and reset the device.
     * @param  clockHz  I2C clock speed (default 400kHz)
     * @return true on success
     */
    bool begin(uint32_t clockHz = 400000);

    /**
     * @brief  Hard-reset the HC32L130 via RST pin.
     * @param  delayMs  Reset pulse width in ms (default 10)
     */
    void reset(uint16_t delayMs = 10);

    /* ═══════════════════════════════════════════════════════════════════════
     *  System Registers
     * ═══════════════════════════════════════════════════════════════════════ */

    /** @brief  Read chip ID (should return 0x39). */
    uint8_t readChipId();

    /**
     * @brief  Read firmware version.
     * @param[out] major
     * @param[out] minor
     * @param[out] patch
     */
    void readVersion(uint8_t &major, uint8_t &minor, uint8_t &patch);

    /**
     * @brief  Read and clear IRQ status register.
     * @return Bitmask of active IRQ flags.
     */
    uint8_t readIrqStatus();

    /* ═══════════════════════════════════════════════════════════════════════
     *  GPIO Direction (0=output, 1=input)
     * ═══════════════════════════════════════════════════════════════════════ */

    /**
     * @brief  Set a single pin direction.
     * @param  pin      Pin index (0-25)
     * @param  input    true=input, false=output
     */
    void pinMode(uint8_t pin, bool input);

    /**
     * @brief  Set direction for all 24 pins at once.
     * @param  mask     Bitmask: 0=output, 1=input (bits [23:0])
     */
    void setDirectionMask(uint32_t mask);

    /**
     * @brief  Read current direction mask.
     * @return 24-bit direction bitmask
     */
    uint32_t getDirectionMask();

    /* ═══════════════════════════════════════════════════════════════════════
     *  GPIO Output
     * ═══════════════════════════════════════════════════════════════════════ */

    /**
     * @brief  Set a single pin output level.
     * @param  pin    Pin index (0-25)
     * @param  high   true=HIGH, false=LOW
     */
    void digitalWrite(uint8_t pin, bool high);

    /**
     * @brief  Set output levels for all 24 pins at once.
     * @param  mask   Bitmask: bit=1 → HIGH, bit=0 → LOW
     */
    void setOutputMask(uint32_t mask);

    /**
     * @brief  Read current output register.
     * @return 24-bit output bitmask
     */
    uint32_t getOutputMask();

    /* ═══════════════════════════════════════════════════════════════════════
     *  GPIO Input
     * ═══════════════════════════════════════════════════════════════════════ */

    /**
     * @brief  Read a single pin input level.
     * @param  pin    Pin index (0-25)
     * @return true if HIGH, false if LOW
     */
    bool digitalRead(uint8_t pin);

    /**
     * @brief  Read input levels for all 24 pins.
     * @return 24-bit input bitmask (bit=1 → HIGH)
     */
    uint32_t readInputMask();

    /* ═══════════════════════════════════════════════════════════════════════
     *  Pull-up / Pull-down
     * ═══════════════════════════════════════════════════════════════════════ */

    void setPullUp(uint8_t pin, bool enable);
    void setPullDown(uint8_t pin, bool enable);
    void setPullUpMask(uint32_t mask);
    void setPullDownMask(uint32_t mask);

    /* ═══════════════════════════════════════════════════════════════════════
     *  GPIO Interrupt
     * ═══════════════════════════════════════════════════════════════════════ */

    /**
     * @brief  Enable/disable GPIO change interrupt on a pin.
     * @param  pin      Pin index (0-25)
     * @param  enable   true=enable, false=disable
     */
    void setIrqEnable(uint8_t pin, bool enable);

    /**
     * @brief  Set IRQ enable mask for all 24 pins.
     * @param  mask     Bitmask: bit=1 → interrupt enabled
     */
    void setIrqEnableMask(uint32_t mask);

    /**
     * @brief  Set interrupt trigger edge.
     * @param  edge     EDGE_BOTH, EDGE_RISING, or EDGE_FALLING
     */
    void setIrqEdge(IrqEdge edge);

    /**
     * @brief  Read GPIO change mask (which pins changed since last read).
     *         Reading clears the mask.
     * @return 24-bit change bitmask
     */
    uint32_t readGpioChangeMask();

    /**
     * @brief  Read GPIO level at the time of interrupt.
     * @return 24-bit level bitmask
     */
    uint32_t readGpioLevelMask();

    /**
     * @brief  Check if IRQ pin is LOW (interrupt pending).
     * @return true if IRQ is active
     */
    bool isIrqPending();

    /* ═══════════════════════════════════════════════════════════════════════
     *  Pin Function (PWM / ADC / AW9364)
     * ═══════════════════════════════════════════════════════════════════════ */

    /**
     * @brief  Set pin function mode.
     * @param  pin   Pin index (0-25)
     * @param  func  FUNC_GPIO, FUNC_PWM, FUNC_ADC, FUNC_AW9364
     */
    void setPinFunction(uint8_t pin, PinFunction func);

    /**
     * @brief  Read current pin function.
     * @param  pin   Pin index (0-25)
     * @return PinFunction value
     */
    PinFunction getPinFunction(uint8_t pin);

    /* ═══════════════════════════════════════════════════════════════════════
     *  PWM Control
     * ═══════════════════════════════════════════════════════════════════════ */

    /**
     * @brief  Set PWM duty cycle for a pin (pin must be set to FUNC_PWM first).
     * @param  pin   Pin index (0-25)
     * @param  duty  Duty cycle (0=off, 255=full brightness)
     */
    void analogWrite(uint8_t pin, uint8_t duty);

    /* ═══════════════════════════════════════════════════════════════════════
     *  AW9364 Backlight Control
     * ═══════════════════════════════════════════════════════════════════════ */

    /**
     * @brief  Set AW9364 backlight brightness for a pin.
     *         Pin must be set to FUNC_AW9364 first.
     * @param  pin    Pin index (0-25)
     * @param  level  Brightness level (0=off, 16=max)
     */
    void setAw9364Level(uint8_t pin, uint8_t level);

    /* ═══════════════════════════════════════════════════════════════════════
     *  GPIO Sleep Hold
     * ═══════════════════════════════════════════════════════════════════════ */

    /**
     * @brief  Enable/disable sleep hold for a pin (pin state preserved during deep sleep).
     * @param  pin     Pin index (0-25)
     * @param  enable  true to hold state during sleep
     */
    void setSleepHold(uint8_t pin, bool enable);

    /**
     * @brief  Set sleep hold mask for all pins at once.
     * @param  mask  24-bit bitmask (bit=1 → hold during sleep)
     */
    void setSleepHoldMask(uint32_t mask);

    /**
     * @brief  Read current sleep hold mask.
     * @return 24-bit bitmask
     */
    uint32_t getSleepHoldMask();

    /* ═══════════════════════════════════════════════════════════════════════
     *  Special Commands
     * ═══════════════════════════════════════════════════════════════════════ */

    /** @brief  Enter deep sleep (wake via IRQ pin only). */
    void sleep(bool wakeup_by_button = false);


    /** @brief  Enter bootloader mode and reset. */
    void enterBootloader();

    /* ═══════════════════════════════════════════════════════════════════════
     *  Low-level Register Access
     * ═══════════════════════════════════════════════════════════════════════ */

    /** @brief  Write a single register. */
    bool writeReg(uint8_t reg, uint8_t value);

    /** @brief  Read a single register. */
    uint8_t readReg(uint8_t reg);

    /** @brief  Write multiple consecutive registers. */
    bool writeRegs(uint8_t startReg, const uint8_t *data, size_t len);

    /** @brief  Read multiple consecutive registers. */
    bool readRegs(uint8_t startReg, uint8_t *data, size_t len);

private:
    int      _sda;
    int      _scl;
    int      _irq;
    int      _rst;
    uint8_t  _addr;
    TwoWire *_wire;

    /* ─── Register Addresses (from config.h) ─── */
    static constexpr uint8_t REG_CHIP_ID        = 0x00;
    static constexpr uint8_t REG_FW_VERSION     = 0x01;
    static constexpr uint8_t REG_IRQ_STATUS     = 0x04;
    static constexpr uint8_t REG_IRQ_ENABLE     = 0x05;
    static constexpr uint8_t REG_GPIO_CHANGE_L  = 0x06;
    static constexpr uint8_t REG_GPIO_CHANGE_M  = 0x07;
    static constexpr uint8_t REG_GPIO_CHANGE_H  = 0x08;
    static constexpr uint8_t REG_GPIO_LEVEL_L   = 0x09;
    static constexpr uint8_t REG_GPIO_LEVEL_M   = 0x0A;
    static constexpr uint8_t REG_GPIO_LEVEL_H   = 0x0B;
    static constexpr uint8_t REG_CMD            = 0x0F;
    static constexpr uint8_t REG_GPIO_DIR_L     = 0x10;
    static constexpr uint8_t REG_GPIO_DIR_M     = 0x11;
    static constexpr uint8_t REG_GPIO_DIR_H     = 0x12;
    static constexpr uint8_t REG_GPIO_OUT_L     = 0x13;
    static constexpr uint8_t REG_GPIO_OUT_M     = 0x14;
    static constexpr uint8_t REG_GPIO_OUT_H     = 0x15;
    static constexpr uint8_t REG_GPIO_IN_L      = 0x16;
    static constexpr uint8_t REG_GPIO_IN_M      = 0x17;
    static constexpr uint8_t REG_GPIO_IN_H      = 0x18;
    static constexpr uint8_t REG_GPIO_PU_L      = 0x19;
    static constexpr uint8_t REG_GPIO_PU_M      = 0x1A;
    static constexpr uint8_t REG_GPIO_PU_H      = 0x1B;
    static constexpr uint8_t REG_GPIO_PD_L      = 0x1C;
    static constexpr uint8_t REG_GPIO_PD_M      = 0x1D;
    static constexpr uint8_t REG_GPIO_PD_H      = 0x1E;
    static constexpr uint8_t REG_GPIO_IRQ_EN_L  = 0x1F;
    static constexpr uint8_t REG_GPIO_IRQ_EN_M  = 0x20;
    static constexpr uint8_t REG_GPIO_IRQ_EN_H  = 0x21;
    static constexpr uint8_t REG_GPIO_IRQ_EDGE  = 0x22;
    static constexpr uint8_t REG_GPIO_FUNC_BASE = 0x30;
    static constexpr uint8_t REG_PWM_DUTY       = 0x50;
    static constexpr uint8_t REG_PWM_PIN        = 0x51;
    static constexpr uint8_t REG_GPIO_SLEEP_L   = 0x52;
    static constexpr uint8_t REG_GPIO_SLEEP_M   = 0x53;
    static constexpr uint8_t REG_GPIO_SLEEP_H   = 0x54;
    static constexpr uint8_t REG_AW9364_LEVEL   = 0x55;
    static constexpr uint8_t REG_AW9364_PIN     = 0x56;

    void setMaskRegs(uint8_t regL, uint8_t regM, uint8_t regH, uint32_t mask);
    uint32_t getMaskRegs(uint8_t regL, uint8_t regM, uint8_t regH);
};
