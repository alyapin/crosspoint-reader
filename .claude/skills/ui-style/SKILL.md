---
name: ui-style
description: Use when designing, mocking up, or changing anything a user sees in this firmware fork (CrossPoint on Xteink X4 Pro) — new screens or activities, home screen, games, statistics, menus, icons, boot or sleep visuals, layout or font tweaks. Not for non-UI work (parsers, engines, sync, OTA).
---

# UI style "Setka" (Xteink X4 Pro fork)

Quiet 1-bit screen, one loud number. Strict left-aligned grid, black rules, the selected item marked by a black bar on the left.

## Scope

Applies only to UI changes. Non-UI changes skip the mockup steps but still need codex review before any `.bin` reaches the device.

## Workflow for every UI change

1. Mockup as a picture (inline widget or HTML built from `mockup-kit.html` in this folder). Never describe UI only in words.
2. Aleksei approves the picture.
3. Code.
4. Simulator screenshot of the real screen, shown next to the mockup (commands below).
5. Codex review (`codex-code-review` skill), fix, repeat until APPROVED.
6. Build `pio run -e x4pro-gh_release` (never the dev `x4pro` env) and hand over the `.bin`.

## Device constraints

| Fact | Consequence |
|---|---|
| 480×800 portrait, UI drawing is 1-bit | Design in pure black and white |
| Gray exists only as dither: LightGray 25%, DarkGray 50% (`GfxRenderer::fillRectDither`) | Gray only for fills, tracks, cover placeholders. Never gray or dithered text |
| No Back/Confirm buttons; physical Left/Right/Power + capacitive Home | Every action is an on-screen touch target; Left/Right only move focus or turn pages |
| `minTouchSize = 44`, list rows ≥ 56 px on touch | Size targets accordingly |
| System gestures: left 25% back-swipe, top/bottom 14% swipes | Keep swipe-sensitive controls out of those zones; taps there are fine |
| Hit rects within 12 px of the edge snap to the bezel | Don't place small targets at the very edge |
| Refresh: FAST default, HALF/FULL for full-frame changes | First paint of a new screen = HALF; in-screen updates FAST, small regions only (clocks) |
| App slot ~1.1 MB free (build 81.5% of 6.25 MB) | Every font size costs ~110 KB per Regular+Bold pair; prefer digits-only hero fonts |

## Tokens

- **Hero digits:** Unbounded SemiBold, digits-only face (0-9 % : . − space), 72–120 px for the screen's one big number, 40–56 px for a secondary number. ~20 KB.
- **Text:** Golos Text Regular/SemiBold at 10 and 12 pt (to be added as built-in faces; until then fall back to `UI_10_FONT_ID` / `UI_12_FONT_ID` Ubuntu). Titles 12 pt SemiBold, body 10–12 pt, captions 8 pt (`SMALL_FONT_ID`).
- **One hero per screen.** Everything else is small and quiet. Content-first screens (game boards, covers) have no hero number: the content is the hero. Unbounded is only for that one hero number; digits inside grids, cells and lists use the text font (12 pt SemiBold for fixed values, Regular for user input).
- **Selected / active in lists and tabs:** 10 px black bar on the left + SemiBold label. In grids and boards (cells, squares, keys): 4 px black inset frame. No pills, no inverted blocks, no shadows.
- **Unavailable actions:** hide them. There is no gray text to show a disabled state.
- **Text never sits on a dither fill.** Dither only behind empty areas, tracks and cover placeholders.
- **Rules:** 2 px black lines between sections; no boxes around content.
- **Grid:** left-aligned, 20 px side padding (Lyra `contentSidePadding`), full-bleed content (boards, covers) ignores the padding.
- **Progress:** 12 px bar, black fill on a LightGray-dithered track with a 1 px black outline.
- **Icons:** Lucide via the 1-bpp icon pipeline (`freeink-sdk/libs/assets/Icons`), 24 px in rows, 32 px in tabs.
- **Copy:** Russian first (`lib/I18n/translations/russian.yaml`, key also in `english.yaml`), sentence case, short, verb on buttons that perform an action ("Отменить ход", not "Отмена"). Toggles name the mode as a noun ("Заметки").
- **Refresh in long-lived screens** (boards, timers): HALF on entry, FAST on the changed regions only, plus one HALF every 20 FAST updates to clear ghosting.

## Navigation

- Home = reading only. Bottom tabs: Библиотека, Статистика, Ещё, Настройки.
- Games, file transfer, OPDS catalogs, plugins, SD files live under **Ещё**. Games never appear on Home.
- Path to a game: Ещё → Игры (list) → game setup (list: mode, difficulty, time) → game screen. Every screen has an on-screen exit ("‹ Выйти" or "‹ Назад") at the top left.
- System screens inherited from upstream (settings, library, reader) keep the Lyra theme; only screens this fork adds or redesigns use Setka.

## Home screen (approved layout "3в")

```
21:40                          82%
Читаю сейчас              3 книги
[ cover ] [cover] [cover]          <- current one larger, others smaller and lower
  64%       12%     3%             <- hero digits under each
──────────────────────────────────
▌Мастер и Маргарита                <- current book, left bar, tap = continue reading
▌глава 14, ещё 1 ч 20 мин
          ...
──────────────────────────────────
Библиотека | Статистика | Ещё | Настройки
```

Reference picture: `mockup-kit.html`.

## Code mapping

- Hosting: one list → `UiListActivity`; custom layout → `UiAppHost` (see `docs/contributing/touch-and-ui.md`). Board-game shell template: `src/activities/reader/EpubReaderPercentSelectionActivity.{h,cpp}`.
- Put Setka constants (bar width, rule width, paddings, font ids) in one header next to the first screen that needs them and reuse it; don't scatter literals.
- Never call `setTheme`; `clearTapFlash()` before leaving a screen.

## Simulator screenshots

Simulator envs live in the untracked `platformio.local.ini` (`[env:simulator]`, `[env:simulator_x4_pro]` from crosspoint-simulator's `sample-platformio-macos.ini`).

```bash
.venv/bin/pio run -e simulator_x4_pro
CROSSPOINT_SIM_SD=/tmp/sim-sd \
CROSSPOINT_SIM_INPUT_SCRIPT='3200:TAP:240,560;6000:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS='2900:/tmp/shots/home.bmp;5000:/tmp/shots/next.bmp' \
  .pio/build/simulator_x4_pro/program
sips -s format png --resampleWidth 480 /tmp/shots/home.bmp --out /tmp/shots/home.png
```

Use a fresh SD folder per run (the firmware reopens the last book). Taps are in logical 480×800 coordinates.

Close the simulator after every run: end the input script with `QUIT`, run it behind a guard that kills the process after ~40 s, and confirm `pgrep -fl simulator_x4_pro/program` is empty. Never leave windows open on Aleksei's screen.

## Common mistakes

- Gray or dithered text, thin gray rules: invisible or noisy on the panel. Use black.
- Capsules, inverted rows, rounded cards for selection: that's Lyra/other forks, not Setka.
- Button-only navigation or missing on-screen confirm: X4 Pro has no Back/Confirm keys.
- Mockups in fonts or grays the device can't render: mock with the tokens above and the dither patterns from the kit.
- Shipping UI that Aleksei saw only as text.
