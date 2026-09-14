# salver — the morning briefing, on a tray (working name)

Sparked 2026-09-13 from a live itch: a new Xteink X3 that won't get used for
books.

## The itch (user's words, near-verbatim)

> I just got an Xteink X3 e-ink reader and think it's a cool device but don't
> foresee myself reading books on it. Could we make it an interface to my
> curated RSS feeds and sync my briefing every morning? Maybe fork CrossPoint
> Reader or build our own — it's just an ESP32 in there, right?

## What the hardware actually is (surveyed 2026-09-13)

- **ESP32-C3**, single-core RISC-V @ 160 MHz, **400 KB SRAM (~380 KB usable),
  no PSRAM**, 16 MB flash, microSD (FAT32), 2.4 GHz Wi-Fi, BLE 5. Deep sleep
  ~10 µA. Battery reported as 350 or 650 mAh depending on source — measure.
- 3.7" mono e-ink, **528×792 @ 259 ppi**, no frontlight. Full refresh ~1.6 s.
- Physical buttons + power; gyro page-turn.
- Stock firmware is closed. Some units ship *firmware-locked*; pocketink.io's
  FAQ covers locked units and brick recovery. **Confirm this unit flashes
  before anything else.**

So: "just an ESP32" is right, but it's the *small* ESP32. 380 KB of RAM is the
whole design constraint. This is a device that renders EPUBs beautifully and
must not be asked to think.

## CrossPoint, surveyed (v1.6.0, master @ 2026-09-14)

MIT, PlatformIO/Arduino, `esp32-c3-devkitm-1` env for X3/X4. Already has:
Wi-Fi STA + AP, saved networks, HTTP file server + WebDAV (only while in
File Transfer mode, no auth), **OPDS browser** (8 saved servers, basic auth),
Calibre wireless, **KOSync client** (progress sync), OTA from GitHub releases,
sleep screens from `/sleep.bmp`, deep sleep via `enterDeepSleep()` →
`powerManager.startDeepSleep(gpio)` with GPIO wake only.

Their SCOPE.md is unambiguous: *"Active Connectivity: No RSS readers, news
aggregators, or web browsers"* — out of scope, forever. PR #2274 (a full
on-device RSS reader with on-device Readability) was closed on exactly that
line. But their ROADMAP phase 2 has **"SD-loaded plugins … integrations and
connectors, running through the web server and a whitelisted job queue"** as
*the* sanctioned way to add "talk to a server" features. Issue #3525 shows
early plugin hooks in the wild (`sleep.enter` with `connect: true`: connect
saved Wi-Fi on sleep, download a fresh `/sleep.bmp`).

### This has been designed three times already — all three wrong for us

| Prior art | Where the thinking happens | Push/pull | Why not |
|---|---|---|---|
| **xteink-crosspoint-websync** | desktop app builds EPUBs from RSS/YouTube | *pushes* over CrossPoint's HTTP file API, Windows Task Scheduler | needs device awake in File Transfer mode; Windows-shaped; no curation |
| **PR #2274** (chrisdiana) | on-device feed parse + Readability | pull, manual | rejected upstream; 380 KB RAM is the wrong place for HTML |
| **crosspoint-reader-apps** fork | on-device RSS/Reddit, cached to SD | pull, manual | same, plus a whole apps hub to carry |

Every one of them either parses HTML on the C3 or pushes into a device that's
asleep. We already have the thing none of them have: **a curator that runs
server-side** (feedcurator, on tabitha, 3-stage Claude pipeline).

## Reframe

The ask is "make the X3 an interface to my feeds." The device is not an
interface to feeds. It is a **subscriber to a newspaper**. One edition per
morning, delivered while you sleep, waiting on the tray when you get up.

Three consequences:

1. **The edition is an EPUB.** CrossPoint's entire strength is rendering
   EPUBs. Feeds → curation → one well-typeset EPUB per day is a *build
   artifact*, produced where the RAM is.
2. **The device pulls.** Wake on a timer at 05:55, join home Wi-Fi, GET one
   file, sleep. ~20 s awake at ~100 mA ≈ 0.5 mAh/day. Push is the wrong
   direction for a thing that's asleep 23.9 h a day.
3. **The firmware diff is tiny.** One wake reason + one fetch activity. Small
   enough to rebase on upstream monthly, and shaped to become an SD plugin
   the day their plugin system lands.

Hence the name: a **salver** is the tray the morning post and paper arrive
on. (And it *salves* — the calm, no-glow, no-doomscroll version of the
morning read.) Domestic, wry, sits next to hob and chatelaine.
gem=404, pypi=404, npm=200 (`@jenner/salver`). Runner-ups: **doorstep**
(gem/pypi 404) and **paperround** (all 404).

## Proposed shape

```
 miniflux ──▶ feedcurator ──▶ [edition builder] ──▶ /editions/2026-09-13.epub
 (feeds)     (curates,          (new: Readability,     /editions/latest.epub  (ETag)
              ranks, blurbs)     EPUB + 1-bit BMP)     /editions/latest.bmp
                                                       /opds  (acquisition feed)
                                        tabitha, private tier, salver.home.amber.place
                                                            ▲
                                             05:55 timer wake │ GET, If-None-Match
                                                            │
                                    Xteink X3 ── CrossPoint fork ── SD:/Editions/*.epub
                                                                    SD:/sleep.bmp (front page)
                                                            │
                                             KOSync progress ▼  (v3: what got read → curator signal)
```

### Pillar 1 — the press (server; belongs *inside* feedcurator)

An **edition builder** as a new feedcurator output. Not a new service: it's
an exporter over data feedcurator already has.

- Input: the day's curated set (top ~20 items, curator's one-line blurbs,
  sections/ranks) + full article text via server-side extraction
  (trafilatura/readability — feedcurator may already fetch bodies; verify).
- Output, tuned for 528×792 mono on a C3:
  - `nav.xhtml` = the front page: headline + curator blurb per item, grouped by
    section. This *is* the briefing.
  - One chapter per article, plain semantic HTML, no CSS tricks. Images
    dropped or downscaled to ≤480 px 1-bit (C3 image decode is slow; ghosting
    hates photos). Hard cap per article (~40 KB) — long reads get a "continue
    on the big screen" line, not the whole piece.
  - `latest.bmp`: 528×792 1-bit front page render → becomes the sleep screen,
    so the *lock screen is the headlines*. This is the delightful bit and it
    costs nothing.
- Serve: `/opds` acquisition feed (newest first; lets stock CrossPoint work
  with zero firmware) + `/editions/latest.{epub,bmp}` with strong ETags.
- Cron: feedcurator runs 05:30 → edition built by 05:45 → device wakes 05:55.

### Pillar 2 — the tray (device; CrossPoint fork, the eventual `salver` repo)

The one feature: **scheduled pull**.

- `enterDeepSleep()`: also `esp_sleep_enable_timer_wakeup(seconds until next
  edition time)`. GPIO wake stays; both can be armed at once on the C3.
- On boot with `ESP_SLEEP_WAKEUP_TIMER`: new `EditionFetchActivity` —
  connect saved STA network (note #3525: saved creds aren't loaded before
  early hooks; do it explicitly), NTP for clock, `GET latest.epub` with
  `If-None-Match`, stream to `/Editions/<date>.epub`, fetch `latest.bmp` →
  `/sleep.bmp`, set it as the book-to-resume, prune editions older than
  `keep_days`, re-enter deep sleep. No display refresh at all unless the
  sleep image changed. Abort-and-sleep on any failure; try again next timer.
- Config: `/salver.json` on SD — `{url, wake_at: "05:55", keep_days: 7}`.
  Editable through CrossPoint's existing web settings page later.
- Wake-up experience: press a button, the edition is already open at the
  front page. Page-turn buttons walk the articles. That's the whole UI.

### Pillar 3 — the return post (v3)

CrossPoint already speaks KOSync. feedcurator can serve a KOSync-compatible
endpoint; chapter-level progress tells it which articles were actually read
→ the first real behavioural signal the curator has ever had. A button
mapping for "keep this" is the only other interaction worth adding.

## The dogmatic rule

**The device never parses a feed or a web page.** It downloads exactly two
files, an EPUB and a BMP, and everything else happens on tabitha. This is
what keeps the firmware diff a few hundred lines, keeps it rebasable on
upstream, and respects 380 KB of RAM. Every fork listed above broke this rule
and paid for it with a permanent fork. The seductive anti-feature is the
"interface": on-device feed browsing, per-item actions, a feeds app. Not
first; possibly not ever — the front page *is* the interface.

## Fleet grounding

- **feedcurator** is the press. ⚠️ Not on this box (only `kat` and
  `launchpad` under `~/src`), so the "briefing" shape is *assumed*: verify
  what it emits today (a page? mail? JSON?) and whether it already fetches
  article bodies. This seed's v1 is most likely a feedcurator PR, not a repo.
- **miniflux** stays the feed store; nothing new there.
- **Deploy**: tabitha, **private tier** (LAN only) — the X3 only ever sees
  home Wi-Fi, so the pull needs no auth and CrossPoint's no-auth web server
  is never used in pull mode. `salver.home.amber.place`, declared in
  `exposure.yaml` per golden path.
- **hob**: not needed. LLM calls already live in feedcurator; if a
  "front-page summary" paragraph is wanted later, it's a hob call.
- **Pattern already proven**: single-file-app discipline (workcycle),
  CLI→HTTP (focusrelay-api). Nothing to extract *from* the fleet here; the
  extraction is feedcurator → a new output format.

## Sequencing (cut lines)

- **v0 (a weekend, zero firmware)**: edition builder in feedcurator + OPDS
  feed on tabitha. Stock CrossPoint, OPDS server pointed at it. Morning
  ritual = wake, Wi-Fi, OPDS, tap today's edition. Proves the *edition* is
  worth reading before touching firmware. Also proves the unit flashes.
- **v1 (razor-thin)**: the CrossPoint fork with timer wake + fetch + sleep
  BMP. Nothing else changes in the fork. Rebase on upstream tags.
- **v2**: sleep-screen front page polish; `keep_days` pruning; web-settings
  UI for `/salver.json`; migrate to upstream's SD-plugin mechanism when it
  ships so the fork can die.
- **v3**: return post — KOSync-derived "read" signal into feedcurator; one
  "keep" button mapping.
- **Never**: on-device feed parsing, browsing, or a feeds app.

## Open questions

- What does feedcurator's daily output look like today, and does it keep
  article bodies? (Decides whether v0 is 50 lines or 500.)
- X3 battery: 350 or 650 mAh? Measure; it bounds how many *failed* wake
  attempts per day are tolerable when Wi-Fi is flaky.
- Does the C3's RTC drift enough across a day to matter? (NTP on every wake
  makes it moot; confirm CrossPoint has no clock assumptions in sleep.)
- Is this unit firmware-locked? Flash CrossPoint stock *first*.
- Edition size budget: how many articles fit before CrossPoint's chapter
  cache (`.crosspoint/` on SD) makes the first open sluggish on a C3?
- Does the timer-wake fetch fit upstream's plugin "whitelisted job queue"
  model when it lands, or will salver stay a permanent thin fork?
