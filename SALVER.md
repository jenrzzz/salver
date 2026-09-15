# salver — the morning paper, on a tray

A thin fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
that turns an Xteink X3 into a **subscriber to a newspaper**. Every morning,
while the reader sleeps, a timer wakes it; it joins home Wi-Fi, pulls one EPUB
(today's curated edition, built by [feedcurator](../feedcurator)) and one BMP
(the front page, which becomes the sleep screen), and goes back to sleep. Press
a button and the edition is already open.

The whole fork is a few hundred lines on top of upstream `1.6.0`, and it is
meant to stay that way so it can be rebased on each upstream release.

## The dogmatic rule

**The device never parses a feed or a web page.** It downloads exactly two
files with `If-None-Match`, and everything else — fetching bodies, readability,
typesetting, scheduling — happens server-side in feedcurator. That is what keeps
the diff small, keeps 380 KB of RAM out of trouble, and keeps the fork
rebasable. The seductive anti-feature (an on-device feeds app) is not built.

## What changed vs upstream

| File | Change |
|---|---|
| `src/salver/Salver.{h,cpp}` | The paper round: Wi-Fi → two conditional GETs → SD → state → next wake |
| `src/salver/SalverStore.{h,cpp}` | `/salver.json` config and `/.crosspoint/salver.json` state (ArduinoJson `PersistableStore`s) |
| `src/salver/SalverUtil.{h,cpp}` | Pure helpers, host-tested in `test/salver` |
| `src/main.cpp` | Boot hook: a timer wake runs the paper round before the display is touched; every deep-sleep path arms the timer |
| `src/activities/settings/SalverSyncActivity.{h,cpp}`, `SettingsActivity.{h,cpp}` | Settings → System → Fetch Salver now, with an on-screen result |
| `lib/hal/HalPowerManager.{h,cpp}` | `startDeepSleep(gpio, timerWakeSeconds)`: arms `esp_sleep_enable_timer_wakeup`; on the X4 keeps the GPIO13 battery latch HIGH |
| `lib/hal/HalClock.{h,cpp}` | `getEpoch()` / `setEpoch()` so the server's clock seeds the RTC |
| `test/CMakeLists.txt`, `test/salver/` | Host unit tests |

Without `/salver.json` on the card, automatic delivery is disabled and the
manual fetch screen explains how to configure it.

## Setup

1. Flash this build (`pio run -e default -t upload`, or the release `.bin` the
   same way you would flash CrossPoint). Confirm the unit is not firmware-locked
   first — flash stock CrossPoint before anything else.
2. Join home Wi-Fi once through CrossPoint's normal Wi-Fi screen so the
   credentials are saved on the card.
3. Put `salver.json` on the root of the SD card:

   ```json
   { "url": "https://salver.jfave.com" }
   ```

   Optional keys and their defaults: `"editions_dir": "/Editions"`,
   `"keep_days": 7`, `"retry_seconds": 1800`, `"fallback_seconds": 3600`,
   `"set_sleep_screen": true`.
4. Open **Settings → System → Fetch Salver now** to fetch immediately. The
   screen reports a download, an unchanged edition, an edition not published
   yet, or a failure. It reloads `/salver.json`, so configuration copied to
   the card since boot is picked up. Dismiss the result with Back (or tap on
   touch devices); C3 devices restart to Home to reclaim Wi-Fi memory. Open
   the edition from Recent books.
5. Put the reader to sleep for automatic delivery. With no known next-wake
   time, the timer is armed for `fallback_seconds` (one hour by default)
   **from entry into deep sleep**. This is not an hourly poll while awake.
   Once a pull establishes the clock and schedule, the server sets the next
   wake; a successful pull without a scheduling header defaults to 24 hours.

### Verify a manual fetch

- With saved Wi-Fi and a valid `/salver.json`, select **Fetch Salver now**.
  Check `/Editions/<date>.epub` (or your configured directory) and `/sleep.bmp`.
  Repeat with an unchanged server edition and expect “Edition already up to date”.
- Check `/.crosspoint/salver.json` for `last_edition`, `next_wake_epoch`, and
  `failures`. A failed fetch normally retries after 30 minutes; repeated
  failures back off. The screen distinguishes a server response saying an
  edition has not been published yet from a failed download.
- With Wi-Fi unavailable, expect a failure result; without configuration,
  expect a configuration hint. Verify the screen in all four orientations.
- In a debug build, monitor `SALV` logs for manual-fetch start/end heap values
  and HTTP results. Verify free heap stays above 50 KB and repeated fetches
  followed by dismissal do not leak memory. Device validation is required,
  particularly for TLS with the display and Settings loaded.

The pull goes over TLS (CrossPoint's wolfSSL client, no certificate pinning);
the timer-wake path has the heap to spare because no display, fonts or
activities are loaded. Plain `http://` URLs work too. The firmware does not
follow redirects, so the URL must be the final one.

## How a morning goes

```
05:30  feedcurator refresh (cron)          05:55  X3 timer wake
05:45  edition + front page published      05:55  connect Wi-Fi (saved creds, last network first)
                                            05:55  GET /editions/latest.epub  If-None-Match: <etag>
                                                   → 200: stream to /Editions/.salver.tmp, rename to /Editions/2026-09-14.epub,
                                                          set as the book to resume, add to Recent
                                                   → 304: nothing to do
                                            05:55  GET /editions/latest.bmp   If-None-Match: <etag>
                                                   → 200: becomes /sleep.bmp; sleep screen mode set to Custom
                                            05:56  if the front page changed: init display, paint the sleep screen once
                                            05:56  arm timer for X-Salver-Next-Wake-In seconds, deep sleep
```

About twenty to thirty seconds awake, one full panel refresh at most, and the
lock screen is today's headlines.

### Scheduling is the server's job

The C3's deep-sleep timer runs off an RC oscillator and can drift by minutes a
day, and the device knows no timezone. So every response from feedcurator
carries:

- `X-Salver-Now` — the server's clock (epoch seconds). Sets the ESP32 system
  clock (which keeps running through deep sleep) and the X3's RTC.
- `X-Salver-Next-Wake-In` — seconds until the reader should pull again:
  tomorrow's wake time if today's edition is out, a short nap if the reader
  arrived early or the edition is late.
- `X-Salver-Edition`, `X-Salver-Title` — the file name and the Recent-books title.

The device stores `next_wake_epoch` and, on *every* deep-sleep entry (button,
timeout, or after the paper round), arms the timer for `next_wake_epoch - now`,
clamped to [1 min, 26 h]. Drift never accumulates past one day. Failures retry
after `retry_seconds`, backing off ×8 after four in a row.

### Power

On the X3, GPIO13 gates only the SD rail, so the stock sleep path already
leaves the ESP32 in real deep sleep and a timer wake just works. On the X4 the
same pin is the battery latch and upstream drives it LOW (a full power-off); when
a timer is armed, salver holds it HIGH instead. **Sleep current on the X4 with
the latch held has not been measured** — expect tens to hundreds of µA rather
than ~10 µA. Untested on X4 hardware.

## Building

```bash
git submodule update --init --recursive
pio run -e default            # X3/X4 share one C3 binary
pio run -e default -t upload
cmake -S test -B build/test && cmake --build build/test --target SalverUtilTest && ./build/test/salver/SalverUtilTest
```

Rebasing on upstream: `git fetch upstream && git rebase <tag>`. The touch
points are listed above; the salver code itself lives in its own directory.

## Sequencing

- **v0 (done, zero firmware)**: feedcurator builds the edition and serves
  `/opds` at `https://salver.jfave.com/opds`. Stock CrossPoint's OPDS browser
  pointed at it works today.
- **v1 (this fork)**: timer wake + silent pull + sleep-screen front page,
  plus a manual fetch and result in Settings.
- **v2**: web-settings UI for `/salver.json`; migrate to upstream's SD-plugin
  mechanism when it ships so the fork can die.
- **v3**: the return post — KOSync-derived "what got read" back into feedcurator,
  and one "keep this" button mapping.
- **Never**: on-device feed parsing, browsing, or a feeds app.

## Open questions

- X3 battery capacity (350 or 650 mAh?) — bounds how many failed wakes a flaky
  network can cost per day.
- X4 sleep current with GPIO13 held HIGH.
- How large an edition can be before CrossPoint's chapter cache makes the first
  open sluggish on a C3 (feedcurator caps at 15 clusters × 3 articles × 40 KB).
- Whether the paper round fits upstream's planned "whitelisted job queue"
  plugin model, or salver stays a permanent thin fork.

Provenance: sparked 2026-09-13 in `launchpad/salver`; the original notes are in
`docs/salver/NOTES.md`.
