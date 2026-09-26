# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

BabyBot: an ESP32-S3 child nightlight/alarm clock with a 240×320 ST7789T touch screen (CST328), an I2S DAC (PCM5101) for MP3 playback, an SD card, and a PN532 NFC reader. The UI is built with LVGL 8.3. See README.md for the pin mapping and the SD card layout. Code comments, logs and the README are in French, so write new comments in French too.

## Build / flash

ESP-IDF, target `esp32s3`. There are no tests and no lint step other than `.clang-format`: LLVM base, **tabs**, width 4.

```bash
. ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

- `components/` is **git-ignored but required**. It holds local copies of `lvgl__lvgl` (8.3.11), `chmorgan__esp-audio-player` and `chmorgan__esp-libhelix-mp3`. `managed_components/` is fetched automatically (cJSON, `garag/esp-idf-pn532`).
- Outside Windows, the top-level `CMakeLists.txt` switches the dependency lock to `dependencies.linux.lock` (`dependencies.lock` contains Windows paths). Do not merge the two.
- Every new `.c` file must be added to `SRCS` in `main/CMakeLists.txt`, and every new directory to `INCLUDE_DIRS`. Headers are included by bare name (e.g. `#include "PCM5101.h"`).
- `.clangd` hard-codes a GCC toolchain include path under `~/.espressif`. Update it if the toolchain version changes.

## Architecture

`main/` holds one directory per hardware driver (`Audio_Driver`, `LCD_Driver`, `Touch_Driver`, `LVGL_Driver`, `SD_Card`, `BAT_Driver`, `PWR_Key`, `Wireless`, `NFC_Tag`). All application and UI code lives in `main/cute/`.

**Tasks and threading** (`main/main.c`):
- `app_main` initializes SD → LCD → Audio → LVGL → `app_manager_init()` (loads `/sdcard/settings/config.json`), shows the splash screen and plays `startup.mp3`. It then loops forever. Each pass it drains `app_msg_queue`, calls `wakeup()` (the alarm check) and calls `lv_timer_handler()`. **This loop is the only LVGL task, and there is no LVGL mutex.** Do not call `lv_*` from any other task. Send a message on `app_msg_queue` instead.
- `Driver_Loop` (core 0) runs `Wireless_Init()`: it reads Wi-Fi credentials from `/sdcard/wifi.txt` (JSON), then on connection calls `app_manager_setup_time()` (SNTP, Europe/Paris TZ), which posts `MSG_TIME_READY`. It then polls the battery and power key every 100 ms.
- On `MSG_TIME_READY`, the main loop switches to the robot screen, starts `nfc_task` (core 1) and stops Wi-Fi. Wi-Fi is used only for the initial time sync.
- `nfc_task` calls `app_manager_notify(EVENT_NFC_TAG / EVENT_NFC_TAG_REMOVED)` directly from its own task. When a tag is placed, the app plays `/sdcard/<UID hex>.mp3` if that file exists. When the tag is removed, the music stops.

**app_manager** (`cute/app_manager.[ch]`) is the central hub:
- Global state: mood and `alarm_t` (the `days` bitmask uses bit 0 = Monday; `in_settings` suppresses the alarm while it is being edited).
- An event dispatcher (`app_manager_notify`).
- Config persistence to `/sdcard/settings/config.json` via cJSON (`set_wakeup_config` writes it).
- Screen navigation via `app_manager_choose_frame(frame_select_t)`.

**Screens ("frames")**: each frame has its own `*_create()` function. That function builds a new `lv_obj_t` screen and calls `lv_scr_load` on it:
- `robot_cute.c` / `robot_face.c`: the robot's face and eyes
- `wakeup_settings.c` / `wakeup_sound.c`: alarm settings
- `wireframe_clock_lvgl.c`: clock
- `mp3_player_lvgl.c`: music player
- `baby_splash_screen.c`: splash screen

Prototypes for several of these are in `draw_function.h`. Touching anywhere opens the circular menu `pie_dialog_lvgl.c` (`pie_dialog_open`), which calls `app_manager_choose_frame`. To add a screen: add a `FRAME_*` enum value, add a case in `app_manager_choose_frame`, and add an entry in the pie menu.

`*_png.c` files (`robot_png.c`, `horloge_png.c`, …) are generated LVGL image arrays (menu icons, declared in `pie_icons.h`). Do not edit them by hand.

**Audio**: `PCM5101.c` wraps `esp-audio-player`: `Play_Music(dir, file)`, `Play_Music_ex(path)`, `Music_stop/pause/resume` and `Volume_adjustment`.
