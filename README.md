# Sova

Sova («Сова») is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), the open-source e-reader firmware. It targets two Xteink readers: the X4 Pro (ESP32-S3, touch, frontlight) and the plain X4 (ESP32-C3, buttons only); the X4 build also carries the X3 board profile. Upstream features are listed in the [CrossPoint README](https://github.com/crosspoint-reader/crosspoint-reader#what-can-crosspoint-do). This file covers what the fork adds. For using the device, see the [user guide](./USER_GUIDE.md).

## What Sova adds

Reading:

- Reader footer with the page number in the whole book and the reading time left. Pages not laid out yet are estimated and marked with `≈`; reading speed is learned from your page turns.
- FB2 and FB2.ZIP books, converted once into an EPUB inside the book's cache.
- Reading statistics on a Home tab: today's minutes against a daily goal, streak and record, a 12-week calendar, books finished and hours read this year.
- Library tabs by status (Reading, Finished, Paused) and shelves you create, rename and delete.
- A right-edge swipe sets the frontlight brightness while reading (X4 Pro, Edge Swipe Brightness setting).

Games, under More → Games, with touch on the X4 Pro and buttons on the X4, saved to the SD card:

- Chess against the computer (levels 1 to 5) or a friend, with clocks and four piece sets.
- Sudoku, Minesweeper and Klondike solitaire.
- Scanwords. Puzzles are files in `/scanwords/` on the card, made with `tools/scanword` (samples in `tools/scanword/samples`).
- Tic-tac-toe against the computer or a friend, 2048, the 15 puzzle.
- Hangman and 5 letters (a Russian Wordle) on built-in word lists; hangman has a separate mode with crude words.
- More games are in progress on separate branches.
- The X4 build leaves out the keyboard games (scanwords, hangman, 5 letters, codewords): they take too many key presses.

Books and screens:

- Receive books over LocalSend: More → Receive books shows the device as «Сова» with a 4-digit code, and you accept or decline each transfer. The receiver runs only while that screen is open.
- An owl on the boot screen and on the built-in Dark and Light sleep screens.
- Sleep image picker: Settings → Display → Sleep image shows a thumbnail grid of the BMPs in `/sleep` and `/.sleep`, with a full-screen preview.
- Setka, a 1-bit UI style for the screens the fork adds, drawn in Unbounded on both devices. The rules are in [.claude/skills/ui-style/SKILL.md](./.claude/skills/ui-style/SKILL.md).

Battery:

- Light sleep between inputs on the X4 Pro: when idle, the chip sleeps until a button, a touch or the next timer wakes it, instead of polling every 10 ms.

Safety:

- OTA rollback guard on the X4 Pro: a new firmware is confirmed only after about 10 seconds of running, or when the device goes to sleep, so a build that crashes during start-up returns to the previous one.
- Boot rescue: hold Left + Right on the X4 Pro, or Back + Side Down on the X4 and X3, to restart into the previous firmware. Three crashes in a row during start-up do the same on their own.

## Install

1. Copy the firmware to the SD card: `.pio/build/x4pro-gh_release/firmware.bin` for the X4 Pro, `.pio/build/gh_release/firmware.bin` for the X4 and X3 (see [Build](#build)). The picker lists any file ending in `.bin`, so you can rename it; a file built for another device is refused.
2. On the device, open Settings → System → SD Card Firmware Update, pick the file and confirm.
3. After the first boot, leave the device on for about 10 seconds or let it go to sleep before you press Reset. Until then the new firmware is not confirmed, and on the X4 Pro a reset returns to the previous one.

The SD path needs CrossPoint or Sova on the device already. Otherwise flash over USB with the web flasher's Custom .bin option or with `esptool`, as described under [Install firmware](https://github.com/crosspoint-reader/crosspoint-reader#install-firmware) in the CrossPoint README.

## Rescue

- X4 Pro: [Going back to the previous firmware (X4 Pro)](./USER_GUIDE.md#going-back-to-the-previous-firmware-x4-pro).
- X4 and X3: [Going back to the previous firmware (X4, X3)](./USER_GUIDE.md#going-back-to-the-previous-firmware-x4-x3).

## Build

Clone with submodules (`git submodule update --init --recursive` if you cloned without them) and install pioarduino core 6.1.19 into `.venv` as described in [Getting started](./docs/contributing/getting-started.md).

X4 Pro:

```sh
pio run -e x4pro-gh_release
```

X4 and X3 use `gh_release`. Like every env with `custom_sdkconfig`, it builds a nested core environment, and that environment pulls pioarduino 6.2.x unless you pin it. 6.2.x installs SCons 4.11.1, the platform then deletes `tool-scons` in the middle of the build, and the link fails with `No module named 'SCons.Tool.FortranCommon'`. Build it in its own PlatformIO core directory, with the nested core pinned to 6.1.19:

```sh
export PLATFORMIO_CORE_DIR="$HOME/.platformio-c3"
python -m pip install uv
pio pkg install -e gh_release
uv pip install --python "$PLATFORMIO_CORE_DIR/penv/bin/python" 'pioarduino==6.1.19'
pio run -e gh_release
```

The separate core directory also keeps the C3 build from deleting the `tool-scons` package that X4 Pro builds in the default core use.

Host tests (GoogleTest through CMake and CTest, needs `cmake`):

```sh
pio run -t unit-tests
```

Simulator: the `simulator` and `simulator_x4_pro` envs are not in the repo. They go into an untracked `platformio.local.ini`, copied from crosspoint-simulator's `sample-platformio-macos.ini`. The ui-style skill has the commands for scripted taps and screenshots.

More developer docs: [contributing](./docs/contributing/README.md), [touch and UI development](./docs/contributing/touch-and-ui.md), [web server](./docs/webserver.md), [file formats](./docs/file-formats.md).

## Credits and licences

- [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) and its contributors, MIT. Sova is built on their work; see [crosspointreader.com](https://crosspointreader.com), and you can [fund CrossPoint contributors](https://app.royalty.dev/crosspoint-reader/crosspoint-reader).
- [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) (the `freeink-sdk` submodule), MIT.
- Lucide icons, ISC, shipped in the FreeInk SDK.
- Chess pieces: Lucide, Tabler Icons (MIT), Material Design Icons by Pictogrammers (Apache 2.0) and Font Awesome Free (CC BY 4.0). Licence files are in `src/games/chess/ui/pieces/`.
- Code adapted from guloff/folio-firmware (MIT): the FB2 converter, the sleep image picker, the reading session model and the OTA rollback guard.
- LocalSend HTTP code ported from [bigbag/papyrix-reader](https://github.com/bigbag/papyrix-reader), MIT.
- Fonts in `lib/EpdFont/builtinFonts/source/`: Unbounded, Golos Text, Noto Sans, Noto Serif, Noto Sans Arabic, Noto Sans Hebrew and OpenDyslexic under the SIL Open Font License 1.1; Ubuntu under the Ubuntu Font Licence 1.0.
- Scanword clues and answers are derived from the Russian Wiktionary (CC BY-SA 4.0), ranked with wordfreq (CC BY-SA 4.0) and pymorphy3 / OpenCorpora data. The puzzles are shared under CC BY-SA 4.0; see `tools/scanword/samples/ATTRIBUTION.txt`. The hangman and 5 letters word lists come from the same sources; see `tools/hangman/ATTRIBUTION.txt` and `tools/five-letters/ATTRIBUTION.txt`.

The firmware is MIT-licensed; see [LICENSE](./LICENSE).

Sova and CrossPoint Reader are not affiliated with Xteink or any device manufacturer.
