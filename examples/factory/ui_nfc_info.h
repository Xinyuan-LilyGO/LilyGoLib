/**
 * @file      ui_nfc_info.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-25
 *
 * Separates ST25R3916 radio capability from implemented NFC payload operations.
 */
#pragma once

struct UINfcSupport {
    const char *card;
    bool hardware, read, write, emulate;
    const char *details;
};

static constexpr UINfcSupport kNfcSupport[] = {
    {"NFC-A / Type 1", true, false, false, false,
     "NFC-A radio detection is available. Type 1 payload reading, writing and emulation are not implemented."},
    {"NFC-A / Type 2", true, true, true, true,
     "Read NDEF and capture .t2t pages. Generic captures can be partial. Physical image writing supports compatible NTAG213/215/216 user pages only. UID, locks and configuration are preserved. Type 2 image emulation is experimental; optional writes affect RAM only."},
    {"MIFARE Classic", true, true, false, true,
     "Read Mini/1K/4K blocks with available keys and save .mfc images. Physical card writing is unavailable. Experimental read-only emulation requires a supported four-byte UID image and known keys. Protected data without keys cannot be reconstructed."},
    {"NFC-A / Type 4", true, true, true, true,
     "Read, write or clear NDEF on compatible formatted, writable cards. Emulation serves NDEF content as an NFC-A Type 4 tag; it does not reproduce the source UID, protected applications or an entire smart card."},
    {"NFC-B / Type 4", true, true, true, false,
     "Read, write or clear NDEF on compatible formatted, writable NFC-B cards. NFC-B card emulation is not implemented."},
    {"NFC-F / Type 3", true, true, true, true,
     "Read, write or clear NDEF on compatible formatted, writable cards. Emulation serves NDEF content as an NFC-F Type 3 tag; it does not copy protected services or the full original card."},
    {"NFC-V / Type 5", true, true, true, false,
     "ISO15693 radio and compatible Type 5 NDEF reading, writing and clearing are available. NFC-V emulation is not implemented."},
    {"ST25TB", true, false, false, false,
     "The reader can discover and identify these cards. This app has no payload dump, write or emulation workflow for them."},
    {"DESFire secure apps", true, false, false, false,
     "ISO14443 radio support does not provide secure application keys or full card cloning. Dedicated protected application reading, writing and emulation are unavailable. Compatible NDEF applications fall under NFC-A Type 4."},
    {"125 kHz LF tags", false, false, false, false,
     "This NFC front end operates at 13.56 MHz. It cannot read, write or emulate 125 kHz low-frequency tags."},
};
