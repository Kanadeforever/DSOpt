# DSOpt

中文 | [English](README_EN.md)

ASI 插件，用于修复《死亡空间》（Dead Space, 2008）在现代 Windows 系统上的兼容性、字幕缩放和窗口模式问题。

---

## 功能

| 功能 | 说明 |
|------|------|
| **多核 CPU 崩溃修复** | 限制游戏进程使用的逻辑处理器数量，解决部分超过 10 核心的 CPU 无法正常启动游戏的问题 |
| **完整字幕分辨率缩放** | 同步修复字幕预排版、自动换行、两行容量、行距和最终绘制，使字幕在高分辨率下保持与 720p 一致的相对大小和排版规则 |
| **分辨率切换同步** | 在字幕预排版阶段同步更新缩放倍率，修复从低分辨率切换到 4K 后字幕再次截断的问题 |
| **默认设置补全** | 首次启动时自动补充窗口模式、关闭垂直同步，并将默认分辨率设置为当前桌面分辨率 |
| **高 DPI 支持** | 优先启用 Per-Monitor V2 DPI 感知，修复高 DPI 缩放下的窗口尺寸和坐标错位 |
| **无边框窗口模式** | 支持启动时自动启用，并可通过 F10 热键切换；游戏处于独占全屏时自动跳过 |
| **窗口自动居中** | 启动和切换分辨率后自动居中窗口，支持多显示器及负坐标显示器布局 |
| **窗口始终置顶** | 可选地让游戏窗口保持在其他窗口上方 |
| **INI 自动恢复** | `DSOpt.ini` 丢失时自动创建带有完整中文注释的默认配置文件，不覆盖已有配置 |
| **自动创建 joypad.txt** | 文件不存在时自动创建；文件存在但无法读取时删除并重建，用于修复第四章小行星炮台部分可能卡住的问题 |

---

## 字幕修复说明

《死亡空间》原版在高于 720p 的分辨率下，没有让字幕的预排版和最终绘制使用一致的分辨率倍率，可能产生以下问题：

- 字幕相对画面过小；
- 同一行容纳的文字过多；
- 自动换行规则与 720p 不一致；
- 放大字体后出现第三行；
- 第三行或后续字幕被裁剪；
- 切换分辨率后布局和绘制使用了不同倍率。

DSOpt 会同时修复两段字幕处理流程：

```text
预排版 / 行数容量 / 自动换行
                +
最终行度量 / 字形绘制
```

自动倍率为：

```text
游戏实际渲染高度 / BaseHeight
```

默认以游戏开发分辨率的纵向高度 `720` 为基准。

已测试在 `640×480` 至 `3840×2160` 之间频繁切换分辨率，字幕最多保持两行，自动换行正常，不再发生第三行截断。

---

## 支持的 EXE 版本

理论上支持常见的《死亡空间》PC 版 EXE，包括经过旧汉化补丁修改的可执行文件。

已测试：

- Steam 版；
- GOG 版；
- 旧汉化补丁附带的修改版 EXE；
- 3DM 汉化补丁使用的 RLD 修改版 EXE。

补丁使用特征码在运行时定位游戏代码，不依赖单一固定 EXE 地址。

Steam 原始 EXE 带有 SteamStub，磁盘文件中可能无法直接找到特征码。DSOpt 会在游戏运行时等待代码完成解包后再次检测。

如果当前版本无法匹配，字幕功能会安全停止安装，并在启用日志后记录原因，不会只安装一半 Hook。

---

## 安装

1. 确保游戏目录中已经安装 `32bit` 版 **[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)**，例如：
   - `dinput8.dll`
   - `winmm.dll`
   - `version.dll`
2. 将 `DSOpt.asi` 和 `DSOpt.ini` 放在同一个 ASI 加载目录内。
3. 启动游戏。

支持放置在以下任意位置：

```text
游戏根目录
scripts/
plugins/
update/
```

若使用asi插件版汉化则推荐放置在 `update` 文件夹。

示例：

```text
Dead Space/
├── Dead Space.exe
├── dinput8.dll          ← Ultimate ASI Loader
├── DSOpt.asi            ← 本插件
└── DSOpt.ini            ← 配置文件
```

也可以使用：

```text
Dead Space/
├── Dead Space.exe
├── dinput8.dll
└── scripts/
    ├── DSOpt.asi
    └── DSOpt.ini
```

`DSOpt.asi` 和 `DSOpt.ini` 必须放在同一目录，否则插件无法读取对应配置。

### 升级注意事项

不要同时加载：

- 旧版 DSOpt；
- 其他修改同一字幕缩放路径的补丁。

重复 Hook 或重复缩放可能导致字幕尺寸异常、截断或游戏崩溃。

---

## 推荐插件

### DXVK

**[DXVK](https://github.com/doitsujin/dxvk)** 可以将游戏的 Direct3D 9 渲染转换为 Vulkan。

可用于：

- 改善部分现代系统上的运行兼容性；
- 通过 DXVK 配置限制到 60 FPS；
- 缓解高帧率导致的物理、动画和操作异常。

### dgVoodoo2

**[dgVoodoo2](https://github.com/dege-diosg/dgVoodoo2)** 可以将游戏转换到 Direct3D 11/12。

可用于：

- 设置外部垂直同步；
- 添加 MSAA；
- 改善部分显卡和驱动环境下的兼容性。

使用 dgVoodoo2 时，建议不要在游戏内启用独占全屏。

如果必须使用游戏全屏模式，建议设置：

```ini
[Window]
Borderless=0

[Subtitle]
Scale=1
```

当前 DSOpt 尚未直接实现：

- 高帧率物理行为修复；
- 垂直同步刷新率选择修复。

这些功能仍可暂时通过外部帧率限制或图形包装器处理。

---

## 配置

编辑与 `DSOpt.asi` 位于同一目录的 `DSOpt.ini`：

```ini
[CrashFix]
; 0=关闭核心限制
; 1-8=限制进程使用前 N 个当前允许的逻辑处理器
; 非法数值会自动回退为 4
CoreLimit=4

[Window]
; 0=关闭 DPI 感知设置
; 1=启用高 DPI 感知
; 优先使用 Per-Monitor V2，旧版 Windows 会自动回退
DPIAware=1

; 0=启动时保持普通窗口模式
; 1=启动时自动转换为无边框窗口
; 游戏处于独占全屏时不会强制应用
Borderless=1

; 0=不自动调整窗口位置
; 1=启动和切换分辨率后自动居中
; 支持多显示器和负坐标显示器
CenterWindow=1

; 0=普通窗口层级
; 1=让游戏窗口保持置顶
AlwaysOnTop=0

; 无边框模式切换热键的 Windows 虚拟键码
; 0=禁用热键
; 121=F10
ToggleKey=121

; 窗口样式、位置、置顶和分辨率变化的检查间隔
; 单位：毫秒
; 有效范围：100-60000
PollInterval=1000

[Subtitle]
; 0=关闭字幕完整排版链修复
; 1=同时修复预排版、自动换行、两行容量、行距和最终绘制
FixFullPipelineScaling=1

; 0=自动使用“游戏实际渲染高度 / BaseHeight”
; 正数=强制使用固定倍率
; 手动倍率有效范围为 0.25-8.0
; 例如：1=不额外缩放，1.5=放大至 1.5 倍，3=放大至 3 倍
Scale=0

; 字幕自动缩放使用的纵向基准分辨率
; 游戏开发分辨率为 1280×720，因此默认值为 720
; 有效范围：240-4320，非法数值会回退为 720
BaseHeight=720

; 控制低于或等于 BaseHeight 的分辨率是否按比例缩小
; 1=BaseHeight 及以下保留原版大小，只修复高分辨率
; 0=所有分辨率都严格按照实际高度/BaseHeight 等比例缩放
OnlyAboveBase=1

[Debug]
; 0=关闭日志
; 1=在 DSOpt.asi 同目录生成 DSOpt.log
; 日志内容使用简体中文，文件编码为 UTF-8
Log=0
```

### 推荐默认配置

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

### 字幕倍率示例

当使用：

```ini
Scale=0
BaseHeight=720
OnlyAboveBase=1
```

自动倍率如下：

| 实际纵向分辨率 | 字幕倍率 |
|---:|---:|
| 480 | 1.000 |
| 600 | 1.000 |
| 720 | 1.000 |
| 768 | 1.067 |
| 1080 | 1.500 |
| 1440 | 2.000 |
| 2160 | 3.000 |

将 `OnlyAboveBase` 设置为 `0` 后，低于 720p 的分辨率也会按比例缩小。

---

## 自动修改的游戏文件

### settings.txt

DSOpt 会检查：

```text
%LOCALAPPDATA%\Electronic Arts\Dead Space\settings.txt
```

只在对应键不存在时补充：

```text
Window.Width = 当前桌面宽度
Window.Height = 当前桌面高度
Window.Fullscreen = false
Window.VSync = false
```

已有键值不会被覆盖。

为了避免损坏用户配置，遇到以下情况时不会修改：

- 文件超过安全大小限制；
- 文件无法安全读取；
- 文件为 UTF-16；
- 文件包含二进制数据。

写入时使用临时文件和原子替换，降低异常中断导致配置损坏的风险。

### joypad.txt

DSOpt 会检查：

```text
%USERPROFILE%\Documents\Electronic Arts\Dead Space\joypad.txt
```

处理规则：

```text
文件不存在
→ 创建空文件

文件存在并且能够读取
→ 保持原样

文件存在但无法读取
→ 清除只读属性
→ 删除原文件
→ 创建新的空文件
```

该行为用于修复第四章小行星炮台部分可能因为缺失或不可读的 `joypad.txt` 而卡住的问题。

---

## 日志

设置：

```ini
[Debug]
Log=1
```

插件会在 `DSOpt.asi` 同目录生成：

```text
DSOpt.log
```

日志为 UTF-8 编码，内容全部使用简体中文。

示例：

```text
[进程] 已限制为当前允许范围内的 4 个逻辑处理器。
[进程] 已启用每显示器 V2 DPI 感知。
[文件] 已安全补充 settings.txt 中缺失的默认项。
[字幕] 已安装完整排版链：预排版 RVA=...，绘制 RVA=...。
[字幕] 初始排版倍率：渲染高度=2160，字幕倍率=3.000。
[窗口] 已应用启动无边框模式。
```

---

## 源码结构

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

各模块职责：

| 模块 | 作用 |
|------|------|
| `ds_opt.cpp` | DLL 生命周期、工作线程和各模块调度 |
| `app_config.*` | INI 自动创建、读取、范围校验和默认值 |
| `logging.*` | 简体中文 UTF-8 日志 |
| `process_fixes.*` | CPU 核心限制和 DPI 感知 |
| `game_files.*` | `settings.txt` 和 `joypad.txt` 处理 |
| `window_manager.*` | 窗口查找、无边框、居中、置顶和热键 |
| `subtitle_scaling.*` | 字幕预排版、自动换行和最终绘制修复 |
| `common/*` | 文件操作、PE 扫描和内存补丁公共代码 |

所有 C++ 源码均包含详细的简体中文注释。

---

## 构建

需要安装 Visual Studio 2022，并勾选：

```text
使用 C++ 的桌面开发
MSVC x86/x64 编译工具
Windows SDK
```

推荐直接运行：

```bat
build.bat
```

构建脚本会自动查找 Visual Studio、初始化 x86 编译环境，并输出：

```text
build/DSOpt.asi
build/DSOpt.ini
```

> 本项目的中文 Windows 批处理文件统一使用 **GBK（CP936）编码和 CRLF 换行符**。

### 手动构建

先打开 Visual Studio x86 Native Tools Command Prompt，或手动初始化 x86 环境：

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
```

然后执行：

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

游戏和 ASI Loader 均为 32 位，因此必须构建为 `x86`，不能构建为 `x64`。

---

## 致谢

- [mafril](https://fearlessrevolution.com/viewtopic.php?f=4&t=18320) 和 [Zanzer](https://fearlessrevolution.com/viewtopic.php?f=4&t=18320) — 通过其 Cheat Engine 表定位了字幕缩放的关键地址
- [MarkerPatch](https://github.com/Wemino/MarkerPatch) — 为完整字幕缩放链修复提供了重要思路参考
- [Borderless Gaming](https://github.com/Codeusa/Borderless-Gaming) — 启发了无边框窗口模式
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) — 启发了本 MOD 的加载和工程结构
- DeepSeek — 借给人们翅膀的存在

---

## 许可

MIT License
