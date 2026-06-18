# DSOpt

ASI plugin fixing various issues in Dead Space (2008) on modern Windows.

## Features

| Feature | Description |
|---------|-------------|
| **Multi-core CPU crash fix** | Limits process CPU affinity — prevents crash on CPUs with >10 cores |
| **Subtitle resolution scaling** | Subtitles auto-scale with resolution so they're readable at 1440p/4K |
| **Default settings override** | First launch: windowed mode, VSync off, matches current screen resolution |
| **High DPI support** | Declares PerMonitor DPI awareness, fixes coordinate offsets on HiDPI |
| **Borderless window mode** | F10 hotkey toggle, auto-skips when game is in exclusive fullscreen |
| **Auto-center window** | Centers the window on startup and after resolution changes |
| **Always-on-top** | Optional |
| **INI auto-restore** | Recreates config file if deleted |
| **Auto-create joypad.txt** | Fixes "Stuck at asteroid cannon section in chapter 4" (cursor lock glitch) |

## Supported EXE versions

- Theoretically any PC version, including the modified RLD crack bundled with the 3DM Chinese patch

## Installation

1. Ensure **[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)** (`dinput8.dll` or `winmm.dll`) is in the game directory
2. Place `DSOpt.asi` and `DSOpt.ini` in one of: the game root directory, `scripts/`, or `plugins/`

```
Dead Space/
├── Dead Space.exe
├── dinput8.dll          ← Ultimate ASI Loader
├── DSOpt.asi            ← This plugin
└── DSOpt.ini            ← Configuration
```

## Recommended plugins

- **[DXVK](https://github.com/doitsujin/dxvk)**: Translates to Vulkan; can also cap at 60 FPS to fix various physics issues caused by high frame rates.
- **[dgvoodoo2](https://github.com/dege-diosg/dgVoodoo2)**: Translates to DX11/12; supports external V-Sync and adds MSAA for the game.
    - Note: when using dgVoodoo2, avoid setting the game to fullscreen. If you must, set `Scale` to `1` and `Borderless` to `0`.

## Configuration

Edit `DSOpt.ini`:

```ini
[CrashFix]
CoreLimit=4         ; 0=disabled, 1-8=CPU core limit

[Window]
DPIAware=1          ; Per-monitor DPI awareness
Borderless=1        ; 1=borderless on startup
CenterWindow=1      ; Auto-center window
AlwaysOnTop=0       ; Keep window on top
ToggleKey=121       ; Borderless toggle hotkey (0=disabled, 121=F10)
PollInterval=1000   ; Window tasks poll interval in ms

[Subtitle]
Scale=0             ; 0=auto (window height/720), or manual multiplier
BaseHeight=720      ; Reference resolution height for auto-scale

[Debug]
Log=0               ; 1=write DSOpt.log
```

## Build

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cl.exe /nologo /utf-8 /O2 /MT /LD /GS- /std:c++17 /I"src\common" src\ds_opt\ds_opt.cpp /link kernel32.lib user32.lib /SUBSYSTEM:WINDOWS /MACHINE:X86 /OUT:DSOpt.asi
```

## Credits

- [mafril](https://fearlessrevolution.com/viewtopic.php?f=4&t=18320) and [Zanzer](https://fearlessrevolution.com/viewtopic.php?f=4&t=18320) — CheatEngine tables that provided the key subtitle scaling address
- [MarkerPatch](https://github.com/Wemino/MarkerPatch) — inspiration for this project
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)

## License

MIT License
