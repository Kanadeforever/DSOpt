# DSOpt

[中文](README.md) | English

An ASI plugin that fixes compatibility, subtitle scaling, and window-mode issues in Dead Space (2008) on modern Windows systems.

---

## Features

| Feature | Description |
|------|------|
| **Multi-core CPU crash fix** | Limits the number of logical processors available to the game, fixing startup failures on some CPUs with more than 10 cores |
| **Full subtitle resolution scaling** | Fixes subtitle preprocessing, automatic wrapping, two-line capacity, line spacing, and final drawing so subtitle size and layout remain consistent with 720p at high resolutions |
| **Resolution-change synchronization** | Updates subtitle scaling during preprocessing, preventing clipping after switching from a low resolution to 4K |
| **Default settings completion** | Adds windowed mode, disables VSync, and uses the current desktop resolution when the corresponding settings are missing |
| **High-DPI support** | Prefers Per-Monitor V2 DPI awareness to correct window-size and position issues under Windows display scaling |
| **Borderless window mode** | Can be enabled on startup and toggled with F10; automatically skipped while the game is using exclusive fullscreen |
| **Automatic window centering** | Centers the game after startup and resolution changes, including multi-monitor layouts with negative coordinates |
| **Always on top** | Optionally keeps the game window above other windows |
| **Automatic INI recovery** | Recreates a fully commented default `DSOpt.ini` when it is missing without overwriting an existing configuration |
| **Automatic joypad.txt creation** | Creates the file when missing and deletes/recreates it when unreadable, fixing a possible progression block during the Chapter 4 asteroid turret sequence |
| **Optional runtime logging** | Writes diagnostic information about plugin initialization, window management, file handling, and subtitle Hooks |

---

## Subtitle Fix

At resolutions above 720p, the original game does not apply one consistent resolution scale to both subtitle preprocessing and final drawing.

This may cause:

- Subtitles to appear too small relative to the screen;
- Too many characters to fit on one line;
- Wrapping rules to differ from 720p;
- A third subtitle line to appear after increasing font size;
- The third line or later text to be clipped;
- Layout and drawing to use different scales after a resolution change.

DSOpt corrects both stages of the subtitle pipeline:

```text
pre-layout / line capacity / automatic wrapping
                         +
final line metrics / glyph drawing
```

The automatic multiplier is:

```text
actual game render height / BaseHeight
```

The default baseline is the game's development resolution height of `720`.

Frequent resolution changes between `640×480` and `3840×2160` have been tested. Subtitles remain limited to two lines, wrap correctly, and no longer suffer from third-line clipping.

---

## Supported Executables

The following PC releases have been tested:

- Steam;
- GOG.

The plugin uses runtime pattern scanning instead of depending on one fixed executable address.

The original Steam executable contains SteamStub, so the required patterns may not exist in the packed file on disk. DSOpt retries at runtime after SteamStub has unpacked the game code in memory.

When a supported pattern cannot be located, the subtitle module safely stops installation and records the reason when logging is enabled. It will not leave only half of the subtitle Hooks installed.

Other executable variants may work when their relevant code patterns remain compatible, but they are not guaranteed.

---

## Installation

1. Install **[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)** in the game directory, for example as:
   - `dinput8.dll`
   - `winmm.dll`
2. Place `DSOpt.asi` and `DSOpt.ini` together in a directory loaded by the ASI loader.
3. Start the game.

Supported locations include:

```text
game root
scripts/
plugins/
```

Example:

```text
Dead Space/
├── Dead Space.exe
├── dinput8.dll          ← Ultimate ASI Loader
├── DSOpt.asi            ← This plugin
└── DSOpt.ini            ← Configuration
```

Alternatively:

```text
Dead Space/
├── Dead Space.exe
├── dinput8.dll
└── scripts/
    ├── DSOpt.asi
    └── DSOpt.ini
```

`DSOpt.asi` and `DSOpt.ini` must be in the same directory so the plugin can locate its configuration.

### Upgrade Notes

Do not load this version together with:

- The old DSOpt FontFix;
- Another patch that modifies the same subtitle scaling path.

Duplicate Hooks or duplicate scaling may cause incorrect subtitle sizes, clipping, or crashes.

---

## Recommended Plugins

### DXVK

**[DXVK](https://github.com/doitsujin/dxvk)** translates the game's Direct3D 9 rendering to Vulkan.

It can be used to:

- Improve compatibility on some modern systems;
- Limit the game to 60 FPS through DXVK configuration;
- Mitigate physics, animation, and control issues caused by high frame rates.

### dgVoodoo2

**[dgVoodoo2](https://github.com/dege-diosg/dgVoodoo2)** translates the game to Direct3D 11/12.

It can be used to:

- Apply external vertical synchronization;
- Add MSAA;
- Improve compatibility with some graphics hardware and drivers.

When using dgVoodoo2, it is recommended to keep the game in windowed mode rather than enabling exclusive fullscreen.

When exclusive fullscreen is required, use:

```ini
[Window]
Borderless=0

[Subtitle]
Scale=1
```

DSOpt does not currently include direct fixes for:

- High-frame-rate physics behavior;
- VSync refresh-rate selection.

These issues can currently be handled through an external frame-rate limiter or graphics wrapper.

---

## Configuration

Edit `DSOpt.ini` in the same directory as `DSOpt.asi`:

```ini
[CrashFix]
; 0=disable the CPU limit
; 1-8=use the first N logical processors currently allowed for the process
; invalid values fall back to 4
CoreLimit=4

[Window]
; 0=do not change DPI awareness
; 1=enable high-DPI awareness
; Per-Monitor V2 is preferred, with automatic fallbacks on older Windows versions
DPIAware=1

; 0=keep a normal window on startup
; 1=automatically convert the game to a borderless window
; exclusive fullscreen is not forcibly modified
Borderless=1

; 0=do not reposition the window automatically
; 1=center the window after startup and resolution changes
; multi-monitor layouts and negative monitor coordinates are supported
CenterWindow=1

; 0=normal window level
; 1=keep the game window above other windows
AlwaysOnTop=0

; Windows virtual-key code used to toggle borderless mode
; 0=disable the hotkey
; 121=F10
ToggleKey=121

; interval used to maintain window style, position, topmost state,
; and detect resolution changes
; unit: milliseconds
; valid range: 100-60000
PollInterval=1000

[Subtitle]
; 0=disable the full subtitle-pipeline fix
; 1=fix preprocessing, automatic wrapping, two-line capacity,
; line spacing, and final drawing
FixFullPipelineScaling=1

; 0=automatically use "actual game render height / BaseHeight"
; a positive number forces a fixed manual multiplier
; valid manual range: 0.25-8.0
; examples: 1=no extra scaling, 1.5=1.5x, 3=3x
Scale=0

; vertical baseline used by automatic subtitle scaling
; the game was developed for 1280x720, so the default is 720
; valid range: 240-4320; invalid values fall back to 720
BaseHeight=720

; controls whether resolutions at or below BaseHeight are scaled down
; 1=preserve original size at BaseHeight and below,
; and only fix higher resolutions
; 0=scale every resolution proportionally using actual height/BaseHeight
OnlyAboveBase=1

[Debug]
; 0=disable logging
; 1=create DSOpt.log next to DSOpt.asi
Log=0
```

### Recommended Defaults

```ini
[CrashFix]
CoreLimit=4

[Window]
DPIAware=1
Borderless=1
CenterWindow=1
AlwaysOnTop=0
ToggleKey=121
PollInterval=1000

[Subtitle]
FixFullPipelineScaling=1
Scale=0
BaseHeight=720
OnlyAboveBase=1

[Debug]
Log=0
```

### Automatic Subtitle Scale Examples

With:

```ini
Scale=0
BaseHeight=720
OnlyAboveBase=1
```

the automatic scale is:

| Actual vertical resolution | Subtitle scale |
|---:|---:|
| 480 | 1.000 |
| 600 | 1.000 |
| 720 | 1.000 |
| 768 | 1.067 |
| 1080 | 1.500 |
| 1440 | 2.000 |
| 2160 | 3.000 |

Setting `OnlyAboveBase=0` also scales resolutions below 720p proportionally.

---

## Automatically Managed Game Files

### settings.txt

DSOpt checks:

```text
%LOCALAPPDATA%\Electronic Arts\Dead Space\settings.txt
```

It adds the following entries only when the corresponding keys are missing:

```text
Window.Width = current desktop width
Window.Height = current desktop height
Window.Fullscreen = false
Window.VSync = false
```

Existing values are not overwritten.

To avoid damaging user configuration, the file is not modified when:

- It exceeds the safety size limit;
- It cannot be read safely;
- It uses UTF-16;
- It contains binary data.

Changes are written through a temporary file and atomic replacement to reduce the risk of corruption if the operation is interrupted.

### joypad.txt

DSOpt checks:

```text
%USERPROFILE%\Documents\Electronic Arts\Dead Space\joypad.txt
```

Rules:

```text
file does not exist
→ create an empty file

file exists and is readable
→ leave it unchanged

file exists but is unreadable
→ clear the read-only attribute
→ delete the existing file
→ create a new empty file
```

This behavior fixes a possible progression block in the Chapter 4 asteroid turret section when `joypad.txt` is missing or unreadable.

---

## Logging

Set:

```ini
[Debug]
Log=1
```

The plugin creates:

```text
DSOpt.log
```

in the same directory as `DSOpt.asi`.

The log records information such as:

- Plugin initialization;
- CPU affinity configuration;
- DPI-awareness initialization;
- Game-file handling;
- Window discovery and borderless-mode changes;
- Subtitle pattern scanning and Hook installation;
- Subtitle scale updates after resolution changes;
- Error codes when an operation fails.

Logging is disabled by default.

---

## Source Layout

```text
src/
├── common/
│   ├── file_utils.cpp
│   ├── file_utils.hpp
│   ├── logging_types.hpp
│   ├── memory_patch.cpp
│   ├── memory_patch.hpp
│   ├── pe_scanner.cpp
│   └── pe_scanner.hpp
│
└── ds_opt/
    ├── app_config.cpp
    ├── app_config.hpp
    ├── ds_opt.cpp
    ├── ds_opt.ini
    ├── game_files.cpp
    ├── game_files.hpp
    ├── logging.cpp
    ├── logging.hpp
    ├── process_fixes.cpp
    ├── process_fixes.hpp
    ├── subtitle_scaling.cpp
    ├── subtitle_scaling.hpp
    ├── window_manager.cpp
    └── window_manager.hpp
```

Module responsibilities:

| Module | Purpose |
|------|------|
| `ds_opt.cpp` | DLL lifecycle, worker thread, and module scheduling |
| `app_config.*` | INI creation, parsing, range validation, and defaults |
| `logging.*` | Runtime logging |
| `process_fixes.*` | CPU affinity limit and DPI awareness |
| `game_files.*` | `settings.txt` and `joypad.txt` management |
| `window_manager.*` | Window discovery, borderless mode, centering, topmost state, and hotkey |
| `subtitle_scaling.*` | Subtitle preprocessing, automatic wrapping, and final drawing fix |
| `common/*` | Shared file operations, PE scanning, and memory patching |

---

## Build

Install Visual Studio 2022 with:

```text
Desktop development with C++
MSVC x86/x64 build tools
Windows SDK
```

The recommended method is to run:

```bat
build.bat
```

The build script automatically locates Visual Studio, initializes the x86 toolchain, and produces:

```text
build/DSOpt.asi
build/DSOpt.ini
```

### Manual Build

Open an x86 Native Tools Command Prompt for Visual Studio, or initialize the x86 environment manually:

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
```

Then run:

```bat
cl.exe /nologo /utf-8 /O2 /MT /LD /std:c++17 /EHsc /permissive- /W4 ^
    /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
    /I"src\common" /I"src\ds_opt" ^
    "src\common\file_utils.cpp" ^
    "src\common\memory_patch.cpp" ^
    "src\common\pe_scanner.cpp" ^
    "src\ds_opt\app_config.cpp" ^
    "src\ds_opt\ds_opt.cpp" ^
    "src\ds_opt\game_files.cpp" ^
    "src\ds_opt\logging.cpp" ^
    "src\ds_opt\process_fixes.cpp" ^
    "src\ds_opt\subtitle_scaling.cpp" ^
    "src\ds_opt\window_manager.cpp" ^
    /Fe:"build\DSOpt.asi" ^
    /link kernel32.lib user32.lib shell32.lib ^
    /SUBSYSTEM:WINDOWS /MACHINE:X86 /OUT:"build\DSOpt.asi"
```

The game and ASI loader are 32-bit, so DSOpt must be built for `x86`, not `x64`.

---

## Credits

- [mafril](https://fearlessrevolution.com/viewtopic.php?f=4&t=18320) and [Zanzer](https://fearlessrevolution.com/viewtopic.php?f=4&t=18320) — Their Cheat Engine tables helped locate the key subtitle-scaling addresses
- [MarkerPatch](https://github.com/Wemino/MarkerPatch) — Provided important reference ideas for correcting the complete subtitle-scaling pipeline
- [Borderless Gaming](https://github.com/Codeusa/Borderless-Gaming) — Inspired the borderless-window implementation
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) — Inspired the mod's loading and project structure
- DeepSeek — The existence that lends wings to people

---

## License

MIT License
