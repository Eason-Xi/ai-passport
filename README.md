<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Limited Rock-Paper-Scissors — FoloToy AI Passport

A multiplayer party game based on "Limited Rock-Paper-Scissors" from *Kaiji: Ultimate Survivor*. Every player
holds an AI Passport and plays cards in secret on it; one more AI Passport acts as the **host** (dealer and
referee). All devices join the venue Wi-Fi and play through a **hub** running on a computer (`tools/kj_hub`) —
no USB cable to the computer is needed. The computer's dashboard shows every player's nickname, remaining rock /
scissors / paper cards, stars, and the cards already locked in during a duel, and records every game for CSV
export. The dashboard is for the host and the audience, not for players.

<p align="center">
  <img src="assets/images/kj-preview.png" alt="Device screens: title, QR nickname registration, hand, bump, bump matched, host roster" width="100%">
</p>

<p align="center">
  <img src="assets/images/kj-board-preview.png" alt="Computer dashboard: table summary, one card per player, game log" width="80%">
</p>

> The device screens are rendered on a computer by `tools/render_kj_preview.py` with the real UI code and fonts;
> the dashboard screenshot is fed with real firmware-format board lines. Neither is a photo of real hardware.

## Rules (as in the original)

- Each player starts with **4 rocks, 4 scissors and 4 papers**, and **3 stars**.
- Two players duel: each locks in one card face down, then both are revealed. Rock beats scissors, scissors beat
  paper, paper beats rock. The winner takes **1 star** from the loser; a draw changes nothing. Played cards are
  discarded.
- A player with **zero stars is out** immediately.
- Playing **all 12 cards with at least 3 stars clears** the game; running out of cards with fewer than 3 stars
  **fails**.
- When the host ends the game (the original's time limit), everyone still holding cards fails.

There are two ways to start a duel:

- **Bump**: two players face each other and **long-press OK** at the same time. The host pairs them by the moment
  each one pressed; both screens show the opponent's nickname and the duel starts after a 3-second countdown. Wrong
  person? Long-press OK during the countdown to cancel.
- **Challenge from the list**: pick an idle player under "Choose opponent"; they have 20 seconds to accept.

## What you need

| Item | Notes |
| --- | --- |
| One AI Passport per player | Any number of players; one host has up to 128 seats. |
| One more AI Passport as the host | Same firmware; it holds the only authoritative game state. |
| A computer | Runs the hub (Python 3.9+ only, nothing to install). Keep it plugged in and stop it from sleeping. |
| A 2.4 GHz Wi-Fi network | A dedicated router is recommended, with the computer on Ethernet. Guest networks, captive-portal networks or networks with "AP isolation" usually do not work; a phone hotspot is only good for a few devices while testing. |
| One phone per player (optional) | Registers a nickname by scanning a QR code; you can also name devices on the dashboard. |

Two devices are enough: one host plus one player, with **computer players** added on the host as sparring
partners.

## Quick start

1. Flash `build/FoloToy-AI-Passport-full.bin` to every device (see [Build and flash](#build-and-flash)).
2. Start the hub on the computer; the browser opens the dashboard:

   ```bash
   python3 tools/kj_hub/hub.py
   ```

   On the first run macOS asks whether Python may accept incoming connections — allow it. On Windows, mark the
   current network as "Private". See the [hub guide](tools/kj_hub/README.md).
3. **Wi-Fi setup** (once per device): on first boot the device opens a hotspot `KJ-XXXX` and shows a QR code.
   Scan it with the phone **camera** to join the hotspot, then pick the venue Wi-Fi and enter its password on the
   page that pops up (or open `192.168.4.1` in the browser). Once the device has actually connected it restarts,
   and from then on it joins that Wi-Fi at boot.
4. On Wi-Fi the device finds the hub by itself; the title screen shows "hub connected" in the top-left corner.
5. On the host device choose "I am the host". The game appears on the dashboard.
6. On each player device choose "I am a player". Without a nickname the device first shows a **registration QR
   code**: scan it with **WeChat** (or the phone camera), enter a nickname (up to 8 Chinese characters or 16
   letters), and the device greets you right away. Then pick the host's game in the list and press **OK** to sit
   down. The phone must be on the same Wi-Fi as the computer to open the registration page.
7. Start the game from the dashboard or the host menu. Players bump or challenge each other, duel and guard their
   stars; the dashboard tracks every card.

After a restart the last role is preselected, and players return to their previous game and seat automatically.

## Device screens and buttons

UP / DOWN act on press; a short OK confirms; a long OK (0.5 s) goes back, withdraws, declines or cancels — except
on the hand screen, where a long OK is the **bump**. Every screen shows its own button hints at the bottom and the
battery level in the top-right corner (`--` when the fuel gauge cannot be read).

| Screen | UP / DOWN | OK | Long OK |
| --- | --- | --- | --- |
| Wi-Fi setup (hotspot QR) | — | — | Skip for now |
| Title | Move between player / host / settings | Select | — |
| Settings | Move between register nickname / redo Wi-Fi / back | Select | Back to title |
| Register nickname (QR) | — | New QR code | Back (players may skip) |
| Find a game | Choose a game | Sit down | Back to title |
| Seated | — | — | Leave (the host keeps the seat) |
| Hand | — | Choose opponent (challenge) | **Bump** |
| Bumping | — | — | — |
| Bump matched (countdown) | — | — | Cancel (wrong person) |
| Choose opponent | Move between players you can challenge | Challenge | Back to hand |
| Waiting for an answer | — | — | Withdraw the challenge |
| Challenged | — | Accept | Decline |
| Play a card | Move between cards you still hold | Lock in this card (final) | Give up before playing |
| Reveal | — | Continue | — |
| Cleared / out / failed | — | — | — |
| Host panel | Move through the menu | Run it (end, new game and reset ask for a second OK) | Cancel the confirmation |
| Host: player list | Scroll | Back to the panel | Back to the panel |

- **Bump**: counted from whoever pressed first, the other player must press within 0.6 s; the host settles 0.9 s
  later. Exactly one partner pairs the two; nobody nearby in time gives "no one bumped"; several people bumping at
  the same moment, too close to tell apart, gives "too many people, try again". Computer players never bump.
- **Choose opponent** lists only players who are idle and online right now — humans first, computer players last —
  with their nicknames.
- An unanswered challenge expires after 20 seconds; if a duelist is offline for 15 seconds the duel is void and no
  cards are spent.
- The host screen shows totals (seated, online, dueling, finished). Its "player list" shows each nickname, status,
  stars and number of cards left, but never which cards, so the host device can be left in plain sight. The footer
  shows whether the dashboard link to the computer is up.
- Settings shows the device's nickname, Wi-Fi, hub address and device ID; "redo Wi-Fi" restarts into Wi-Fi setup.
- The registration and Wi-Fi setup screens stay lit; other screens dim after 60 seconds without input. The first
  key press only wakes the screen, and new events wake it too.

## Hub and dashboard

`tools/kj_hub` is a standard-library-only Python service; see the [hub guide](tools/kj_hub/README.md):

- **Relay**: every device talks only to the computer (UDP), which forwards game frames by device; the host is
  still the only referee.
- **QR nickname registration**: the device shows a QR code and the phone opens the registration page on the
  computer. Nicknames are stored only in the hub's data directory (default `~/kj-hub-data`). Devices render them
  with their own nickname font, and characters they cannot show are rejected at registration time.
- **Dashboard**: open `http://127.0.0.1:47180/board`. Table summary, one card per player (nicknames are editable
  and sync to the devices), start / end / new game / add or remove computer players / reset / kick, an optional
  time limit, game log, one-key cover (H) and a demo. With several games you can switch between them at the top.
  Only the computer running the hub may open the dashboard; other computers or tablets need the token link the
  hub prints at startup.
- **Collection and export**: every game's player states and events are written as JSONL. The hub home page
  `http://127.0.0.1:47180/` exports per-game player results, game events and the nickname registry as CSV (UTF-8
  with BOM, so Excel and WPS show Chinese correctly).

Without the hub, the host can still be connected over USB and `tools/kj_board/index.html` opened directly in
Chrome or Edge ("connect host device (USB)"), but players can only play together through the hub.

## How it works

```text
                                  +------------- hub tools/kj_hub -------------+
players --UDP 47101 envelope+frame-> | relay by MAC, discovery, nicknames,       | <-HTTP 47180- phones (registration)
   ^                                 | records, dashboard                        | --SSE--> browser dashboard
   +------- host views / beacons <---+                                           |
host --UDP (game frames) + TCP 47103 ("@KJ {json}" board lines / "@KJ start" commands)
```

- **One firmware, two roles.** The host runs the rules engine (`main/kj_rules.c`) and is the only source of
  truth. Players receive only the view of their own seat; an opponent's card is never sent to anyone until both
  cards are locked in.
- **Network.** Devices join the venue Wi-Fi as stations. Until they find the hub they broadcast a DISCOVER every
  second (alternating with a unicast to the last known address); the hub answers with an OFFER and also
  broadcasts one every 2 seconds. Game frames (`main/kj_proto.h`, up to 64 bytes) travel inside a 24-byte hub
  envelope (`main/kj_hubproto.h`) through the computer: the host's beacons fan out to every player, while
  players' heartbeats and requests go only to the host.
- **Reliability lives in the application** (`main/kj_server.c`, `main/kj_client.c`): requests carry a sequence
  number and a boot ID and are resent every 250 ms until a view acknowledges them; the host de-duplicates per boot
  ID and resends views until a heartbeat acknowledges the version. Views carry the host's boot ID (epoch), and
  players drop views whose version goes backwards within one epoch (UDP may reorder). Bump requests carry how
  long they have been in flight so the host can recover the moment the button was pressed.
- **Nicknames.** The hub's registry is authoritative. At boot a device reports the nickname it has stored (so a
  lost registry can be restored) and the hub answers with the current one. Other players' nicknames are looked up
  by (game, seat number) and cached. The host's board lines carry each seat's MAC so the hub can match seats to
  nicknames.
- **Wi-Fi setup.** A separate boot mode: the device runs APSTA with a web server and a wildcard DNS (captive
  portal), saves a new Wi-Fi only after actually connecting to it, then restarts into the game so the memory peaks
  of setup and play never overlap.
- **Persistence.** After state changes the host writes a compact snapshot to NVS and restores the game after a
  power loss; bumps, challenges and duels in progress are void and locked cards go back to the hand. Wi-Fi, the
  nickname and the last hub address are kept in NVS too.
- **Tests.** Host tests cover the rules, bump pairing, the game protocol, the hub wire protocol and client, DNS,
  the Wi-Fi form, and two multi-device simulations: direct with 25% loss, and relayed through a hub with 10% loss,
  duplicates and reordering per hop while a player, the hub and the host restart. The hub has its own unit and
  integration tests; the C and Python sides of the wire protocol are compared byte by byte against shared golden
  vectors (`tests/data/kj_hub_vectors.txt`).

## Build and flash

```bash
source <path to ESP-IDF v5.5.3>/export.sh
./tools/validate.sh            # host tests + firmware build + merged-image verification
```

Flashing the verified merged image at `0x0` **erases data saved on the device** (Wi-Fi, nickname, the host's saved
game); the device then needs Wi-Fi setup again:

```bash
python -m esptool --chip esp32c3 -p <port> -b 460800 write-flash 0x0 build/FoloToy-AI-Passport-full.bin
```

To update only the program and keep saved data, write just the application partition (from the archive
`validate.sh` writes to `build/firmware/<sha256>/`):

```bash
python -m esptool --chip esp32c3 -p <port> -b 460800 write-flash 0x10000 build/firmware/<sha256>/FoloToy-AI-Passport.bin
```

While developing you can set `CONFIG_KJ_DEV_WIFI_SSID` / `CONFIG_KJ_DEV_WIFI_PASSWORD` in your local (untracked)
`sdkconfig`; devices that were never set up then join that Wi-Fi directly. The Limited RPS menu in `menuconfig`
also lets you switch Wi-Fi power save to MIN_MODEM (saves battery but adds latency to every packet; measure it
first).

Development helpers:

| Command | Purpose |
| --- | --- |
| `python3 tools/kj_hub/hub.py` | The hub (relay, registration, dashboard, records). |
| `python3 tools/render_kj_preview.py` | Renders every device screen with the real UI code into `build/kj_preview/shots/` and reports the LVGL pool peak; `--sheet` builds the README collage. Needs one firmware build first for `managed_components/`. |
| `python3 tools/gen_kj_fonts.py check` | Checks that the committed fonts cover every UI string and the nickname charset (run by `validate.sh`). |
| `python3 tools/gen_kj_fonts.py generate ...` | Regenerates fonts after editing `main/kj_strings.h` or `tools/kj_charset.py`; see [assets](assets/README.md). |
| `python3 tools/kj_charset.py <name>` | Checks whether a nickname can be shown on the device. |

## Limitations and open checks

- **Not yet verified on real hardware:** latency and stability of Wi-Fi play, airtime and battery life with many
  devices (Wi-Fi stays awake; roughly 4–5 hours estimated), how the setup hotspot's captive portal behaves on
  different phones, opening a LAN page from WeChat's scanner (on iOS WeChat may need "Local Network" permission;
  otherwise scan with the phone camera), and the timing spread of real people bumping.
- The hub is a single point: if the computer sleeps or the hub stops, the whole table pauses; after it restarts the
  devices reconnect automatically, and the host's game lives on the host device, so nothing is lost.
- Devices must be able to reach the computer over a 2.4 GHz Wi-Fi; networks with AP isolation or a captive portal
  do not work. If the router does not forward broadcasts, enter the computer's IP under "Advanced" on the Wi-Fi
  setup page, or start the hub with `--sweep <subnet>`.
- Nicknames may only use characters in the device's nickname font: printable ASCII, the middle dot, the 6763
  GB2312 Chinese characters and a few common name characters. Emoji and traditional characters are not supported.
- This version no longer plays over direct ESP-NOW without a computer, and it cannot be mixed with the previous
  firmware (the hub reports the version mismatch).
- One host has at most 128 seats; after choosing the host role, restart the device to change roles.

## Files

| Path | Contents |
| --- | --- |
| `main/kj_rules.*` / `main/kj_bump.*` | Rules engine (seats, dealing, duels, stars, clearing, timeouts) and bump pairing |
| `main/kj_proto.*` | Game frame format |
| `main/kj_server.*` / `main/kj_client.*` | Host and player protocol logic, computer players |
| `main/kj_hubproto.*` / `main/kj_hubc.*` | Device–hub wire protocol and the device-side hub client (discovery, nicknames, registration) |
| `main/kj_net.*` | Wi-Fi station, UDP relay, host dashboard TCP |
| `main/kj_prov.*` / `main/kj_dns.*` / `main/kj_prov_form.*` / `main/web/kj_prov.html` | SoftAP web Wi-Fi setup |
| `main/kj_flow.*` / `main/kj_model.*` | UI state machine and UI model |
| `main/kj_ui*.c` / `main/kj_strings.h` / `main/kj_fonts.*` / `main/kj_utf8.*` | LVGL screens, every UI string, fonts and glyph self-check, nickname handling |
| `main/kj_board.*` | Host-to-dashboard line protocol |
| `main/kj_persist.*` / `main/kj_store.*` | Snapshot format and NVS storage |
| `main/kj_sound.*` / `main/main.c` | Synthesized sounds, application task |
| `tools/kj_hub/` | The hub (relay, registration, dashboard, records and export) |
| `tools/kj_board/index.html` | Dashboard page (opened through the hub, or standalone over USB serial) |
| `tools/kj_charset.py` | Nickname charset (shared by the device font and the hub's validation) |
| `tests/test_kj_*.c`, `tests/test_kj_*.py`, `tests/data/kj_hub_vectors.txt` | Host tests, simulations, hub tests, wire-protocol golden vectors |
