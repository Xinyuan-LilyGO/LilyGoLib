/*
  RadioLib LoRaWAN End Device Reference Example

  This example joins a LoRaWAN network and will send
  uplink packets. Before you start, you will have to
  register your device at https://www.thethingsnetwork.org/
  After your device is registered, you can run this example.
  The device will join the network and start uploading data.

  Also, most of the possible and available functions are
  shown here for reference.

  LoRaWAN v1.0.4/v1.1 requires the use of EEPROM (persistent storage).
  Running this examples REQUIRES you to check "Resets DevNonces"
  on your LoRaWAN dashboard. Refer to the notes or the
  network's documentation on how to do this.
  To comply with LoRaWAN's persistent storage, refer to
  https://github.com/radiolib-org/radiolib-persistence

  For default module settings, see the wiki page
  https://github.com/jgromes/RadioLib/wiki/Default-configuration

  For full API reference, see the GitHub Pages
  https://jgromes.github.io/RadioLib/

  For LoRaWAN details, see the wiki page
  https://github.com/jgromes/RadioLib/wiki/LoRaWAN

*/

#include "config.h"

#include <Preferences.h>

enum LoRaWanState {
    LORAWAN_JOINING,
    LORAWAN_JOINED,
    LORAWAN_UPLINK,
};

LoRaWanState join_state = LORAWAN_JOINING;
Preferences store;
RTC_DATA_ATTR uint8_t LWsession[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
lv_obj_t *label;

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    const char *example_title = "LoRaWAN Example";
    label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_label_set_text(label, example_title);
    lv_obj_center(label);
    lv_timer_handler();

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    Serial.println(F("\nSetup"));

    int state;

    // Override the default join rate
    uint8_t joinDR = 4;

    // Setup the OTAA session information
    node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);

    // ##### setup the flash storage
    store.begin("radiolib");
    // ##### if we have previously saved nonces, restore them and try to restore session as well
    if (store.isKey("nonces")) {
        uint8_t buffer[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];                                       // create somewhere to store nonces
        store.getBytes("nonces", buffer, RADIOLIB_LORAWAN_NONCES_BUF_SIZE); // get them from the store
        state = node.setBufferNonces(buffer);                                                           // send them to LoRaWAN
        debug(state != RADIOLIB_ERR_NONE, F("Restoring nonces buffer failed"), state, false);

        // recall session from RTC deep-sleep preserved variable
        state = node.setBufferSession(LWsession); // send them to LoRaWAN stack

        // if we have booted more than once we should have a session to restore, so report any failure
        // otherwise no point saying there's been a failure when it was bound to fail with an empty LWsession var.
        debug((state != RADIOLIB_ERR_NONE), F("Restoring session buffer failed"), state, false);

        // if Nonces and Session restored successfully, activation is just a formality
        // moreover, Nonces didn't change so no need to re-save them
        if (state == RADIOLIB_ERR_NONE) {
            Serial.println(F("Successfully restored session - now activating"));
            state = node.activateOTAA();
            debug((state != RADIOLIB_LORAWAN_SESSION_RESTORED), F("Failed to activate restored session"), state, true);

            // ##### close the store before returning
            store.end();
        }
    } else {  // store has no key "nonces"
        Serial.println(F("No Nonces saved - starting fresh."));
    }
}

void joinHandle()
{
    // if we got here, there was no session to restore, so start trying to join
    uint32_t sleepForSeconds = 60 * 1000;
    int state = RADIOLIB_ERR_NETWORK_NOT_JOINED;

    while (state != RADIOLIB_LORAWAN_NEW_SESSION) {

        Serial.println(F("Join ('login') to the LoRaWAN Network"));

        state = node.activateOTAA();

        // ##### save the join counters (nonces) to permanent store
        Serial.println(F("Saving nonces to flash"));
        uint8_t buffer[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];           // create somewhere to store nonces
        uint8_t *persist = node.getBufferNonces();                  // get pointer to nonces
        memcpy(buffer, persist, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);  // copy in to buffer
        store.putBytes("nonces", buffer, RADIOLIB_LORAWAN_NONCES_BUF_SIZE); // send them to the store

        // we'll save the session after an uplink
        if (state != RADIOLIB_LORAWAN_NEW_SESSION) {

            lv_label_set_text_fmt(label, "Join failed: %d\nRetrying join in %u s", state, sleepForSeconds);

            Serial.print(F("Join failed: "));
            Serial.println(state);

            // how long to wait before join attempts. This is an interim solution pending
            // implementation of TS001 LoRaWAN Specification section #7 - this doc applies to v1.0.4 & v1.1
            // it sleeps for longer & longer durations to give time for any gateway issues to resolve
            // or whatever is interfering with the device <-> gateway airwaves.
            // uint32_t sleepForSeconds = min((bootCountSinceUnsuccessfulJoin++ + 1UL) * 60UL, 3UL * 60UL);
            Serial.print(F("Boots since unsuccessful join: "));
            // Serial.println(bootCountSinceUnsuccessfulJoin);
            Serial.print(F("Retrying join in "));
            Serial.print(sleepForSeconds);
            Serial.println(F(" seconds"));

            lv_timer_handler();

            delay(sleepForSeconds);
        }

    } // if activateOTAA state

    lv_label_set_text(label, "Join successed");
}

void setParams()
{
    // Print the DevAddr
    Serial.print("[LoRaWAN] DevAddr: ");
    Serial.println((unsigned long)node.getDevAddr(), HEX);

    // Enable the ADR algorithm (on by default which is preferable)
    node.setADR(true);

    // Set a datarate to start off with
    node.setDatarate(5);

    // Manages uplink intervals to the TTN Fair Use Policy
    node.setDutyCycle(true, 1250);

    // Enable the dwell time limits - 400ms is the limit for the US
    node.setDwellTime(true, 400);

    Serial.println(F("Ready!\n"));
}


void uploadLink()
{
    static uint32_t nextUplinkTime = 0;

    int16_t state = RADIOLIB_ERR_NONE;

    if (millis() < nextUplinkTime) {
        return;
    }

    // set battery fill level - the LoRaWAN network server
    // may periodically request this information
    // 0 = external power source
    // 1 = lowest (empty battery)
    // 254 = highest (full battery)
    // 255 = unable to measure
    uint8_t battLevel = 146;
    node.setDeviceStatus(battLevel);

    // This is the place to gather the sensor inputs
    // Instead of reading any real sensor, we just generate some random numbers as example
    uint8_t value1 = radio.random(100);
    uint16_t value2 = radio.random(2000);

    // Build payload byte array
    uint8_t uplinkPayload[3];
    uplinkPayload[0] = value1;
    uplinkPayload[1] = highByte(value2);   // See notes for high/lowByte functions
    uplinkPayload[2] = lowByte(value2);

    uint8_t downlinkPayload[10];  // Make sure this fits your plans!
    size_t  downlinkSize;         // To hold the actual payload size received

    // you can also retrieve additional information about an uplink or
    // downlink by passing a reference to LoRaWANEvent_t structure
    LoRaWANEvent_t uplinkDetails;
    LoRaWANEvent_t downlinkDetails;

    uint8_t fPort = 10;

    // Retrieve the last uplink frame counter
    uint32_t fCntUp = node.getFCntUp();

    // Send a confirmed uplink on the second uplink
    // and also request the LinkCheck and DeviceTime MAC commands
    Serial.println(F("Sending uplink"));

    if (fCntUp == 1) {
        Serial.println(F("and requesting LinkCheck and DeviceTime"));
        node.sendMacCommandReq(RADIOLIB_LORAWAN_MAC_LINK_CHECK);
        node.sendMacCommandReq(RADIOLIB_LORAWAN_MAC_DEVICE_TIME);
        state = node.sendReceive(uplinkPayload, sizeof(uplinkPayload), fPort, downlinkPayload, &downlinkSize, true, &uplinkDetails, &downlinkDetails);
    } else {
        state = node.sendReceive(uplinkPayload, sizeof(uplinkPayload), fPort, downlinkPayload, &downlinkSize, false, &uplinkDetails, &downlinkDetails);
    }
    debug(state < RADIOLIB_ERR_NONE, F("Error in sendReceive"), state, false);

    // Check if a downlink was received
    // (state 0 = no downlink, state 1/2 = downlink in window Rx1/Rx2)
    if (state > 0) {
        Serial.println(F("Received a downlink"));
        // Did we get a downlink with data for us
        if (downlinkSize > 0) {
            Serial.println(F("Downlink data: "));
            arrayDump(downlinkPayload, downlinkSize);
        } else {
            Serial.println(F("<MAC commands only>"));
        }



        // print RSSI (Received Signal Strength Indicator)
        Serial.print(F("[LoRaWAN] RSSI:\t\t"));
        Serial.print(radio.getRSSI());
        Serial.println(F(" dBm"));

        // print SNR (Signal-to-Noise Ratio)
        Serial.print(F("[LoRaWAN] SNR:\t\t"));
        Serial.print(radio.getSNR());
        Serial.println(F(" dB"));

        // print extra information about the event
        Serial.println(F("[LoRaWAN] Event information:"));
        Serial.print(F("[LoRaWAN] Confirmed:\t"));
        Serial.println(downlinkDetails.confirmed);
        Serial.print(F("[LoRaWAN] Confirming:\t"));
        Serial.println(downlinkDetails.confirming);
        Serial.print(F("[LoRaWAN] Datarate:\t"));
        Serial.println(downlinkDetails.datarate);
        Serial.print(F("[LoRaWAN] Frequency:\t"));
        Serial.print(downlinkDetails.freq, 3);
        Serial.println(F(" MHz"));
        Serial.print(F("[LoRaWAN] Frame count:\t"));
        Serial.println(downlinkDetails.fCnt);
        Serial.print(F("[LoRaWAN] Port:\t\t"));
        Serial.println(downlinkDetails.fPort);
        Serial.print(F("[LoRaWAN] Time-on-air: \t"));
        Serial.print(node.getLastToA());
        Serial.println(F(" ms"));
        Serial.print(F("[LoRaWAN] Rx window: \t"));
        Serial.println(state);

        uint8_t margin = 0;
        uint8_t gwCnt = 0;
        if (node.getMacLinkCheckAns(&margin, &gwCnt) == RADIOLIB_ERR_NONE) {
            Serial.print(F("[LoRaWAN] LinkCheck margin:\t"));
            Serial.println(margin);
            Serial.print(F("[LoRaWAN] LinkCheck count:\t"));
            Serial.println(gwCnt);
        }

        uint32_t networkTime = 0;
        uint8_t fracSecond = 0;
        if (node.getMacDeviceTimeAns(&networkTime, &fracSecond, true) == RADIOLIB_ERR_NONE) {
            Serial.print(F("[LoRaWAN] DeviceTime Unix:\t"));
            Serial.println(networkTime);
            Serial.print(F("[LoRaWAN] DeviceTime second:\t1/"));
            Serial.println(fracSecond);
        }

    } else {
        Serial.println(F("[LoRaWAN] No downlink received"));
    }

    // wait before sending another packet
    uint32_t minimumDelay = uplinkIntervalSeconds * 1000UL;
    uint32_t interval = node.timeUntilUplink();     // calculate minimum duty cycle delay (per FUP & law!)
    uint32_t delayMs = max(interval, minimumDelay); // cannot send faster than duty cycle allows

    Serial.print(F("[LoRaWAN] Next uplink in "));
    Serial.print(delayMs / 1000);
    Serial.println(F(" seconds\n"));

    lv_label_set_text_fmt(label, "[Downlink]\nRSSI:%.2fdBm\nSNR:%.2fdB\nFREQ:%.2fMHz\nNextTime:%us",
                          radio.getRSSI(), radio.getSNR(), downlinkDetails.freq, delayMs / 1000 );

    nextUplinkTime += delayMs;
}

void loop()
{
    switch (join_state) {
    case LORAWAN_JOINING:
        joinHandle();
        join_state = LORAWAN_JOINED;
        break;
    case LORAWAN_JOINED:
        setParams();
        join_state = LORAWAN_UPLINK;
        break;
    case LORAWAN_UPLINK:
        uploadLink();
        break;
    default:
        break;
    }
    lv_timer_handler();
}
