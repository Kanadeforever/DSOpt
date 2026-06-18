// =============================================================================
// DSOpt.asi — 《死亡空间》(Dead Space, 2008) 修复与优化插件（统一版）
//
// 本插件通过 ASI Loader (dinput8.dll) 注入游戏进程，
// 在 DllMain 中完成所有初始化：读取配置、安装 Hook、启动工作线程。
//
// 全 Unicode API 实现，支持全球用户路径中的非英文字符。
//
// =============================================================================
// 功能清单（由 DSOpt.ini 配置）
// =============================================================================
//   [CrashFix] CoreLimit=4          0=禁用, 1-8=限制进程到 N 个核心
//   [Window]   DPIAware=1           声明 PerMonitor DPI 感知，修复高 DPI 坐标偏移
//   [Window]   Borderless=1         启动时自动切换到无边框窗口模式
//   [Window]   CenterWindow=1       启动 / 分辨率变更时自动居中窗口
//   [Window]   AlwaysOnTop=0        强制窗口置顶
//   [Window]   ToggleKey=121        无边框切换热键虚拟键码 (0=禁用, 121=F10)
//   [Window]   PollInterval=1000    后台任务（居中、无边框维持、字号检测）轮询间隔 ms
//   [Subtitle] Scale=0              字幕缩放倍率 (0=自动: 窗口高度/BaseHeight)
//   [Subtitle] BaseHeight=720       自动缩放基准分辨率高度
//   [Debug]    Log=0                输出诊断日志到 DSOpt.log (0=关闭, 1=开启)
// =============================================================================

#include <windows.h>
#include <stdlib.h>
#include <atomic>
#include "../common/ini.hpp"       // INI 配置读取 (Unicode API)
#include "../common/hook.hpp"      // Hook 安装工具

// ===========================================================================
// 字幕缩放 (FontFix) 状态变量
// ===========================================================================
static float g_fontScale   = 1.0f;       // 当前字幕缩放倍率（运行时动态更新）
static int   g_baseHeight  = 720;        // 自动缩放基准高度（来自 INI）
static int   g_pollInterval = 1000;      // 慢速任务轮询间隔（来自 INI）
static bool  g_debugLog    = false;      // 是否输出诊断日志（来自 INI）
static int   g_fontLastH    = 0;         // 上一次检测到的窗口高度（去重用）
static wchar_t g_asiDir[MAX_PATH] = {};  // ASI 文件所在目录（日志输出路径）

// ---------------------------------------------------------------------------
// OptLog：条件诊断日志输出
// ---------------------------------------------------------------------------
// 当 g_debugLog == true 时，同时输出到：
//   1. OutputDebugStringW — 可用 DebugView 实时查看（Unicode）
//   2. <ASI目录>\DSOpt.log — UTF-8 + BOM 编码追加写入
//      首次创建时写入 BOM (EF BB BF)，确保 Notepad/VSCode 正确识别编码
// ---------------------------------------------------------------------------
static void OptLog(const wchar_t* msg) {
    if (!g_debugLog) return;
    OutputDebugStringW(msg);                                // 实时调试输出（Unicode）

    // 构造日志文件完整路径
    wchar_t logPath[MAX_PATH];
    wsprintfW(logPath, L"%s\\DSOpt.log", g_asiDir);

    // 将 Unicode 消息转为 UTF-8 字节流（跨平台可读）
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, msg, -1, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return;

    // 栈上分配，超长消息才堆分配（正常 < 512 字节）
    char  stackBuf[1024];
    char* utf8Buf = stackBuf;
    if ((size_t)utf8Len > sizeof(stackBuf))
        utf8Buf = (char*)HeapAlloc(GetProcessHeap(), 0, utf8Len);

    WideCharToMultiByte(CP_UTF8, 0, msg, -1, utf8Buf, utf8Len, NULL, NULL);

    // 打开文件（追加模式）
    HANDLE h = CreateFileW(logPath, GENERIC_WRITE,
                           FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        // 如果文件为空（新文件），先写入 UTF-8 BOM (EF BB BF)
        DWORD fileSize = GetFileSize(h, NULL);
        if (fileSize == 0) {
            static const UINT8 kBom[] = { 0xEF, 0xBB, 0xBF };
            DWORD bomWritten;
            WriteFile(h, kBom, sizeof(kBom), &bomWritten, NULL);
        }

        SetFilePointer(h, 0, NULL, FILE_END);               // 确保在文件末尾
        DWORD written;
        WriteFile(h, utf8Buf, (DWORD)(utf8Len - 1), &written, NULL);  // -1 排除结尾 null
        CloseHandle(h);
    }

    if (utf8Buf != stackBuf)
        HeapFree(GetProcessHeap(), 0, utf8Buf);
}

// ===========================================================================
// 字幕缩放 (FontFix) — AOB 特征码定义与扫描
// ===========================================================================
// 目标机器码序列（14 字节）：
//   83 7E 1C 00           cmp dword ptr [esi+1Ch], 0       ; 检查某标志位
//   F3 0F 10 46 44        movss xmm0, [esi+44h]            ; 读取原始字号
//   F3 0F 59 45 18        mulss xmm0, [ebp+18h]            ; ★ 关键：乘以缩放因子
//   53                    push ebx
//   8B ...                mov ...
//
// Hook 偏移 = 9，即 hook 在 MULSS 指令处 (地址偏移 +9)
// MULSS 指令长度 = 5 字节 (F3 0F 59 45 18)
//
// Trampoline 替换逻辑：
//   原始: MULSS xmm0, [ebp+18h]    ; xmm0 *= 游戏内部缩放因子
//   替换: MULSS xmm0, [g_fontScale] ; xmm0 *= 我们的缩放因子
//         然后执行原始 MULSS（保持原始逻辑链完整）
//   条件: EDI == 0 时才缩放（EDI 非零是普通 UI 文字，不应缩放）
// ---------------------------------------------------------------------------
static const UINT8 FONT_AOB[] = {
    0x83,0x7E,0x1C,0x00, 0xF3,0x0F,0x10,0x46,0x44,
    0xF3,0x0F,0x59,0x45,0x18, 0x53,0x8B
};
static const DWORD FONT_HOOK_OFF = 9;           // AOB 匹配后 +9 字节= MULSS 指令位置
static const DWORD FONT_INST_LEN = 5;           // MULSS 指令 = 5 字节

// ---------------------------------------------------------------------------
// FindFontAOB：在指定内存范围搜索字体 AOB 特征码（朴素线性扫描）
// ---------------------------------------------------------------------------
static UINT8* FindFontAOB(UINT8* start, DWORD size) {
    if (size < sizeof(FONT_AOB)) return NULL;
    for (DWORD i = 0; i <= size - sizeof(FONT_AOB); i++)
        if (memcmp(start + i, FONT_AOB, sizeof(FONT_AOB)) == 0)
            return start + i;                         // 返回 AOB 起始地址
    return NULL;
}

// ===========================================================================
// 全局配置与窗口状态
// ===========================================================================
// g_cfg — 所有用户可配置项，由 LoadConfig() 从 INI 读取
// 窗口状态变量 — 由工作线程和 Hook 函数共同维护
// ===========================================================================
static struct {
    int  coreLimit    = 4;           // CPU 核心限制数 (0=关闭, 1-8=限制)
    bool dpiAware     = true;        // 是否启用高 DPI 感知
    bool borderless   = true;        // 是否启动时自动无边框
    bool centerWindow = true;        // 是否自动居中窗口
    bool alwaysOnTop  = false;       // 是否强制窗口置顶
    int  toggleKey    = VK_F10;      // 无边框切换热键虚拟键码
} g_cfg;

static HWND              g_hWnd             = nullptr;   // 游戏主窗口句柄
static bool              g_borderlessActive = false;      // 当前是否处于无边框状态
static LONG              g_savedStyle       = 0;          // 切换到无边框前保存的普通样式
static LONG              g_savedExStyle     = 0;          // 切换到无边框前保存的扩展样式
static RECT              g_savedRect        = {};         // 切换到无边框前保存的窗口位置
static RECT              g_lastWinRect      = {};         // 上一次检测到的窗口矩形（居中检测用）
static std::atomic<bool> g_workerRunning(true);           // 工作线程运行标志（DLL 卸载时置 false）
static bool              g_topmostApplied   = false;      // 是否已应用置顶（防重复调用）

// ===========================================================================
// 功能模块：高 DPI 感知
// ===========================================================================
// 默认情况下，Windows 对旧程序使用 DPI 虚拟化，导致坐标缩放失真。
// 本函数声明进程支持 Per-Monitor DPI V2，使窗口坐标在高 DPI 显示器上正确。
// 兼容性策略：
//   1. 优先尝试 SetProcessDpiAwareness(2) — Win8.1+ PerMonitorV2
//   2. 回退到 SetProcessDPIAware() — Vista+ 系统级 DPI
// ===========================================================================
static void ApplyDPIAwareness() {
    if (!g_cfg.dpiAware) return;

    // 尝试加载 shcore.dll（Win8.1+）并调用 SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_V2 = 2)
    HMODULE hShcore = LoadLibraryW(L"shcore.dll");
    if (hShcore) {
        typedef HRESULT (WINAPI *Fn)(int);
        Fn fn = (Fn)GetProcAddress(hShcore, "SetProcessDpiAwareness");
        if (fn) { fn(2); return; }
    }

    // 回退：Vista/Win7 兼容方案
    SetProcessDPIAware();
}

// ===========================================================================
// 功能模块：默认游戏设置补全
// ===========================================================================
// 问题：《死亡空间》首次运行时 settings.txt 可能不存在或内容为空。
//       部分用户遇到 settings.txt 被创建为空文件或只包含部分设置项。
// 解决：检测到文件缺失或不完整时，自动补全以下四个关键项：
//       Window.Width / Window.Height — 匹配当前屏幕分辨率
//       Window.Fullscreen — 设为 false（窗口模式启动更稳定）
//       Window.VSync — 设为 false（避免 VSync 相关问题）
//
// 路径使用 Unicode API，内容保持 ANSI（游戏用 fopen/fscanf 以 ANSI 方式读取）。
// ===========================================================================

// ---------------------------------------------------------------------------
// MkDirRecursiveW：递归创建目录（类似 mkdir -p），Unicode 版
// ---------------------------------------------------------------------------
static void MkDirRecursiveW(wchar_t* path) {
    for (wchar_t* p = path; *p; p++) {
        if (*p == L'\\' && p > path) {
            wchar_t saved = *p; *p = L'\0';
            CreateDirectoryW(path, NULL);
            *p = saved;
        }
    }
    CreateDirectoryW(path, NULL);
}

static void ApplyDefaultSettings() {
    // 构造路径: %LOCALAPPDATA%\Electronic Arts\Dead Space\settings.txt（Unicode）
    wchar_t path[MAX_PATH];
    if (!ExpandEnvironmentStringsW(L"%LOCALAPPDATA%", path, MAX_PATH)) return;
    lstrcatW(path, L"\\Electronic Arts\\Dead Space\\settings.txt");

    // 确保目录存在（首次启动时可能没有 Electronic Arts\Dead Space 目录）
    wchar_t dir[MAX_PATH];
    lstrcpyW(dir, path);
    wchar_t* last = wcsrchr(dir, L'\\');
    if (last) { *last = L'\0'; MkDirRecursiveW(dir); }

    // 获取当前屏幕分辨率作为默认值
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if (sw <= 0 || sh <= 0) { sw = 1280; sh = 720; }

    // 定义需要补全的四个默认设置项（keys/values 均为 ASCII）
    struct { const char* key; char value[32]; } defaults[4];
    wsprintfA(defaults[0].value, "%d", sw);  defaults[0].key = "Window.Width";
    wsprintfA(defaults[1].value, "%d", sh);  defaults[1].key = "Window.Height";
    lstrcpyA(defaults[2].value, "false");     defaults[2].key = "Window.Fullscreen";
    lstrcpyA(defaults[3].value, "false");     defaults[3].key = "Window.VSync";

    // 读取现有文件内容（如果存在）— 使用 Unicode 路径 + 二进制读取
    HANDLE hRead = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    char existing[4096] = {};
    DWORD existingLen = 0;
    if (hRead != INVALID_HANDLE_VALUE) {
        ReadFile(hRead, existing, sizeof(existing) - 1, &existingLen, NULL);
        CloseHandle(hRead);
    }

    // 遍历检查每个默认项是否已在文件中（简单 ASCII 子串匹配 "Key = "）
    char append[256] = {};
    int appendLen = 0;
    for (int i = 0; i < 4; i++) {
        char needle[64];
        wsprintfA(needle, "%s =", defaults[i].key);
        if (!strstr(existing, needle))
            appendLen += wsprintfA(append + appendLen, "%s = %s\r\n",
                                   defaults[i].key, defaults[i].value);
    }
    if (appendLen == 0) return;                          // 全部已存在，无需写入

    // 重写文件：保留原有内容 + 追加缺失项（Unicode 路径 + ANSI 内容）
    HANDLE hWrite = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hWrite == INVALID_HANDLE_VALUE) return;
    DWORD written;
    if (existingLen > 0) WriteFile(hWrite, existing, existingLen, &written, NULL);
    WriteFile(hWrite, append, appendLen, &written, NULL);
    CloseHandle(hWrite);
}

// ===========================================================================
// 功能模块：CPU 核心数限制（修复超过 10 核 CPU 崩溃）
// ===========================================================================
// 问题：《死亡空间》(2008) 的线程调度代码在检测到超过 10 个 CPU 核心时
//       会尝试分配超出预期大小的数组，导致 "Display hardware video memory
//       exhausted" 错误并退出。
// 解决：通过 SetProcessAffinityMask 将进程限制在指定核心数范围内。
//
// CoreLimit 配置规则：
//   0  — 不限制（关闭修复）
//   1-8 — 限制到指定核心数
//   其他非法值 — 回退为 4
// ---------------------------------------------------------------------------
static void ApplyCoreLimit() {
    if (g_cfg.coreLimit == 0) return;                    // 关闭此功能

    int limit = g_cfg.coreLimit;
    if (limit < 1 || limit > 8) limit = 4;               // 非法值回退为 4

    DWORD_PTR procMask, sysMask;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask)) return;

    DWORD_PTR newMask = ((DWORD_PTR)1 << limit) - 1;
    newMask &= sysMask;                                  // 和系统可用核心做交集
    SetProcessAffinityMask(GetCurrentProcess(), newMask);
}

// ===========================================================================
// 功能模块：无边框窗口化（Borderless Fullscreen Window）
// ===========================================================================
// 通过动态修改窗口样式实现有标题栏窗口 ↔ 无边框全屏窗口的切换。
//
// 要移除的窗口样式（使窗口不再有标题栏、边框、系统菜单）：
//   WS_CAPTION | WS_BORDER | WS_DLGFRAME | WS_THICKFRAME |
//   WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX
//
// 要移除的扩展样式（使窗口不再有 3D 边缘）：
//   WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
//   WS_EX_CLIENTEDGE | WS_EX_STATICEDGE
//
// 添加的扩展样式：
//   WS_EX_APPWINDOW — 确保窗口在任务栏可见
// ===========================================================================

static constexpr LONG kStylesToRemove =
    WS_CAPTION | WS_BORDER | WS_DLGFRAME | WS_THICKFRAME |
    WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;

static constexpr LONG kExStylesToRemove =
    WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;

// ---------------------------------------------------------------------------
// StripStyles：移除窗口的标题栏和边框样式（切换到无边框状态）
// ---------------------------------------------------------------------------
static void StripStyles(HWND hwnd) {
    LONG s = (LONG)GetWindowLongPtrW(hwnd, GWL_STYLE);       // 读取当前普通样式
    LONG e = (LONG)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);     // 读取当前扩展样式
    SetWindowLongPtrW(hwnd, GWL_STYLE,   s & ~kStylesToRemove);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, (e & ~kExStylesToRemove) | WS_EX_APPWINDOW);
}

// ---------------------------------------------------------------------------
// RestoreStyles：恢复窗口原始样式（从无边框切换回普通窗口）
// ---------------------------------------------------------------------------
static void RestoreStyles(HWND hwnd, LONG style, LONG exStyle) {
    SetWindowLongPtrW(hwnd, GWL_STYLE,   style);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
}

// ---------------------------------------------------------------------------
// GetMonitorFullRect：获取窗口所在显示器完整区域（用于铺满屏幕）
// ---------------------------------------------------------------------------
static RECT GetMonitorFullRect(HWND hwnd) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    return mi.rcMonitor;                     // rcMonitor = 包含任务栏的完整显示器矩形
}

// ---------------------------------------------------------------------------
// IsFullscreen：检测游戏是否处于 D3D9 独占全屏模式
// ---------------------------------------------------------------------------
// 判断依据：D3D9 独占全屏窗口使用 WS_POPUP 样式且无 WS_CAPTION。
// 如果游戏处于独占全屏，我们不应该修改窗口样式（会破坏全屏渲染）。
// ---------------------------------------------------------------------------
static bool IsFullscreen(HWND hwnd) {
    LONG s = (LONG)GetWindowLongPtrW(hwnd, GWL_STYLE);
    return (s & WS_POPUP) && !(s & WS_CAPTION);
}

// ===========================================================================
// 功能模块：游戏窗口查找（供无边框切换和字号检测共用）
// ===========================================================================
struct FindCtx { DWORD pid; HWND hwnd; bool fallback; };

static BOOL CALLBACK FindProc(HWND hwnd, LPARAM lp) {
    FindCtx* c = (FindCtx*)lp;
    DWORD pid; GetWindowThreadProcessId(hwnd, &pid);
    if (pid != c->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    RECT r; if (!GetWindowRect(hwnd, &r)) return TRUE;
    if (r.right <= r.left || r.bottom <= r.top) return TRUE;

    LONG s = (LONG)GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (c->fallback) {
        if (!(s & WS_CHILD)) { c->hwnd = hwnd; return FALSE; }
    } else {
        if (s & WS_CAPTION) { c->hwnd = hwnd; return FALSE; }
    }
    return TRUE;
}

static HWND FindGameWindow() {
    FindCtx c = { GetCurrentProcessId(), nullptr, false };
    EnumWindows(FindProc, (LPARAM)&c);
    if (!c.hwnd) { c.fallback = true; EnumWindows(FindProc, (LPARAM)&c); }
    return c.hwnd;
}

// ===========================================================================
// 功能模块：窗口居中
// ===========================================================================
static void CenterWindow(HWND hwnd) {
    RECT wr;
    if (!GetWindowRect(hwnd, &wr)) return;
    int ww = wr.right - wr.left;
    int wh = wr.bottom - wr.top;

    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(mon, &mi)) return;

    int workW = mi.rcWork.right  - mi.rcWork.left;
    int workH = mi.rcWork.bottom - mi.rcWork.top;
    int cx = mi.rcWork.left + (workW - ww) / 2;
    int cy = mi.rcWork.top  + (workH - wh) / 2;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;

    SetWindowPos(hwnd, nullptr, cx, cy, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

// ===========================================================================
// 功能模块：无边框切换（Enable / Disable）
// ===========================================================================
static void EnableBorderless(HWND hwnd) {
    if (IsFullscreen(hwnd)) return;

    if (!g_borderlessActive) {
        g_savedStyle   = (LONG)GetWindowLongPtrW(hwnd, GWL_STYLE);
        g_savedExStyle = (LONG)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        GetWindowRect(hwnd, &g_savedRect);
    }

    StripStyles(hwnd);
    RECT mr = GetMonitorFullRect(hwnd);
    SetWindowPos(hwnd, nullptr, mr.left, mr.top,
        mr.right - mr.left, mr.bottom - mr.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    g_borderlessActive = true;
}

static void DisableBorderless(HWND hwnd) {
    RestoreStyles(hwnd, g_savedStyle, g_savedExStyle);
    SetWindowPos(hwnd, nullptr,
        g_savedRect.left, g_savedRect.top,
        g_savedRect.right - g_savedRect.left,
        g_savedRect.bottom - g_savedRect.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    g_borderlessActive = false;
}

// ===========================================================================
// 功能模块：热键切换（防重复触发的状态机）
// ===========================================================================
enum ToggleState { IDLE, PRESSED };
static ToggleState g_toggleState = IDLE;

static void PollToggleKey() {
    if (!g_hWnd || g_cfg.toggleKey == 0) return;
    if (GetForegroundWindow() != g_hWnd) { g_toggleState = IDLE; return; }

    bool down = (GetAsyncKeyState(g_cfg.toggleKey) & 0x8000) != 0;

    switch (g_toggleState) {
    case IDLE:
        if (down) g_toggleState = PRESSED;
        break;
    case PRESSED:
        if (!down) {
            if (g_borderlessActive) {
                DisableBorderless(g_hWnd);
                OptLog(L"[Opt] 无边框已关闭（热键）\n");
            } else {
                EnableBorderless(g_hWnd);
                OptLog(L"[Opt] 无边框已开启（热键）\n");
            }
            g_toggleState = IDLE;
        }
        break;
    }
}

// ===========================================================================
// 功能模块：字幕缩放倍率计算
// ===========================================================================
static void ComputeFontScale() {
    HWND hwnd = FindGameWindow();
    if (hwnd) {
        RECT rc;
        if (GetClientRect(hwnd, &rc)) {
            int h = rc.bottom - rc.top;
            if (h >= 480) {
                g_fontScale = (float)h / (float)g_baseHeight;
                return;
            }
        }
    }
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if (sh <= 0) sh = g_baseHeight;
    g_fontScale = (float)sh / (float)g_baseHeight;
}

// ===========================================================================
// 后台工作线程：窗口维护与字号监控
// ===========================================================================
// 双速轮询设计：
//   ┌──────────┬──────────────────┬────────────────────────────────┐
//   │ 速度     │ 周期             │ 任务                           │
//   ├──────────┼──────────────────┼────────────────────────────────┤
//   │ 快速     │ 固定 50ms        │ 热键检测（需要低延迟响应）     │
//   │ 慢速     │ PollInterval(ms) │ 边框维持 / 居中 / 字号检测    │
//   └──────────┴──────────────────┴────────────────────────────────┘
// ===========================================================================
static DWORD WINAPI WorkerThread(LPVOID) {
    Sleep(200);

    bool appliedInitial = false;
    int  slowTick = 0;
    int  slowEvery = g_pollInterval / 50;
    if (slowEvery < 1) slowEvery = 1;

    while (g_workerRunning.load(std::memory_order_acquire)) {
        // ---- 窗口句柄刷新（每个周期） ----
        if (g_hWnd && !IsWindow(g_hWnd)) {
            g_hWnd = nullptr;
            g_topmostApplied = false;
        }
        if (!g_hWnd) {
            g_hWnd = FindGameWindow();
            if (g_hWnd) {
                wchar_t msg[128];
                wsprintfW(msg, L"[Opt] 检测到游戏窗口: 0x%p\n", g_hWnd);
                OptLog(msg);
            }
        }

        // ---- 热键（每个周期，50ms） ----
        PollToggleKey();

        // ---- 慢速任务（PollInterval） ----
        if (++slowTick >= slowEvery) {
            slowTick = 0;

            if (g_hWnd) {
                // 启动时自动无边框（仅一次）
                if (!appliedInitial && g_cfg.borderless && !g_borderlessActive) {
                    EnableBorderless(g_hWnd);
                    appliedInitial = g_borderlessActive;
                    if (appliedInitial)
                        OptLog(L"[Opt] 启动时自动无边框已应用\n");
                }

                // 无边框状态维持
                if (g_borderlessActive && !IsFullscreen(g_hWnd)) {
                    LONG s = (LONG)GetWindowLongPtrW(g_hWnd, GWL_STYLE);
                    if (s & WS_CAPTION) {
                        StripStyles(g_hWnd);
                        RECT mr = GetMonitorFullRect(g_hWnd);
                        SetWindowPos(g_hWnd, nullptr, mr.left, mr.top,
                            mr.right - mr.left, mr.bottom - mr.top,
                            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                    }
                }

                // 窗口置顶维持
                if (g_cfg.alwaysOnTop && !g_topmostApplied) {
                    SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    g_topmostApplied = true;
                }

                // 分辨率变更时自动居中
                if (g_cfg.centerWindow && !g_borderlessActive) {
                    RECT wr;
                    if (GetWindowRect(g_hWnd, &wr)) {
                        int w = wr.right - wr.left;
                        int h = wr.bottom - wr.top;
                        int ow = g_lastWinRect.right  - g_lastWinRect.left;
                        int oh = g_lastWinRect.bottom - g_lastWinRect.top;
                        if (w != ow || h != oh) {
                            SetRect(&g_lastWinRect, wr.left, wr.top, wr.right, wr.bottom);
                            CenterWindow(g_hWnd);
                        }
                    }
                }

                // 字号缩放：实时监控窗口高度变化
                RECT rc;
                if (GetClientRect(g_hWnd, &rc)) {
                    int ch = rc.bottom - rc.top;
                    if (ch >= 480 && ch != g_fontLastH) {
                        g_fontLastH = ch;
                        g_fontScale = (float)ch / (float)g_baseHeight;
                        wchar_t logMsg[96];
                        int sInt = (int)g_fontScale;
                        int sFrac = (int)((g_fontScale - (float)sInt) * 1000.0f + 0.5f);
                        wsprintfW(logMsg, L"[Opt] 字号缩放: %d -> %d.%03d\n", ch, sInt, sFrac);
                        OptLog(logMsg);
                    }
                }
            }
        }

        Sleep(50);
    }
    return 0;
}

// ===========================================================================
// 功能模块：字体缩放 AOB Trampoline Hook（核心逆向工程部分）
// ===========================================================================
// Trampoline 汇编伪码：
//   pushf                          ; 保存 EFLAGS
//   test edi, edi                  ; EDI==0 是字幕文字？
//   jz   scale                     ; 是 → 跳到缩放逻辑
//   popf                           ; 不是 → 恢复 EFLAGS
//   <原始 MULSS 5 字节>            ; 执行原始指令
//   jmp  site+5                    ; 返回原始流程
// scale:
//   movss xmm0, [ebp+18h]          ; 重新加载
//   mulss xmm0, [g_fontScale]      ; 乘以我们的缩放因子
//   movss [ebp+18h], xmm0          ; 写回
//   <原始 MULSS 5 字节>            ; 执行原始指令
//   popf                           ; 恢复 EFLAGS
//   jmp  site+5                    ; 返回原始流程
// ===========================================================================
static bool InstallFontHook() {
    HMODULE exe = GetModuleHandleW(NULL);
    UINT8* base = (UINT8*)exe;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(base, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    if (dos->e_lfanew <= 0 || (DWORD)dos->e_lfanew >= mbi.RegionSize - sizeof(IMAGE_NT_HEADERS))
        return false;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    WORD ns = nt->FileHeader.NumberOfSections;
    if (ns == 0 || ns > 96) return false;

    UINT8* site = NULL;
    for (WORD i = 0; i < ns; i++) {
        if (!(sec[i].Characteristics & IMAGE_SCN_CNT_CODE)) continue;
        UINT8* f = FindFontAOB(base + sec[i].VirtualAddress, sec[i].Misc.VirtualSize);
        if (f) { site = f + FONT_HOOK_OFF; break; }
    }
    if (!site) {
        OptLog(L"[Opt] 字体 AOB 未找到！可能是不支持的 EXE 版本\n");
        return false;
    }

    wchar_t diagMsg[128];
    int sInt = (int)g_fontScale;
    int sFrac = (int)((g_fontScale - (float)sInt) * 1000.0f + 0.5f);
    wsprintfW(diagMsg, L"[Opt] 字体 Hook 安装: RVA=0x%08X 缩放倍率=%d.%03d\n",
              (DWORD)(site - base), sInt, sFrac);
    OptLog(diagMsg);

    DWORD old;
    if (!VirtualProtect(site, FONT_INST_LEN, PAGE_EXECUTE_READWRITE, &old)) return false;

    UINT8 orig[5];
    memcpy(orig, site, FONT_INST_LEN);

    UINT8* tr = (UINT8*)VirtualAlloc(NULL, 96, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) { VirtualProtect(site, FONT_INST_LEN, old, &old); return false; }

    // 手写 x86 机器码（trampoline 体）
    int p = 0;
    tr[p++]=0x9C; tr[p++]=0x85; tr[p++]=0xFF;                   // pushf; test edi,edi
    tr[p++]=0x74; tr[p++]=0x12;                                   // jz +0x12
    tr[p++]=0xF3; tr[p++]=0x0F; tr[p++]=0x10; tr[p++]=0x4D; tr[p++]=0x18; // movss xmm0,[ebp+18h]
    tr[p++]=0xF3; tr[p++]=0x0F; tr[p++]=0x59; tr[p++]=0x0D;               // mulss xmm0,[g_fontScale]
    *(DWORD*)(tr+p) = (DWORD)(ULONG_PTR)&g_fontScale; p+=4;
    tr[p++]=0xF3; tr[p++]=0x0F; tr[p++]=0x11; tr[p++]=0x4D; tr[p++]=0x18; // movss [ebp+18h],xmm0
    memcpy(tr+p, orig, FONT_INST_LEN); p+=FONT_INST_LEN;
    tr[p++]=0x9D;                                                  // popf
    tr[p++]=0xE9; *(LONG*)(tr+p) = (LONG)(site+FONT_INST_LEN-(tr+p)-4); p+=4; // jmp site+5

    FlushInstructionCache(GetCurrentProcess(), tr, p);
    site[0]=0xE9; *(LONG*)(site+1) = (LONG)(tr-site-5);
    VirtualProtect(site, FONT_INST_LEN, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return true;
}

// ===========================================================================
// 功能模块：CreateWindowExA 拦截
// ===========================================================================
// Hook 游戏调用的 ANSI 版 CreateWindowExA，在窗口创建完成时执行初始化。
// 内部窗口操作使用 Unicode API（GetWindowLongPtrW 等）。
// ===========================================================================
typedef HWND (WINAPI *FnCreateWindowExA)(
    DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int,
    HWND, HMENU, HINSTANCE, LPVOID);
static FnCreateWindowExA g_origCreate = nullptr;
static bool g_threadStarted = false;

static HWND WINAPI Hooked_CreateWindowExA(
    DWORD dwEx, LPCSTR cls, LPCSTR name, DWORD style,
    int x, int y, int w, int h,
    HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    HWND hwnd = g_origCreate(dwEx, cls, name, style, x, y, w, h, parent, menu, inst, param);
    if (hwnd && !parent) {
        if (g_cfg.centerWindow && !g_cfg.borderless) {
            CenterWindow(hwnd);
            GetWindowRect(hwnd, &g_lastWinRect);
        }
        if (g_cfg.alwaysOnTop)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (!g_threadStarted) {
            g_threadStarted = true;
            CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        }
    }
    return hwnd;
}

// ===========================================================================
// 功能模块：加载配置（全部使用 Unicode INI API）
// ===========================================================================
static void LoadConfig(HMODULE hMod) {
    // [CrashFix] 节
    g_cfg.coreLimit    = ReadIniInt(hMod, L"CrashFix", L"CoreLimit", 4);

    // [Window] 节
    g_cfg.dpiAware     = ReadIniInt(hMod, L"Window", L"DPIAware", 1) != 0;
    g_cfg.borderless   = ReadIniInt(hMod, L"Window", L"Borderless", 1) != 0;
    g_cfg.centerWindow = ReadIniInt(hMod, L"Window", L"CenterWindow", 1) != 0;
    g_cfg.alwaysOnTop  = ReadIniInt(hMod, L"Window", L"AlwaysOnTop", 0) != 0;
    g_cfg.toggleKey    = ReadIniInt(hMod, L"Window", L"ToggleKey", VK_F10);

    // [Subtitle] 节
    g_baseHeight   = ReadIniInt(hMod, L"Subtitle", L"BaseHeight", 720);
    if (g_baseHeight <= 0) g_baseHeight = 720;

    // [Window] 节 — PollInterval
    g_pollInterval = ReadIniInt(hMod, L"Window", L"PollInterval", 1000);
    if (g_pollInterval < 500) g_pollInterval = 500;

    // [Debug] 节
    g_debugLog     = ReadIniInt(hMod, L"Debug", L"Log", 0) != 0;

    // [Subtitle].Scale — 手动读取字符串以支持浮点数和逗号小数点
    std::wstring scaleStr = ReadIniStr(hMod, L"Subtitle", L"Scale", L"0");
    // 将宽字符串中的逗号替换为小数点（德语/法语等区域格式兼容）
    for (wchar_t& c : scaleStr) if (c == L',') c = L'.';
    g_fontScale = wcstof(scaleStr.c_str(), NULL);
    if (g_fontScale <= 0.0f) ComputeFontScale();

    g_fontLastH = 0;
}

// ===========================================================================
// 功能模块：INI 文件自动恢复（UTF-16 LE + BOM 编码）
// ===========================================================================
// 当用户误删 DSOpt.ini 或在全新安装环境下运行时，
// 自动在 ASI 同目录创建一个带完整注释的默认配置文件。
//
// 文件编码：UTF-16 LE + BOM
//   - BOM (FF FE) 使 Notepad/VSCode 正确识别 UTF-16 LE 编码
//   - UTF-16 LE 是 Windows INI API（GetPrivateProfileStringW）的原生格式
//   - 中文注释在任何语言的系统上都能正确显示
// ===========================================================================
static const wchar_t kDefaultIni[] =
    L"[CrashFix]\r\n"
    L"; 0=禁用, 1-8=限制进程到 N 个 CPU 核心，非法值回退为 4\r\n"
    L"CoreLimit=4\r\n"
    L"\r\n"
    L"[Window]\r\n"
    L"; PerMonitor DPI 感知（修复高 DPI 下的坐标偏移）\r\n"
    L"DPIAware=1\r\n"
    L"; 无边框全屏模式（可通过 ToggleKey 热键切换）\r\n"
    L"Borderless=1\r\n"
    L"; 启动和分辨率变更时自动居中窗口\r\n"
    L"CenterWindow=1\r\n"
    L"; 窗口始终置顶\r\n"
    L"AlwaysOnTop=0\r\n"
    L"; 无边框切换热键虚拟键码 (0=禁用, 121=F10)\r\n"
    L"ToggleKey=121\r\n"
    L"; 窗口任务轮询间隔 ms (最小值 500, 默认 1000)\r\n"
    L"; 控制：居中检测、无边框状态维持、字号缩放检测\r\n"
    L"PollInterval=1000\r\n"
    L"\r\n"
    L"[Subtitle]\r\n"
    L"; 0 = 自动 (游戏窗口高度 / BaseHeight, 回退到屏幕高度)\r\n"
    L"; 手动设置例如：1080p 设为 1.5, 4K 设为 3.0\r\n"
    L"Scale=0\r\n"
    L"; 自动缩放基准分辨率高度 (默认 720)\r\n"
    L"BaseHeight=720\r\n"
    L"\r\n"
    L"[Debug]\r\n"
    L"; 输出诊断日志到 DSOpt.log（UTF-8 编码, 0=关闭, 1=开启）\r\n"
    L"Log=0\r\n";

static void RestoreDefaultIni(HMODULE hModule) {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(hModule, path, MAX_PATH)) return;
    wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return;
    wcscpy(dot, L".ini");

    // 如果文件已存在则跳过（保留用户修改）
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;

    // 创建 UTF-16 LE + BOM 编码的默认配置文件
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;

    // 写入 UTF-16 LE BOM (0xFFFE = U+FEFF little-endian)
    DWORD written;
    static const UINT8 kBomUtf16LE[] = { 0xFF, 0xFE };
    WriteFile(h, kBomUtf16LE, sizeof(kBomUtf16LE), &written, NULL);

    // 写入 wchar_t 内容（Windows 上 wchar_t = UTF-16 LE，无需转换）
    DWORD iniBytes = (DWORD)(wcslen(kDefaultIni) * sizeof(wchar_t));
    WriteFile(h, kDefaultIni, iniBytes, &written, NULL);

    CloseHandle(h);
}

// ===========================================================================
// 功能模块：手柄配置文件修复（Unicode 路径）
// ===========================================================================
static void FixJoypadConfig() {
    wchar_t path[MAX_PATH];
    if (!ExpandEnvironmentStringsW(L"%USERPROFILE%", path, MAX_PATH)) return;
    lstrcatW(path, L"\\Documents\\Electronic Arts\\Dead Space\\joypad.txt");

    // 确保目录存在
    wchar_t dir[MAX_PATH];
    lstrcpyW(dir, path);
    wchar_t* last = wcsrchr(dir, L'\\');
    if (last) { *last = L'\0'; MkDirRecursiveW(dir); }

    // 尝试以读模式打开文件
    HANDLE hTest = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTest != INVALID_HANDLE_VALUE) {
        CloseHandle(hTest);
        return;  // 文件存在且可读，无需修复
    }

    // 文件不存在或无法读取 → 删除可能存在的损坏文件 → 创建空文件
    DeleteFileW(path);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

// ===========================================================================
// DllMain — ASI 插件入口点
// ===========================================================================
BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // 提取 ASI 文件所在目录（Unicode）
        GetModuleFileNameW(hModule, g_asiDir, MAX_PATH);
        wchar_t* slash = wcsrchr(g_asiDir, L'\\');
        if (slash) *slash = L'\0';

        // ==== 初始化顺序很关键 — 不要随意调整 ====
        LoadConfig(hModule);
        RestoreDefaultIni(hModule);

        // 启动日志
        {
            wchar_t info[256];
            wsprintfW(info, L"[Opt] DSOpt 已加载 | 核心限制=%d | DPI感知=%d | 无边框=%d | 居中=%d | 置顶=%d | 热键=%d | 轮询=%dms | 字幕缩放=%.3f | 基准高度=%d\n",
                g_cfg.coreLimit, g_cfg.dpiAware, g_cfg.borderless, g_cfg.centerWindow,
                g_cfg.alwaysOnTop, g_cfg.toggleKey, g_pollInterval, g_fontScale, g_baseHeight);
            OptLog(info);
        }

        ApplyDPIAwareness();
        ApplyDefaultSettings();
        FixJoypadConfig();
        ApplyCoreLimit();

        void* tramp = nullptr;
        if (InstallHook(CreateWindowExA, Hooked_CreateWindowExA, &tramp))
            g_origCreate = (FnCreateWindowExA)tramp;

        InstallFontHook();
    }
    if (reason == DLL_PROCESS_DETACH) {
        OptLog(L"[Opt] DSOpt 卸载\n");
        g_workerRunning.store(false, std::memory_order_release);
        Sleep(50);
    }
    return TRUE;
}
