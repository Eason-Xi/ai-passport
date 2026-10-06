<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# ASMR Soundscape Player — FoloToy AI Passport

An offline sleep and focus soundscape player for FoloToy AI Passport, with a Simplified Chinese UI
driven entirely by the three buttons. All 10 soundscapes (rain, waves, campfire, stream, wind,
crickets, singing bowl, and white / pink / brown noise) are **synthesized in real time** on the chip:
they use no storage, carry no licensing concerns, and have no loop seams — every raindrop and every
crackle is generated randomly. Up to three layers can be mixed, together with a fading sleep timer,
breathing guide, and automatic screen-off, so the device can sit on the pillow at bedtime.

<p align="center">
  <img src="assets/images/asmr-preview.png" alt="Screens: three-layer mix playing, mixer level editing, sleep timer, 4-7-8 breathing guide" width="100%">
</p>

> The preview is rendered on a computer by `tools/render_asmr_preview.py` with the real UI code and
> fonts; it is not a device photo.

## Quick start

1. Flash `build/FoloToy-AI-Passport-full.bin` (see [Flashing](#flashing)); the device boots straight into the listening page.
2. Press **OK** to play / pause (1 s fade), and **▲ / ▼** to change the master volume.
3. **Hold OK** for the menu: mixer (choose sounds, per-layer levels), sleep timer, and breathing guide. **Double-press OK** opens the breathing guide directly.

The first boot defaults to rain at level 8 plus campfire at level 4, master volume 5, and a 30-minute fading timer.

## Features

| Feature | Details |
| --- | --- |
| 10 soundscapes | Rain, waves, campfire, stream, wind, crickets, singing bowl, white, pink, and brown noise — all synthesized live, never repeating |
| Three-layer mix | Each of three tracks has its own sound and 0–10 level; changing a sound crossfades over 0.3 s without clicks |
| Live audition | While playing, moving through the sound picker swaps the sound in immediately; a long press cancels and restores it |
| Master volume | Levels 1–10 (codec about −29 dB to 0 dB, about 3 dB per step); hold ▲▼ to repeat |
| Sleep timer | Off / 15 / 30 / 45 / 60 / 90 minutes; the last minute fades along a square curve, then playback stops, a good-night screen is shown, and the screen turns off. The countdown freezes while paused |
| Breathing guide | 4-7-8, even (5-5), and box (4-4-4-4) breathing; an eased circle grows and shrinks with a seconds countdown while the sound keeps playing |
| Soundscape rings | Three rings on the listening page represent the three tracks in each sound's color and move with the live level of each layer |
| Persistence | Layer sounds and levels, master volume, timer option, and breathing pattern are saved and restored at boot (Flash is written only while silent, to avoid audio dropouts) |
| Screen power | Playing: dim after 20 s, screen off after 45 s, sound continues. Stopped: dim after 60 s, off after 2 min, auto power-off after 10 min. While dark, the first key press only wakes the screen |
| Low battery | Three consecutive readings at ≤ 3%: a low-battery notice for 3 s, then power-off; not triggered on computer USB or while charging |
| Offline | No Wi-Fi or Bluetooth; Bluetooth is disabled in the firmware |

## Controls

| Page | ▲ / ▼ | OK click | OK double | OK long |
| --- | --- | --- | --- | --- |
| Listening | Master volume ±1, hold to repeat | Play / pause (opens the mixer if no track is audible) | Breathing guide | Menu |
| Menu | Move (wraps) | Open: mixer / sleep timer / breathing / back | Same as click | Back to listening |
| Mixer | Select a track or Done | Track → sound picker; done → listening | Same as click | Back to listening |
| Mixer (editing level) | Layer level ±1, hold to repeat | Finish editing | Same as click | Finish editing |
| Sound picker | Move through 10 sounds and Empty (live audition while playing) | Confirm, then edit that layer's level | Same as click | Cancel and restore |
| Sleep timer | Scroll options | Confirm (countdown restarts now) | Same as click | Cancel |
| Breathing guide | Change pattern | Back to listening | Back | Back |
| Good-night screen | Any key press returns to the listening page | | | |

The breathing page stays lit for 5 minutes after it is opened (or the pattern changes); after that the
normal dimming rules apply, so the screen does not stay on all night if the user falls asleep.

## How the soundscapes are synthesized

The ESP32-C3 has no FPU, so synthesis uses Q15 / Q30 integer fixed point at 16 kHz, 16-bit mono, rendering one 240-sample block every 15 ms.

| Soundscape | Method |
| --- | --- |
| Rain | High-passed pink noise as the rain bed with a slowly drifting intensity; 25–75 random drops per second (noise grains), about 1 in 7 a rising "plink" into a puddle |
| Waves | Each wave lasts a random 7–13 s: an accelerating build-up, then a fast-then-slow retreat; higher waves open the filter, and foam is added near the crest |
| Campfire | A low flame roar that flickers; about 3 crackles per second with occasional 40–220 ms bursts; 1 in 5 is a muffled pop |
| Stream | A band-pass flow with a drifting center plus 30–60 bubbles per second (decaying sines whose pitch rises as they decay) |
| Wind | A resonant band-pass howl whose pitch and gust strength drift; stronger gusts raise the pitch |
| Crickets | Three crickets at different distances (4.3–5.1 kHz), 3–4 pulses per chirp with occasional pauses, over a faint night ambience |
| Singing bowl | A strike every 10–17 s on a random pentatonic note; paired, slightly detuned modes at 1 : 2.76 : 5.4 : 8.9 beat against each other and decay at different rates |
| White / pink / brown | White noise; Paul Kellet three-pole pink noise; brown noise from a 150 Hz one-pole low-pass |

Each soundscape's gain is calibrated by A-weighted loudness (after removing content below 300 Hz that a
small speaker cannot reproduce), so all sounds feel similar at the same layer level. The mixer sums the
three layers (up to six voices during crossfades), then applies the play fade, timer fade, a 120 Hz
high-pass, and a soft limiter.

## Code structure

Application code lives in `main/as_*`; hardware is accessed only through the BSP (`components/bsp` is reused without changing interface semantics).

| Module | Role | Dependencies |
| --- | --- | --- |
| `as_dsp` / `as_sounds` | Fixed-point DSP building blocks and the 10 soundscape synthesizers | Plain C, host-tested |
| `as_mixer` | Three-layer mix, crossfades, fades, soft limiter, level meters | Plain C, host-tested |
| `as_cfg` / `as_crc32` | Settings, 16-byte CRC-protected record, volume and timer tables | Plain C, host-tested |
| `as_breath` | Breathing phase computation and timer fade curve | Plain C, host-tested |
| `as_power` | Dim / screen-off / power-off timing, wake-key swallowing, low-battery decision | Plain C, host-tested |
| `as_model` | Page and button state machine, playback and sleep timer; returns effect bits | Plain C, host-tested |
| `as_ui*` / `as_fonts` / `as_strings.h` / `as_theme.h` | LVGL UI, glyph self-check, all Chinese text, colors | LVGL, host preview |
| `as_audio` | Audio task (priority 6): owns codec / I2S, writes the mix to DMA, logs render time | ESP-IDF |
| `as_store` | NVS persistence, written only while audio is silent | ESP-IDF |
| `as_app` | Application task (priority 5): button queue, effects, battery polling, deep-sleep power-off | ESP-IDF |

Threading: button callbacks only enqueue; only the application task mutates the state machine and
refreshes the UI while holding `bsp_lvgl_lock()`; animation runs in a 40 ms timer inside the LVGL task
and reads only the UI snapshot and the audio levels (atomic bytes).

## Build, tests, and previews

```bash
source ~/esp/esp-idf-v5.5.3/export.sh
./tools/validate.sh            # full gate: repository checks + host tests + firmware + merged image
./tools/validate.sh --static   # repository checks and host tests only
```

Host tests (`tests/test_as_*.c`, `tests/test_asmr_*.py`) cover DSP accuracy; level, DC, no clipping,
and determinism for all 10 soundscapes; noise color ordering; singing-bowl strike spacing; mixer fades,
ramp limits, crossfade queueing, three-layer full-level limiting, and the timer fade; settings round
trips, single-byte corruption, and out-of-range fields; breathing phases and the fade curve; power
levels, wake swallowing, and low battery; every page transition and the sleep timer in the state
machine; font coverage (with a negative case) and the UI literal rule; and the power-off call order
and wake source.

Development tools (not part of the firmware):

```bash
# Listen on a computer: render a 16 kHz WAV for each soundscape and a three-layer mix
cc -std=c11 -O2 -Imain tools/asmr_audio_preview.c main/as_dsp.c main/as_sounds.c main/as_mixer.c -lm -o build/asmr_audio_preview
build/asmr_audio_preview build/preview/audio 30

# Render every page to PNG with the real LVGL; reports the glyph check and LVGL pool peak
python3 tools/render_asmr_preview.py --out build/preview

# Regenerate the font subsets after editing main/as_strings.h (needs lv_font_conv 1.5.3 and the Source Han Sans OTF)
python3 tools/gen_asmr_fonts.py generate --lv-font-conv <lv_font_conv> --font <SourceHanSansSC-Regular.otf>
```

## Flashing

Write the merged image `build/FoloToy-AI-Passport-full.bin` at `0x0`:

```bash
python -m esptool --chip esp32c3 -b 460800 write_flash 0x0 build/FoloToy-AI-Passport-full.bin
```

The partition table is the repository default (NVS + PHY + one factory app). A full merged flash may
clear previous settings in NVS; the device then starts with the defaults.

## Test status and unverified items

- Verified on a computer: the full `./tools/validate.sh` gate passes; the host UI preview renders 18
  screens with no missing glyphs and an LVGL pool peak of about 24 KB of 48 KB; offline WAV renders
  pass spectrum and loudness analysis with no clipping.
- Device: the user tested the firmware on the device as a whole (full image sha256 `103fc22d…`) and
  reported that it passed.
- Measurements not recorded item by item: the real CPU load of synthesis and mixing (render time is
  logged every 30 s while playing), stability and heat over a full night of playback, key power-on
  reliability for each key after auto power-off (the OK key has the smallest wake margin), and the
  battery thresholds.

## License

Code is under the repository [MIT License](LICENSE). The font subsets come from Source Han Sans (SIL
Open Font License 1.1); the source and conversion commands are recorded in
[`assets/fonts/as_fonts.manifest.json`](assets/fonts/as_fonts.manifest.json). All soundscapes are
synthesized; no recordings are included.
