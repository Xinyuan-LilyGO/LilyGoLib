/* Rotary encoder handler for arduino.
 *
 * Copyright 2011 Ben Buxton. Licenced under the GNU GPL Version 3.
 * Contact: bb@cactii.net
 *
 */

#include "Arduino.h"
#include "Rotary.h"

/*
 * The below state table has, for each state (row), the new state
 * to set based on the next encoder output. From left to right in,
 * the table, the encoder outputs are 00, 01, 10, 11, and the value
 * in that position is the new state to set.
 */

#define R_START 0x0

// Use the half-step state table (emits a code at 00 and 11)
#define H_R_CCW_BEGIN 0x1
#define H_R_CW_BEGIN 0x2
#define H_R_START_M 0x3
#define H_R_CW_BEGIN_M 0x4
#define H_R_CCW_BEGIN_M 0x5
const unsigned char halfStepTable[6][4] = {
    // R_START (00)
    {H_R_START_M,            H_R_CW_BEGIN,     H_R_CCW_BEGIN,  R_START},
    // R_CCW_BEGIN
    {H_R_START_M | DIR_CCW, R_START,          H_R_CCW_BEGIN,  R_START},
    // R_CW_BEGIN
    {H_R_START_M | DIR_CW,  H_R_CW_BEGIN,     R_START,        R_START},
    // R_START_M (11)
    {H_R_START_M,            H_R_CCW_BEGIN_M,  H_R_CW_BEGIN_M, R_START},
    // R_CW_BEGIN_M
    {H_R_START_M,            H_R_START_M,      H_R_CW_BEGIN_M, R_START | DIR_CW},
    // R_CCW_BEGIN_M
    {H_R_START_M,            H_R_CCW_BEGIN_M,  H_R_START_M,    R_START | DIR_CCW},
};

// Use the full-step state table (emits a code at 00 only)
#define F_R_CW_FINAL 0x1
#define F_R_CW_BEGIN 0x2
#define F_R_CW_NEXT 0x3
#define F_R_CCW_BEGIN 0x4
#define F_R_CCW_FINAL 0x5
#define F_R_CCW_NEXT 0x6

const unsigned char fullStepTable[7][4] = {
    // R_START
    {R_START,      F_R_CW_BEGIN,  F_R_CCW_BEGIN, R_START},
    // R_CW_FINAL
    {F_R_CW_NEXT, R_START,       F_R_CW_FINAL,   R_START | DIR_CW},
    // R_CW_BEGIN
    {F_R_CW_NEXT, F_R_CW_BEGIN,  R_START,         R_START},
    // R_CW_NEXT
    {F_R_CW_NEXT, F_R_CW_BEGIN,  F_R_CW_FINAL,    R_START},
    // R_CCW_BEGIN
    {F_R_CCW_NEXT, R_START,      F_R_CCW_BEGIN,   R_START},
    // R_CCW_FINAL
    {F_R_CCW_NEXT, F_R_CCW_FINAL, R_START,         R_START | DIR_CCW},
    // R_CCW_NEXT
    {F_R_CCW_NEXT, F_R_CCW_FINAL, F_R_CCW_BEGIN,   R_START},
};

/*
 * Constructor. Each arg is the pin number for each encoder contact.
 */
Rotary::Rotary(char _pin1, char _pin2)
{
    // Assign variables.
    pin1 = _pin1;
    pin2 = _pin2;
    // Initialise state.
    state = R_START;
    // Don't invert read pin state by default
    inverter = 0;
    halfStepEnabled = false;
}

void Rotary::begin(bool internalPullup, bool flipLogicForPulldown, bool halfStep)
{
    if (internalPullup) {
        // Enable weak pullups
        pinMode(pin1, INPUT_PULLUP);
        pinMode(pin2, INPUT_PULLUP);
    } else {
        // Set pins to input.
        pinMode(pin1, INPUT);
        pinMode(pin2, INPUT);
    }
    inverter = flipLogicForPulldown ? 1 : 0;
    halfStepEnabled = halfStep;
    state = R_START;
}

unsigned char Rotary::process()
{
    // Grab state of input pins.
    unsigned char pinstate = ((inverter ^ digitalRead(pin2)) << 1) |
                             (inverter ^ digitalRead(pin1));
    // Determine new state from the pins and state table.
    state = halfStepEnabled ? halfStepTable[state & 0xf][pinstate]
                            : fullStepTable[state & 0xf][pinstate];
    // Return emit bits, ie the generated event.
    return state & 0x30;
}
