/**
 * Browser-accessible capture of the screen last written by LVGL.
 */
#if defined(ARDUINO) && defined(LILYGO_SCREEN_CAPTURE_ENABLED) && LILYGO_SCREEN_CAPTURE_ENABLED

#include <Arduino.h>
#include <ESPmDNS.h>
#include <LV_Helper.h>
#include <LilyGoLog.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#ifndef LODEPNG_NO_COMPILE_CPP
#define LODEPNG_NO_COMPILE_CPP
#endif
#include <libs/lodepng/lodepng.h>

#include "screen_web_server.h"

#define SCREEN_WEB_SERVER_PORT 80
#define SCREEN_WEB_WRITE_TIMEOUT_MS 5000
#define SCREEN_WEB_WRITE_CHUNK_SIZE 4096
#define SCREEN_WEB_TASK_STACK_SIZE 8192
#define SCREEN_WEB_TASK_PRIORITY 1

static WebServer screen_server(SCREEN_WEB_SERVER_PORT);
static bool screen_server_configured = false;
static bool screen_server_running = false;
static bool screen_mdns_running = false;
static char screen_server_hostname[32] = "lilygo-screen";
static TaskHandle_t screen_server_task_handle = NULL;
static uint8_t *screen_rgb_buffer = NULL;
static size_t screen_rgb_capacity = 0;

extern void instanceLockTake();
extern void instanceLockGive();

static const char screen_server_index_html[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>LilyGo Screen Capture</title>
  <style>
    :root{color-scheme:dark;--bg:#111315;--panel:#1b1e20;--line:#353a3e;--text:#f3f5f6;--muted:#a9b0b5;--accent:#00c99a}
    *{box-sizing:border-box}
    body{margin:0;min-height:100vh;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif;display:grid;place-items:center;padding:20px}
    main{width:min(960px,100%)}
    header{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:14px}
    h1{font-size:20px;font-weight:650;margin:0;letter-spacing:0}
    .controls{display:flex;align-items:center;flex-wrap:wrap;gap:8px}
    button,a{border:1px solid var(--line);border-radius:6px;background:var(--panel);color:var(--text);padding:9px 13px;text-decoration:none;cursor:pointer;font:inherit}
    button:hover,a:hover{border-color:var(--accent)}
    label{display:flex;align-items:center;gap:7px;color:var(--muted);padding:0 4px}
    input{accent-color:var(--accent)}
    .screen{min-height:220px;border:1px solid var(--line);background:#000;display:grid;place-items:center;overflow:auto;padding:12px}
    img{display:block;max-width:100%;height:auto;image-rendering:auto}
    footer{color:var(--muted);font-size:12px;margin-top:10px}
    @media(max-width:600px){header{align-items:flex-start;flex-direction:column}.screen{padding:6px}}
  </style>
</head>
<body>
  <main>
    <header>
      <h1>LilyGo Screen Capture</h1>
      <div class="controls">
        <button id="refresh" type="button">Refresh</button>
        <a id="download" href="/screenshot.png?download=1">Download PNG</a>
        <label><input id="auto" type="checkbox">Auto refresh</label>
      </div>
    </header>
    <div class="screen"><img id="capture" src="/screenshot.png" alt="Device screen"></div>
    <footer id="status">Loading current device screen...</footer>
  </main>
  <script>
    const image=document.getElementById('capture');
    const status=document.getElementById('status');
    const auto=document.getElementById('auto');
    let timer=0;
    function url(download=false){return '/screenshot.png?t='+Date.now()+(download?'&download=1':'')}
    function refresh(){status.textContent='Refreshing...';image.src=url()}
    image.addEventListener('load',()=>{status.textContent=image.naturalWidth+' x '+image.naturalHeight+' pixels'});
    image.addEventListener('error',()=>{status.textContent='Screenshot unavailable';});
    document.getElementById('refresh').addEventListener('click',refresh);
    document.getElementById('download').addEventListener('click',e=>{e.currentTarget.href=url(true)});
    auto.addEventListener('change',()=>{clearInterval(timer);timer=auto.checked?setInterval(refresh,2000):0});
  </script>
</body>
</html>
)HTML";

static bool write_all(NetworkClient &client, const uint8_t *data, size_t length)
{
    size_t offset = 0;
    uint32_t last_progress = millis();
    while (offset < length && client.connected()) {
        size_t remaining = length - offset;
        size_t chunk_size = remaining > SCREEN_WEB_WRITE_CHUNK_SIZE
                            ? SCREEN_WEB_WRITE_CHUNK_SIZE : remaining;
        size_t written = client.write(data + offset, chunk_size);
        if (written > 0) {
            offset += written;
            last_progress = millis();
            taskYIELD();
        } else {
            if (millis() - last_progress >= SCREEN_WEB_WRITE_TIMEOUT_MS) break;
            delay(1);
        }
    }
    return offset == length;
}

static bool ensure_rgb_capacity(size_t required)
{
    if (screen_rgb_buffer && screen_rgb_capacity >= required) {
        return true;
    }

    uint8_t *replacement = (uint8_t *)heap_caps_malloc(required,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!replacement) {
        replacement = (uint8_t *)heap_caps_malloc(required, MALLOC_CAP_8BIT);
    }
    if (!replacement) {
        return false;
    }

    if (screen_rgb_buffer) {
        heap_caps_free(screen_rgb_buffer);
    }
    screen_rgb_buffer = replacement;
    screen_rgb_capacity = required;
    return true;
}

static bool prepare_screenshot(uint8_t **png_data_out, size_t *png_size_out,
                               uint32_t *encode_time_out)
{
    if (!png_data_out || !png_size_out || !encode_time_out) return false;
    *png_data_out = NULL;
    *png_size_out = 0;
    *encode_time_out = 0;

    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        lv_screen_capture_t capture = {};
        instanceLockTake();
        bool available = lv_get_screen_capture(&capture);
        instanceLockGive();
        if (!available) return false;

        size_t pixel_count = (size_t)capture.width * capture.height;
        size_t rgb_size = pixel_count * 3U;
        if (!ensure_rgb_capacity(rgb_size)) return false;

        instanceLockTake();
        lv_screen_capture_t current = {};
        available = lv_get_screen_capture(&current);
        if (!available || current.width != capture.width || current.height != capture.height) {
            instanceLockGive();
            continue;
        }

        memcpy(screen_rgb_buffer, current.pixels, pixel_count * sizeof(uint16_t));
        bool rgb565_swapped = current.rgb565_swapped;
        instanceLockGive();

        // Expand backwards so the RGB565 snapshot and RGB888 input share one PSRAM buffer.
        const uint16_t *rgb565 = (const uint16_t *)screen_rgb_buffer;
        for (size_t index = pixel_count; index > 0; --index) {
            uint16_t pixel = rgb565[index - 1];
            if (rgb565_swapped) {
                pixel = (uint16_t)((pixel << 8) | (pixel >> 8));
            }
            uint8_t red = (uint8_t)((pixel >> 11) & 0x1F);
            uint8_t green = (uint8_t)((pixel >> 5) & 0x3F);
            uint8_t blue = (uint8_t)(pixel & 0x1F);
            uint8_t *dst = screen_rgb_buffer + (index - 1) * 3U;
            dst[0] = (uint8_t)((red << 3) | (red >> 2));
            dst[1] = (uint8_t)((green << 2) | (green >> 4));
            dst[2] = (uint8_t)((blue << 3) | (blue >> 2));
        }

        uint32_t encode_started = millis();
        unsigned error = lodepng_encode24(png_data_out, png_size_out,
                                          screen_rgb_buffer,
                                          capture.width, capture.height);
        *encode_time_out = millis() - encode_started;
        if (error) {
            LILYGO_LOG_E("PNG encode failed (%u): %s", error, lodepng_error_text(error));
            if (*png_data_out) {
                lv_free(*png_data_out);
                *png_data_out = NULL;
                *png_size_out = 0;
            }
            return false;
        }
        return *png_data_out != NULL && *png_size_out != 0;
    }

    return false;
}

static void handle_screenshot(void)
{
    LILYGO_LOG_D("Screen capture request received");

    uint8_t *png_data = NULL;
    size_t png_size = 0;
    uint32_t encode_time = 0;
    if (!prepare_screenshot(&png_data, &png_size, &encode_time)) {
        LILYGO_LOG_E("Failed to prepare screen capture");
        screen_server.send(503, "text/plain", "Screen capture buffer is unavailable.");
        return;
    }

    screen_server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    if (screen_server.hasArg("download")) {
        screen_server.sendHeader("Content-Disposition", "attachment; filename=lilygo-screen.png");
    } else {
        screen_server.sendHeader("Content-Disposition", "inline; filename=lilygo-screen.png");
    }
    screen_server.setContentLength(png_size);
    screen_server.send(200, "image/png", "");

    NetworkClient &client = screen_server.client();
    bool sent = write_all(client, png_data, png_size);
    LILYGO_LOG_D("PNG screen capture %s, size: %lu bytes, encoded in %lu ms",
                 sent ? "sent" : "send failed", (unsigned long)png_size,
                 (unsigned long)encode_time);
    lv_free(png_data);
}

static void handle_screen_info(void)
{
    lv_screen_capture_t capture = {};
    instanceLockTake();
    bool available = lv_get_screen_capture(&capture);
    instanceLockGive();
    if (!available) {
        screen_server.send(503, "application/json", "{\"available\":false}");
        return;
    }

    char json[128];
    snprintf(json, sizeof(json),
             "{\"available\":true,\"width\":%u,\"height\":%u,\"format\":\"PNG\"}",
             capture.width, capture.height);
    screen_server.sendHeader("Cache-Control", "no-store");
    screen_server.send(200, "application/json", json);
}

static void start_screen_server(void)
{
    if (screen_server_running) return;

    screen_server.begin();
    screen_server_running = true;
    screen_mdns_running = MDNS.begin(screen_server_hostname);
    if (screen_mdns_running) {
        MDNS.addService("http", "tcp", SCREEN_WEB_SERVER_PORT);
    }

    Serial.printf("Screen capture: http://%s/\n", WiFi.localIP().toString().c_str());
    if (screen_mdns_running) {
        Serial.printf("Screen capture: http://%s.local/\n", screen_server_hostname);
    }
}

static void stop_screen_server(void)
{
    if (!screen_server_running) return;

    screen_server.stop();
    screen_server_running = false;
    if (screen_mdns_running) {
        MDNS.end();
        screen_mdns_running = false;
    }
}

static void screen_server_task(void *param)
{
    (void)param;

    while (true) {
        bool connected = WiFi.status() == WL_CONNECTED && (uint32_t)WiFi.localIP() != 0;
        if (connected) {
            if (!screen_server_running) start_screen_server();
            screen_server.handleClient();
            vTaskDelay(pdMS_TO_TICKS(2));
        } else {
            stop_screen_server();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void screen_web_server_setup(void)
{
    if (screen_server_configured) return;

    uint64_t chip_id = ESP.getEfuseMac();
    snprintf(screen_server_hostname, sizeof(screen_server_hostname),
             "lilygo-screen-%06lx", (unsigned long)(chip_id & 0xFFFFFFUL));
    WiFi.setHostname(screen_server_hostname);

    screen_server.on("/", HTTP_GET, []() {
        screen_server.send_P(200, "text/html; charset=utf-8", screen_server_index_html);
    });
    screen_server.on("/screenshot.png", HTTP_GET, handle_screenshot);
    screen_server.on("/screenshot.bmp", HTTP_GET, []() {
        screen_server.sendHeader("Location", "/screenshot.png", true);
        screen_server.send(302);
    });
    screen_server.on("/api/info", HTTP_GET, handle_screen_info);
    screen_server.on("/favicon.ico", HTTP_GET, []() {
        screen_server.send(204);
    });
    screen_server.onNotFound([]() {
        screen_server.send(404, "text/plain", "Not found");
    });
    screen_server_configured = true;

#if CONFIG_FREERTOS_UNICORE
    BaseType_t result = xTaskCreate(screen_server_task,
                                    "screen/http",
                                    SCREEN_WEB_TASK_STACK_SIZE,
                                    NULL,
                                    SCREEN_WEB_TASK_PRIORITY,
                                    &screen_server_task_handle);
#else
    BaseType_t result = xTaskCreatePinnedToCore(screen_server_task,
                                                "screen/http",
                                                SCREEN_WEB_TASK_STACK_SIZE,
                                                NULL,
                                                SCREEN_WEB_TASK_PRIORITY,
                                                &screen_server_task_handle,
                                                ARDUINO_RUNNING_CORE == 0 ? 1 : 0);
#endif
    if (result != pdPASS) {
        screen_server_task_handle = NULL;
        LILYGO_LOG_E("Failed to create screen web server task");
    }
}

#endif // ARDUINO && LILYGO_SCREEN_CAPTURE_ENABLED
