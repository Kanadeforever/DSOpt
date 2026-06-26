# DSOpt

中文 | [English](README_EN.md)

ASI 插件，修复《死亡空间》(Dead Space, 2008) 在现代 Windows 上的各类问题。

---

## 功能

| 功能 | 说明 |
|------|------|
| **多核 CPU 崩溃修复** | 限制进程核心数，解决超过 10 核心 CPU 上游戏无法启动的问题 |
| **字幕分辨率缩放** | 高分辨率下字幕自动缩放，不再极小看不清 |
| **默认设置代理** | 首次启动自动设为窗口模式、关闭垂直同步、匹配当前分辨率 |
| **高 DPI 支持** | 声明 PerMonitor DPI，修复高 DPI 下的坐标错位 |
| **无边框窗口模式** | 支持 F10 热键切换，游戏全屏时自动跳过 |
| **窗口自动居中** | 启动和切换分辨率时自动居中窗口 |
| **窗口始终置顶** | 可选 |
| **INI 自动恢复** | 配置文件丢失自动重建 |
| **自动创建joypad.txt** | 修复第四章小行星炮台部分卡住 |

## 支持的EXE版本

- 理论上任意PC版，包括3DM汉化补丁附带的修改过的RLD破解版

## 安装

1. 确保已有 **[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)**（`dinput8.dll` 或 `winmm.dll`）在游戏目录
2. 将 `DSOpt.asi` 和 `DSOpt.ini` 放入游戏根目录/scripts/plugins这三个目录的任意一个内。

```
Dead Space/
├── Dead Space.exe
├── dinput8.dll          ← Ultimate ASI Loader
├── DSOpt.asi            ← 本插件
└── DSOpt.ini            ← 配置文件
```

## 推荐插件

- **[DXVK](https://github.com/doitsujin/dxvk)**：转换到Vulkan，也可以锁60帧，以修复高帧率带来的各种物理问题。
- **[dgvoodoo2](https://github.com/dege-diosg/dgVoodoo2)**：转换到DX11/12，可以设置外部垂直同步、为游戏添加MSAA。
    - 注意，使用dgvoodoo2时，不要把游戏设为全屏。如果一定要，就把`Scale`设为1、`Borderless` 设为0。

## 配置

编辑 `DSOpt.ini`：

```ini
[CrashFix]
CoreLimit=4         ; 0=关闭, 1-8=限制核心数

[Window]
DPIAware=1          ; 高 DPI 感知
Borderless=1        ; 1=启动时无边框窗口
CenterWindow=1      ; 自动居中
AlwaysOnTop=0       ; 窗口置顶
ToggleKey=121       ; 无边框热键 (0=禁用, 121=F10)
PollInterval=1000   ; 窗口任务轮询间隔(毫秒)

[Subtitle]
Scale=0             ; 0=自动 (窗口高/720), 或手动设置倍率
BaseHeight=720      ; 自动缩放基准分辨率

[Debug]
Log=0               ; 1=写入 DSOpt.log
```

## 构建

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cl.exe /nologo /utf-8 /O2 /MT /LD /GS- /std:c++17 /I"src\common" src\ds_opt\ds_opt.cpp /link kernel32.lib user32.lib /SUBSYSTEM:WINDOWS /MACHINE:X86 /OUT:DSOpt.asi
```

## 致谢

- [mafril](https://fearlessrevolution.com/viewtopic.php?f=4&t=18320) 和 [Zanzer](https://fearlessrevolution.com/viewtopic.php?f=4&t=18320) — 提供的CheatEngine 表找到了字幕缩放关键地址
- [MarkerPatch](https://github.com/Wemino/MarkerPatch) — 启发了本项目
- [Borderless Gaming](https://github.com/Codeusa/Borderless-Gaming) — 启发了无边框窗口模式
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) — 启发了本MOD的结构
- DeepSeek — 借给人们翅膀的存在

## 许可

MIT License
