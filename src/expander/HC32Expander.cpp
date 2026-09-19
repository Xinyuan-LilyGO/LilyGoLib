/**
 * @file      HC32Expander.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @date      2026-06-16
 */
#include "HC32Expander.h"

HC32Expander::HC32Expander(int sda, int scl, int irq, int rst,
                           uint8_t addr, TwoWire *wire)
    : _sda(sda), _scl(scl), _irq(irq), _rst(rst), _addr(addr), _wire(wire)
{
}

bool HC32Expander::begin(uint32_t clockHz)
{
    ::pinMode(_irq, INPUT);   // external pull-up
    ::pinMode(_rst, OUTPUT);
    ::digitalWrite(_rst, HIGH);
    delay(30);
    ::digitalWrite(_rst, LOW);
    delay(60);
    ::digitalWrite(_rst, HIGH);
    delay(80);

    _wire->begin(_sda, _scl, clockHz);

    // Check if device responds
    _wire->beginTransmission(_addr);
    if (_wire->endTransmission() != 0) {
        return false;
    }

    // Verify chip ID
    uint8_t id = readChipId();
    return (id == 0x39);
}

void HC32Expander::reset(uint16_t delayMs)
{
    ::digitalWrite(_rst, LOW);
    delay(delayMs);
    ::digitalWrite(_rst, HIGH);
    delay(delayMs + 50);  // wait for bootloader to jump to app
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  System Registers
 * ═══════════════════════════════════════════════════════════════════════════ */

uint8_t HC32Expander::readChipId()
{
    return readReg(REG_CHIP_ID);
}

void HC32Expander::readVersion(uint8_t &major, uint8_t &minor, uint8_t &patch)
{
    uint8_t buf[3];
    readRegs(REG_FW_VERSION, buf, 3);
    patch = buf[0];
    minor = buf[1];
    major = buf[2];
}

uint8_t HC32Expander::readIrqStatus()
{
    return readReg(REG_IRQ_STATUS);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  GPIO Direction
 * ═══════════════════════════════════════════════════════════════════════════ */

void HC32Expander::pinMode(uint8_t pin, bool input)
{
    if (pin > 23) return;
    uint32_t mask = getMaskRegs(REG_GPIO_DIR_L, REG_GPIO_DIR_M, REG_GPIO_DIR_H);
    if (input) {
        mask |= (1UL << pin);
    } else {
        mask &= ~(1UL << pin);
    }
    setMaskRegs(REG_GPIO_DIR_L, REG_GPIO_DIR_M, REG_GPIO_DIR_H, mask);
}

void HC32Expander::setDirectionMask(uint32_t mask)
{
    setMaskRegs(REG_GPIO_DIR_L, REG_GPIO_DIR_M, REG_GPIO_DIR_H, mask & 0xFFFFFF);
}

uint32_t HC32Expander::getDirectionMask()
{
    return getMaskRegs(REG_GPIO_DIR_L, REG_GPIO_DIR_M, REG_GPIO_DIR_H);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  GPIO Output
 * ═══════════════════════════════════════════════════════════════════════════ */

void HC32Expander::digitalWrite(uint8_t pin, bool high)
{
    if (pin > 23) return;
    uint32_t mask = getMaskRegs(REG_GPIO_OUT_L, REG_GPIO_OUT_M, REG_GPIO_OUT_H);
    if (high) {
        mask |= (1UL << pin);
    } else {
        mask &= ~(1UL << pin);
    }
    setMaskRegs(REG_GPIO_OUT_L, REG_GPIO_OUT_M, REG_GPIO_OUT_H, mask);
}

void HC32Expander::setOutputMask(uint32_t mask)
{
    setMaskRegs(REG_GPIO_OUT_L, REG_GPIO_OUT_M, REG_GPIO_OUT_H, mask & 0xFFFFFF);
}

uint32_t HC32Expander::getOutputMask()
{
    return getMaskRegs(REG_GPIO_OUT_L, REG_GPIO_OUT_M, REG_GPIO_OUT_H);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  GPIO Input
 * ═══════════════════════════════════════════════════════════════════════════ */

bool HC32Expander::digitalRead(uint8_t pin)
{
    if (pin > 23) return false;
    uint32_t mask = readInputMask();
    return (mask >> pin) & 1;
}

uint32_t HC32Expander::readInputMask()
{
    return getMaskRegs(REG_GPIO_IN_L, REG_GPIO_IN_M, REG_GPIO_IN_H);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Pull-up / Pull-down
 * ═══════════════════════════════════════════════════════════════════════════ */

void HC32Expander::setPullUp(uint8_t pin, bool enable)
{
    if (pin > 23) return;
    uint32_t mask = getMaskRegs(REG_GPIO_PU_L, REG_GPIO_PU_M, REG_GPIO_PU_H);
    if (enable) mask |= (1UL << pin);
    else        mask &= ~(1UL << pin);
    setMaskRegs(REG_GPIO_PU_L, REG_GPIO_PU_M, REG_GPIO_PU_H, mask);
}

void HC32Expander::setPullDown(uint8_t pin, bool enable)
{
    if (pin > 23) return;
    uint32_t mask = getMaskRegs(REG_GPIO_PD_L, REG_GPIO_PD_M, REG_GPIO_PD_H);
    if (enable) mask |= (1UL << pin);
    else        mask &= ~(1UL << pin);
    setMaskRegs(REG_GPIO_PD_L, REG_GPIO_PD_M, REG_GPIO_PD_H, mask);
}

void HC32Expander::setPullUpMask(uint32_t mask)
{
    setMaskRegs(REG_GPIO_PU_L, REG_GPIO_PU_M, REG_GPIO_PU_H, mask & 0xFFFFFF);
}

void HC32Expander::setPullDownMask(uint32_t mask)
{
    setMaskRegs(REG_GPIO_PD_L, REG_GPIO_PD_M, REG_GPIO_PD_H, mask & 0xFFFFFF);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  GPIO Interrupt
 * ═══════════════════════════════════════════════════════════════════════════ */

void HC32Expander::setIrqEnable(uint8_t pin, bool enable)
{
    if (pin > 23) return;
    uint32_t mask = getMaskRegs(REG_GPIO_IRQ_EN_L, REG_GPIO_IRQ_EN_M, REG_GPIO_IRQ_EN_H);
    if (enable) mask |= (1UL << pin);
    else        mask &= ~(1UL << pin);
    setMaskRegs(REG_GPIO_IRQ_EN_L, REG_GPIO_IRQ_EN_M, REG_GPIO_IRQ_EN_H, mask);
}

void HC32Expander::setIrqEnableMask(uint32_t mask)
{
    setMaskRegs(REG_GPIO_IRQ_EN_L, REG_GPIO_IRQ_EN_M, REG_GPIO_IRQ_EN_H, mask & 0xFFFFFF);
}

void HC32Expander::setIrqEdge(IrqEdge edge)
{
    writeReg(REG_GPIO_IRQ_EDGE, (uint8_t)edge);
}

uint32_t HC32Expander::readGpioChangeMask()
{
    return getMaskRegs(REG_GPIO_CHANGE_L, REG_GPIO_CHANGE_M, REG_GPIO_CHANGE_H);
}

uint32_t HC32Expander::readGpioLevelMask()
{
    return getMaskRegs(REG_GPIO_LEVEL_L, REG_GPIO_LEVEL_M, REG_GPIO_LEVEL_H);
}

bool HC32Expander::isIrqPending()
{
    return digitalRead(_irq) == LOW;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Pin Function
 * ═══════════════════════════════════════════════════════════════════════════ */

void HC32Expander::setPinFunction(uint8_t pin, PinFunction func)
{
    if (pin > 25) return;
    writeReg(REG_GPIO_FUNC_BASE + pin, (uint8_t)func);
}

HC32Expander::PinFunction HC32Expander::getPinFunction(uint8_t pin)
{
    if (pin > 25) return FUNC_GPIO;
    return (PinFunction)readReg(REG_GPIO_FUNC_BASE + pin);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  PWM Control
 * ═══════════════════════════════════════════════════════════════════════════ */

void HC32Expander::analogWrite(uint8_t pin, uint8_t duty)
{
    uint8_t buf[2] = { duty, pin };
    writeRegs(REG_PWM_DUTY, buf, 2);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  AW9364 Backlight Control
 * ═══════════════════════════════════════════════════════════════════════════ */

void HC32Expander::setAw9364Level(uint8_t pin, uint8_t level)
{
    uint8_t buf[2] = { level, pin };
    writeRegs(REG_AW9364_LEVEL, buf, 2);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  GPIO Sleep Hold
 * ═══════════════════════════════════════════════════════════════════════════ */

void HC32Expander::setSleepHold(uint8_t pin, bool enable)
{
    if (pin > 23) return;
    uint32_t mask = getSleepHoldMask();
    if (enable) mask |= (1UL << pin);
    else        mask &= ~(1UL << pin);
    setSleepHoldMask(mask);
}

void HC32Expander::setSleepHoldMask(uint32_t mask)
{
    setMaskRegs(REG_GPIO_SLEEP_L, REG_GPIO_SLEEP_M, REG_GPIO_SLEEP_H, mask);
}

uint32_t HC32Expander::getSleepHoldMask()
{
    return getMaskRegs(REG_GPIO_SLEEP_L, REG_GPIO_SLEEP_M, REG_GPIO_SLEEP_H);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Special Commands
 * ═══════════════════════════════════════════════════════════════════════════ */

void HC32Expander::sleep(bool wakeup_by_button)
{
    writeReg(REG_CMD, wakeup_by_button ? CMD_SLEEP_BTN_WAKEUP : CMD_SLEEP_ENABLE);
}

void HC32Expander::enterBootloader()
{
    writeReg(REG_CMD, CMD_ENTER_BOOTLOADER);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Low-level Register Access
 * ═══════════════════════════════════════════════════════════════════════════ */

bool HC32Expander::writeReg(uint8_t reg, uint8_t value)
{
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->write(value);
    return (_wire->endTransmission() == 0);
}

uint8_t HC32Expander::readReg(uint8_t reg)
{
    uint8_t val = 0xFF;
    readRegs(reg, &val, 1);
    return val;
}

bool HC32Expander::writeRegs(uint8_t startReg, const uint8_t *data, size_t len)
{
    _wire->beginTransmission(_addr);
    _wire->write(startReg);
    for (size_t i = 0; i < len; i++) {
        _wire->write(data[i]);
    }
    return (_wire->endTransmission() == 0);
}

bool HC32Expander::readRegs(uint8_t startReg, uint8_t *data, size_t len)
{
    _wire->beginTransmission(_addr);
    _wire->write(startReg);
    if (_wire->endTransmission(false) != 0) return false;

    size_t received = _wire->requestFrom((int)_addr, (int)len, 1);
    for (size_t i = 0; i < received && i < len; i++) {
        data[i] = _wire->read();
    }
    return (received == len);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Internal helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

void HC32Expander::setMaskRegs(uint8_t regL, uint8_t regM, uint8_t regH, uint32_t mask)
{
    uint8_t buf[3] = {
        (uint8_t)(mask & 0xFF),
        (uint8_t)((mask >> 8) & 0xFF),
        (uint8_t)((mask >> 16) & 0xFF)
    };
    writeRegs(regL, buf, 3);
}

uint32_t HC32Expander::getMaskRegs(uint8_t regL, uint8_t regM, uint8_t regH)
{
    uint8_t buf[3] = {0};
    readRegs(regL, buf, 3);
    return ((uint32_t)buf[2] << 16) | ((uint32_t)buf[1] << 8) | (uint32_t)buf[0];
}
