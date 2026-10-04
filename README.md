<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Instrument Metronome — FoloToy AI Passport

An offline instrument metronome for the FoloToy AI Passport (ESP32-C3, 240 × 320 display, three
buttons, ES8311 speaker). The interface is in Simplified Chinese. Clicks are placed with
sample accuracy and never drift during long sessions, and the on-screen beat dots and pendulum
light up at the moment each click actually leaves the speaker.

This `feature/metronome` branch replaces the baseline hardware-test menu with a newly designed
metronome UI: the device boots straight into the metronome and never shows the test menu or demo
pages. The baseline `demo_*.c` files remain in the tree for host tests but are not compiled into
the firmware.

<p align="center">
  <img src="assets/images/metronome-preview.png" alt="Metronome screens: playing, 7/4 with triplets, settings panel, tap tempo" width="100%">
</p>

> The preview is rendered on the host by `tools/render_metronome_preview.py` with the real LVGL
> UI code; it is not a photo of the device.

## Features

| Feature | Details |
| --- | --- |
| Tempo | 30–250 BPM; each press changes ±1, holding accelerates (±1 at first, then jumps to multiples of 5 after about 2 s) and stops on release |
| Meter and accent | 1–12 beats per bar; the downbeat accent can be turned on or off |
| Subdivision | None / eighths / triplets / sixteenths; subdivision clicks are quieter |
| Tap tempo | Tap any key along with the music; averages up to the last 8 intervals and applies 2.5 s after the last tap |
| Sounds | Electronic beep, wood block, and cowbell, all synthesized in firmware at three strengths (accent / beat / subdivision) |
| Volume | Levels 0–10 (0 mutes) |
| Tempo term | Italian term and its Chinese name for the current BPM (for example Andante for 76–107 BPM) |
| Persistence | Settings are stored in NVS; Flash is written only while stopped to avoid audio dropouts |
| Power saving | After 60 s idle while stopped the backlight dims to 10%; any key wakes it, and the waking press is ignored |
| Battery | Fuel-gauge reading at the top right; hidden when unavailable |

## Controls

| Screen | UP / DOWN | OK click | OK double | OK long |
| --- | --- | --- | --- | --- |
| Main | Press ±1 BPM; hold to accelerate | Start / stop | Tap tempo | Open settings |
| Settings (browse) | Move the selection | Edit the row (the tap-tempo row opens tap tempo) | — | Back to main |
| Settings (edit) | Change the value (beats and volume repeat while held) | Confirm | — | Back to main |
| Tap tempo | Any key press is a tap | (also a tap) | — | Cancel without changing the tempo |

The metronome keeps playing while the settings panel is open, so every change is audible
immediately; an indicator in the panel header flashes on each beat. Tap tempo pauses playback
and restores the previous playing state when it finishes.

## How the timing stays accurate

- **Samples are the clock:** the audio task (priority 6) renders one 240-sample block every
  15 ms and writes it to I2S with a blocking call. `main/mn_sched.c` advances tick positions with
  an integer quotient and remainder accumulator, so tick k lands exactly at
  `floor(k × 16000 × 60 / (BPM × subdivision))`. The error is always below one sample (62.5 µs)
  and never accumulates.
- **Audio-visual sync:** on start the task fills the whole DMA output queue (6 × 240 samples,
  about 90 ms) with silence, then keeps it full, so each tick's audible time follows from the time
  its write returned. A 20 ms LVGL timer pops events whose audible time has arrived and only then
  lights the beat dot and flashes the pendulum bob. The pendulum follows a cosine path and reaches
  an extreme on every beat.
- **Parameter changes:** BPM changes reschedule from the previous tick immediately; subdivision
  changes apply at the next beat; reducing the beat count wraps to beat 1 at the next beat.
- **No audio interruptions:** settings are written to Flash only after the metronome has stopped
  and the last PCM block has played; changes made while running are saved after stopping.

## Build and flash

ESP-IDF 5.5.3 is required (see [environment setup](docs/development/engineering/environment-setup.md)).

```bash
source ~/esp/esp-idf-v5.5.3/export.sh
./tools/validate.sh            # repository checks, host tests, firmware build, merged-image verification
```

The deliverable is `build/FoloToy-AI-Passport-full.bin`, flashed at `0x0`:

```bash
python -m esptool --chip esp32c3 -p <port> -b 460800 write-flash 0x0 build/FoloToy-AI-Passport-full.bin
```

The merged image overwrites NVS, so saved settings return to their defaults; see
[flashing and stored data](docs/development/engineering/firmware-layout.md#flashing-and-stored-data).
Matching ELF and MAP files are archived under `build/firmware/<sha256 of full.bin>/`.

Regenerate the font subsets after changing UI text (see [`assets/README.md`](assets/README.md)).
`python3 tools/render_metronome_preview.py` renders every screen on the host and reports the
peak LVGL pool usage.

## Code layout

| File | Purpose |
| --- | --- |
| `main/mn_sched.*` | Sample-accurate beat scheduling and single-voice mixing (pure logic) |
| `main/mn_sound.*` | Click synthesis for three sounds × three strengths (pure logic) |
| `main/mn_tap.*` | Tap tempo (pure logic) |
| `main/mn_tempo.*`, `main/mn_layout.*` | Tempo terms, hold-to-repeat pacing, beat-dot layout, pendulum angle (pure logic) |
| `main/mn_cfg.*`, `main/mn_crc32.*` | Setting defaults, ranges, and the CRC-protected record format (pure logic) |
| `main/mn_model.*` | Screen state machine, key mapping, backlight dimming and wake (pure logic) |
| `main/mn_audio.*` | Audio task and audio-visual sync events |
| `main/mn_store.*` | NVS storage |
| `main/mn_app.*`, `main/main.c` | Application task and firmware entry point |
| `main/mn_ui*.c`, `main/mn_theme.h`, `main/mn_strings.h`, `main/mn_fonts.*` | UI, palette, all Chinese text, and the font self-check |
| `components/bsp` | Reused board support; this branch adds the `BSP_BTN_RELEASE` key event and exposes the I2S DMA queue size |
| `tests/test_mn_*.c`, `tests/test_metronome_fonts.py` | Host tests run by `tools/validate.sh --static` |

## Known limitations and items to confirm on hardware

- Audio-visual sync assumes the DMA queue stays full; the remaining offset must be checked by eye
  on the device. A constant offset can be compensated in `mn_ui.c`.
- The volume-level to codec-percentage mapping (level 1 = 46% … level 10 = 100%) is a starting
  point; the loudness curve needs listening tests on the device.
- Tap-tempo accuracy is limited by the 5 ms button polling: single intervals jitter by about
  ±5 ms, and the average is within about ±1 BPM.
- All three keys share one ADC input, so key combinations are not supported.
