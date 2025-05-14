/*
  RadioLib nRF24 Receive Example

  This example listens for FSK transmissions using nRF24 2.4 GHz radio module.
  Once a packet is received, an interrupt is triggered.
  To successfully receive data, the following settings have to be the same
  on both transmitter and receiver:
  - carrier frequency
  - data rate
  - transmit pipe on transmitter must match receive pipe
    on receiver

  For default module settings, see the wiki page
  https://github.com/jgromes/RadioLib/wiki/Default-configuration#radio

  For full API reference, see the GitHub Pages
  https://jgromes.github.io/RadioLib/
*/

#include <LilyGoLib.h>
#include <LV_Helper.h>

lv_obj_t *label;

// flag to indicate that a packet was received
volatile bool receivedFlag = false;

ICACHE_RAM_ATTR void setFlag(void)
{
    // we got a packet, set the flag
    receivedFlag = true;
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    const char *example_title = "nRF24_Receive";
    label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_label_set_text(label, example_title);
    lv_obj_center(label);
    lv_timer_handler();

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    // initialize nRF24 with default settings
    Serial.print(F("[nRF24] Initializing ... "));
    int state = nrf24.begin();
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("success!"));
    } else {
        Serial.print(F("failed, code "));
        Serial.println(state);
        lv_label_set_text_fmt(label, "nrf24 init failed : %d\nnRF2401 is not integrated into the T-LoRa-Pager,\n and an additional module needs to be inserted\n into the expansion port of the Pager to be used", state);
        while (true) {
            lv_timer_handler();
            delay(10);
        }
    }

    // set receive pipe 0 address
    // NOTE: address width in bytes MUST be equal to the
    //       width set in begin() or setAddressWidth()
    //       methods (5 by default)
    Serial.print(F("[nRF24] Setting address for receive pipe 0 ... "));
    byte addr[] = {0x01, 0x23, 0x45, 0x67, 0x89};
    state = nrf24.setReceivePipe(0, addr);
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("success!"));
    } else {
        Serial.print(F("failed, code "));
        Serial.println(state);
        while (true) {
            delay(10);
        }
    }

    // set the function that will be called
    // when new packet is received
    nrf24.setPacketReceivedAction(setFlag);

    // start listening
    Serial.print(F("[nRF24] Starting to listen ... "));
    state = nrf24.startReceive();
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("success!"));
    } else {
        Serial.print(F("failed, code "));
        Serial.println(state);
        while (true) {
            delay(10);
        }
    }

    // if needed, 'listen' mode can be disabled by calling
    // any of the following methods:
    //
    // nrf24.standby()
    // nrf24.sleep()
    // nrf24.transmit();
    // nrf24.receive();
    // nrf24.readData();


    lv_label_set_text(label, "nRF24 Starting to listen");
}

void loop()
{

    instance.loop();

    lv_task_handler();

    // check if the flag is set
    if (receivedFlag) {
        // reset flag
        receivedFlag = false;

        // you can read received data as an Arduino String
        String str;
        int state = nrf24.readData(str);

        // you can also read received data as byte array
        /*
          byte byteArr[8];
          int numBytes = nrf24.getPacketLength();
          int state = nrf24.readData(byteArr, numBytes);
        */

        if (state == RADIOLIB_ERR_NONE) {
            // packet was successfully received
            Serial.println(F("[nRF24] Received packet!"));

            // print data of the packet
            Serial.print(F("[nRF24] Data:\t\t"));
            Serial.println(str);

            lv_label_set_text_fmt(label, "Received:\nData:%s", str.c_str());

        } else {
            // some other error occurred
            Serial.print(F("[nRF24] Failed, code "));
            Serial.println(state);

            lv_label_set_text_fmt(label, "Receive failed:%d", state);
        }

        // put module back to listen mode
        nrf24.startReceive();
    }
}
