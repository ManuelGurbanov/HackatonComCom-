---
name: hackathon-board
description: Everything known about the ESP32-S3 hackathon dev board in this repo (pinout, SSD1306 OLED, joystick/keys, speaker, PDM mic, WiFi, build/flash flow, gotchas). Use when writing or debugging firmware for this board, drawing on the display, reading buttons, playing sound, or networking two boards.
---

# Hackathon board (ESP32-S3)

Sources of truth: `example/` (hardware demo) and `truco/` (full game built on the same drivers).
Copy drivers (`display.*`, `buttons.*`, `audio.*`, `board.h`) from `example/main/` into a new project; `truco/` shows how.
Status legend: **[verified]** compiled/ran, **[compiled]** builds but untested on hardware, **[unknown]** not checked.

## Toolchain
- ESP-IDF v6.1 at `~/esp-idf-v6.1` (also `~/esp-idf`). `. ~/esp-idf-v6.1/export.sh` then `idf.py set-target esp32s3`.
- Flash 16 MB, console over USB Serial/JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`) -> port is usually `/dev/ttyACM0`.
- `idf.py -p /dev/ttyACM0 flash monitor`. Any firmware change needs a reflash.
- `sdkconfig.defaults` in the examples: USB console, 16 MB flash, main stack 4096 (use 6144 once you use WiFi/snprintf-heavy code).
- Project skeleton: top-level `CMakeLists.txt` (`include($ENV{IDF_PATH}/tools/cmake/project.cmake)`), `main/CMakeLists.txt` with `idf_component_register(SRCS ... PRIV_REQUIRES ...)`, `main/Kconfig.projbuild` (volume, display rotation, joystick rotation).
- PRIV_REQUIRES that worked in IDF 6.1: `esp_lcd esp_driver_spi esp_driver_i2s esp_driver_gpio` for drivers; add `esp_wifi esp_netif esp_event nvs_flash lwip` for networking. `esp_random.h` needs no extra require.

## Pinout (`board.h`)
| Function | GPIO |
|---|---|
| Speaker I2S (MAX98357A): DIN / BCLK / LRCLK | 4 / 5 / 6 |
| Mic (PDM): CLK / DOUT | 7 / 15 |
| OLED SSD1306 SPI: SCL / SDA(MOSI) / RST / DC / CS | 36 / 37 / 40 / 39 / 38 |
| Joystick UP / DOWN / LEFT / RIGHT | 9 / 11 / 12 / 10 |
| Joystick CENTER | 13 (**not wired**; can be jumpered to a free pin) |
| KEY1 / KEY2 / KEY3 / KEY4 | 14 / 21 / 47 / 48 |
| I2C SDA / SCL | 1 / 2 (broken out, unused by the examples; attached devices **[unknown]**) |

## Display (`display.c`) **[verified]**
- SSD1306, 128x64, 1 bpp, SPI2_HOST at 10 MHz, 4-wire (DC pin) via `esp_lcd_new_panel_ssd1306`.
- You draw into a RAM framebuffer (`s_fb`, 1024 bytes, page layout: byte index = x + (y/8)*128, bit = y%8), then `display_flush()` pushes it (blocks on a semaphore, 200 ms timeout).
- API: `display_init`, `display_clear`, `display_set_pixel`, `display_fill_rect(x,y,w,h,on)`, `display_draw_rect(x,y,w,h)` (outline), `display_text(x,y,str,invert)`, `display_flush`.
- Font: 5x7 glyphs in a 6x8 cell (`DISPLAY_FONT_W/H`), ASCII 32..126 only (other chars show `?`) -> 21 columns x 8 rows. No accents/lowercase-specific tricks needed, lowercase exists in the table.
- `display_text(..., invert=true)` draws a filled white box and black text; `false` clears the cell then draws white text. To draw a glyph inverted *inside your own filled rect*, call `display_text` per char with `invert=true` (what `ui.c::draw_card` does).
- Text overflowing x>127 is clipped silently (set_pixel bounds-checks).
- Upside-down image: enable `HACK_DISPLAY_ROTATE_180` in menuconfig (calls `esp_lcd_panel_mirror(true,true)`).
- Redraw loop pattern: clear -> draw everything -> flush every ~40-50 ms.

## Buttons (`buttons.c`) **[verified for keys, joystick rotation compiled]**
- Active low with internal pull-ups, `GPIO_INTR_ANYEDGE` ISR per pin -> ISR queue -> task (prio 10) waits 20 ms debounce, re-reads the level, updates state and pushes `button_event_t {id, pressed}` on the event queue.
- API: `buttons_init()`, `buttons_event_queue()` (block on it with `xQueueReceive`), `buttons_read()` (bitmask `1<<button_id_t`), `buttons_name()`.
- `button_id_t`: UP=0, DOWN, LEFT, RIGHT, CENTER, KEY1..KEY4. Directions are bits 0..3 (`0xF` mask).
- **Joystick is mounted rotated 45 degrees**: one physical push toggles two adjacent contacts. `truco/main/buttons.c` fixes it in software: a logical direction is pressed when both of its contacts are pressed; single-contact diagonals are ignored. Default assumes clockwise rotation; flip with menuconfig `HACK_JOYSTICK_ROT_CCW` if directions are swapped. `example/` still has the raw (unrotated) behavior.
- Only one task should consume the event queue (it is a single queue, not a broadcast).

## Audio (`audio.c`) **[verified code, played tones in example]**
- Speaker: MAX98357A on I2S1, std Philips, 16 kHz, 16-bit stereo. `audio_play_tone(freq_hz, ms)` is **blocking** (generates sine with 5 ms fades). Loud even at low amplitude: `CONFIG_HACK_SPEAKER_VOLUME` (percent, default 25).
- Mic: PDM on I2S0; background task computes a smoothed 0..100 level, read with `audio_mic_level()`. Raw samples are not exposed.
- `audio_init()` starts both; if it fails, run without sound (apps check an `audio_ok` flag).
- Do not call `audio_play_tone` from a time-critical loop (it blocks ~tone length).

## Networking **[compiled; not yet run on two real boards]**
- Built-in WiFi (2.4 GHz) and BLE 5 on the ESP32-S3. **No Zigbee/802.15.4 radio** on the S3 (that needs ESP32-C6/H2). This answers question 1 in `preguntas.md`.
- `truco/main/net.c` is a reusable pattern: server board = SoftAP `TRUCO-ESP32` (pass `truco-hackaton`, IP `192.168.4.1`) + TCP server :3333; client board = STA + TCP client to 192.168.4.1. Length-prefixed-by-type frames, TCP_NODELAY, aggressive keepalive (~6 s drop detection), `net_session()` counter to detect reconnects, mutex around send.
- `nvs_flash_init()` is mandatory before WiFi. Client must retry `esp_wifi_connect()` on `STA_DISCONNECTED`.
- A PC can stand in for a second board: `truco/tools/sim_client.py` (joins the AP, speaks the protocol; `--bot` plays alone) and `truco/tools/host_server.c` (game logic compiled on the PC, for testing without hardware).

## Gotchas
- Pins 9-14, 21, 47, 48 are used by keys; 36-40 by the display. Check the ESP32-S3 GPIO docs before reusing other pins (e.g. 26-37 may be flash/PSRAM on some modules; strapping pins 0/3/45/46).
- `app_main` stack: 4096 is too small for WiFi + snprintf buffers -> 6144.
- `display_flush` timeout means a broken SPI wiring yields `flush timeout`, the demo continues without screen if init fails.
- Default `sdkconfig` is generated per project (gitignored); changing `sdkconfig.defaults` after the first build requires deleting `sdkconfig` or using menuconfig.
- Logs: `ESP_LOGI(TAG, ...)` over the USB console; view with `idf.py monitor` (exit: Ctrl+]).

## The Truco app (`truco/`)
- `game.c/h`: pure rules (40-card Spanish deck, truco/retruco/vale4, envido/real/falta, mazo, 15 points, mano alternates every hand). Server is authoritative; each player only receives a `view_t` (30 packed bytes).
- `ui.c`: layout and button mapping (header, table, event line, hand, 4-label action bar = KEY1..KEY4). Left/Right pick a card, Up/Down pick envido type.
- `main.c`: role chosen at boot (KEY1 server, KEY2 client), server loop owns the game, client loop renders views and sends actions.
- Simplifications: envido only in the first trick before any truco; no envido re-raises; calls only on your own turn.
