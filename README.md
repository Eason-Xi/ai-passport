<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Limited Rock-Paper-Scissors — FoloToy AI Passport

A multi-device party game for the FoloToy AI Passport, based on the "Restricted
Rock-Paper-Scissors" gamble from the manga *Kaiji*. Every player holds their own AI Passport and
plays their cards in secret on it; one more AI Passport acts as the **host** (dealer and referee).
The host is plugged into a computer, where a live dashboard shows every player's remaining rock,
scissors and paper cards, their stars, and the hidden card each duelist has already put down —
for the organizer and the audience only, never for the players.

<p align="center">
  <img src="assets/images/kj-preview.png" alt="Player screens: role selection, hand, incoming challenge, card locked, reveal, cleared" width="100%">
</p>

<p align="center">
  <img src="assets/images/kj-board-preview.png" alt="Computer dashboard with the ship board totals, one card per player and the duel log" width="80%">
</p>

> The device screens are rendered on a computer by `tools/render_kj_preview.py` with the real UI
> code and fonts, and the dashboard screenshot is fed with real firmware board output. Neither is
> a photo of the hardware.

## Rules (as in the original)

- Every player starts with **4 rock, 4 scissors and 4 paper cards** and **3 stars**.
- Two players agree to a duel: each puts down one card face-down, then both are revealed at the
  same time. Rock beats scissors, scissors beat paper, paper beats rock. The winner takes **one
  star** from the loser; a draw moves no stars. Played cards are discarded.
- A player whose stars reach **0 is out** immediately.
- A player who has played **all 12 cards with at least 3 stars clears** the game; all cards
  played with fewer than 3 stars is a **failure**.
- When the host ends the game (the original's time limit), everyone who still holds cards fails.

## What you need

| Item | Notes |
| --- | --- |
| One AI Passport per player | Any number of players; one host supports up to 128 seats. |
| One extra AI Passport as the host | Same firmware. It holds the only authoritative game state. |
| A computer with Chrome or Edge | Shows the dashboard over the host's USB cable (Web Serial). Optional: the game runs without it. |

With only two boards you can still play: one host plus one player, and add **computer players**
on the host for the player to duel against.

## Quick start

1. Flash `build/FoloToy-AI-Passport-full.bin` to every device (see [Build and flash](#build-and-flash)).
2. On the host device choose **host** on the first screen. Connect it to the computer by USB and
   open `tools/kj_board/index.html` in Chrome or Edge, click **Connect host device** and pick the
   device's serial port.
3. On every player device choose **player**, pick the host's game (its 4-character code is shown
   on the host) and press **OK** to take a seat. Each player gets a seat number such as `07`.
4. Start the game from the dashboard or the host's own menu. Players find an opponent, duel, and
   watch their stars; the dashboard follows every card in real time.

The role chosen last time is preselected after a restart, and a player device automatically
rejoins its previous game if that host is still nearby.

## Device screens and buttons

UP / DOWN act on press; OK acts on click; a long OK press (0.5 s) goes back, withdraws or declines.
Every screen shows its own hint line at the bottom, and the battery level sits in the top-right
corner (`--` when the fuel gauge cannot be read).

| Screen | UP / DOWN | OK | Long OK |
| --- | --- | --- | --- |
| Role selection | Switch player / host | Confirm | — |
| Find a game | Choose among nearby hosts (strongest signal first) | Take a seat | Back to role selection |
| Seated | — | — | Leave the seat (it is kept on the host) |
| Hand | — | Find an opponent | — |
| Choose opponent | Move through challengeable players | Challenge | Back to the hand |
| Waiting for an answer | — | — | Withdraw the challenge |
| Incoming challenge | — | Accept | Decline |
| Play a card | Move between the cards still in hand | Put the card down (final) | Abandon the duel before putting a card down |
| Reveal | — | Continue | — |
| Cleared / out / failed | — | — | — |
| Host panel | Move through the menu | Run the item (ending, new game and clearing ask for a second OK) | Cancel a confirmation |

- **Choose opponent** lists only players who are idle and online right now. Players whose
  heartbeat this device hears are sorted by signal strength, so the person in front of you is
  normally first; computer players and players out of earshot follow.
- An unanswered challenge expires after 20 s. A duel is cancelled without using any card if one
  side disappears for 15 s.
- The host screen shows only totals (seats, online, duels, players who have left) and never shows
  anyone's cards, so it can stand in the open.
- Cues: an alert for an incoming challenge, tones for the duel start and putting a card down, and
  different jingles for win, loss, draw, clearing and being out. The screen dims after 60 s
  without activity; the first key press only wakes it, and incoming events wake it too.

## The computer dashboard

`tools/kj_board/index.html` is a single offline file: it loads nothing from the internet and sends
nothing anywhere. Open it in Chrome or Edge on the computer the host is plugged into.

- **Ship board**: total rock / scissors / paper still in play, total stars, and how many players
  are still in, cleared, or out.
- **One card per player**: seat number, an editable name (stored only in this browser), status,
  online state and signal, stars, the three card counts with pips, record, and — during a duel —
  the opponent and the hidden card already put down (the **Show hidden cards** switch hides it).
- **Controls**: start, end (asks first), new game, add or remove a computer player, clear all
  seats, remove a single player, and an optional time limit in minutes that ends the game
  automatically.
- **Duel log** with every challenge, accept, card put down, result, clear and elimination.
- Press **H** (or the cover button) to black out the dashboard instantly if a player walks by.
- **Demo** previews the dashboard with built-in simulated data without any device.

## How it works

```text
player devices ──ESP-NOW (channel 1)──► host device ──USB Serial/JTAG──► dashboard (Web Serial)
    ▲   requests, heartbeats             │ rules engine, bots,             "@KJ {json}" lines
    └──────── per-player views ◄─────────┘ NVS snapshot                    "@KJ start" commands
```

- **One firmware, two roles.** The host runs the rules engine (`main/kj_rules.c`) and is the only
  source of truth. Players only receive a view of their own seat; an opponent's card is never sent
  to anyone before both cards are down.
- **Radio.** ESP-NOW over Wi-Fi in station mode on a fixed channel, with no router and no
  provisioning. The host broadcasts a beacon every second carrying the game code, phase and a
  128-bit bitmap of challengeable seats. Players broadcast a heartbeat every 1.5 s, which the host
  uses for presence and nearby players use for signal-strength sorting.
- **Reliability is in the application** (`main/kj_server.c`, `main/kj_client.c`): requests carry
  a sequence number and are retried every 250 ms until the view acknowledges them; the host
  deduplicates them and resends a view until the heartbeat confirms its version. A seeded
  simulation with 25 % packet loss, player restarts and a host restart plays complete games in
  the host tests.
- **Persistence.** The host saves a compact snapshot (seats, device IDs, cards, stars, record) to
  NVS after changes. After a power loss it restores the game; duels that were in progress are
  cancelled and their hidden cards stay in hand.
- **Dashboard protocol** (`main/kj_board.h`): one JSON object per line with the `@KJ ` prefix, so
  ordinary ESP-IDF log lines on the same port are ignored.

## Build and flash

```bash
source <path-to-esp-idf-v5.5.3>/export.sh
./tools/validate.sh            # host tests + firmware build + merged-image verification
```

Flash the verified merged image from offset `0x0` (this may reset stored data, including a saved
host game):

```bash
python -m esptool --chip esp32c3 -p <port> -b 460800 write-flash 0x0 build/FoloToy-AI-Passport-full.bin
```

Development helpers:

| Command | Purpose |
| --- | --- |
| `python3 tools/render_kj_preview.py` | Render every device screen to `build/kj_preview/shots/` with the real UI code and report the LVGL pool peak. Needs `managed_components/` from one firmware build. |
| `python3 tools/gen_kj_fonts.py check` | Verify that the committed font subsets cover every UI string (run by `validate.sh`). |
| `python3 tools/gen_kj_fonts.py generate ...` | Regenerate the fonts after changing `main/kj_strings.h`; see [assets](assets/README.md#fonts). |

## Limits and open checks

- One host serves up to 128 seats. Radio range, airtime with many devices, and coexistence with
  busy 2.4 GHz Wi-Fi on channel 1 have not been measured on hardware.
- The game code is derived from the host's MAC address; two hosts nearby are told apart by that
  code and their signal strength.
- Device names are not entered on the devices (three buttons); seat numbers are shown on the
  devices and the dashboard can attach names locally.
- Choosing the host role is final until the device restarts.
- The dashboard needs a browser with Web Serial (Chrome or Edge on a computer).

## Files

| Path | Contents |
| --- | --- |
| `main/kj_rules.*` | Rules engine: seats, dealing, duels, stars, clearing, timeouts |
| `main/kj_proto.*` | ESP-NOW frame format |
| `main/kj_server.*` / `main/kj_client.*` | Host and player protocol logic, computer players |
| `main/kj_flow.*` / `main/kj_model.*` | Screen state machine and UI model |
| `main/kj_ui*.c` / `main/kj_strings.h` / `main/kj_fonts.*` | LVGL screens, all UI text, fonts and glyph self-check |
| `main/kj_board.*` | Host-to-dashboard serial protocol |
| `main/kj_persist.*` / `main/kj_store.*` | Snapshot format and NVS storage |
| `main/kj_radio.*` / `main/kj_sound.*` / `main/main.c` | ESP-NOW driver glue, synthesized cues, application task |
| `tools/kj_board/index.html` | Computer dashboard |
| `tests/test_kj_*.c`, `tests/test_kj_contract.py` | Host tests, including the multi-device loss simulation |
