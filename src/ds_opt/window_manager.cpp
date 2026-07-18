#include "window_manager.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>

namespace WindowManager {
namespace {

// =============================================================================
// 模块状态
// =============================================================================
Config g_config;
Common::LogCallback g_log = nullptr;
bool g_configured = false;

HWND g_window = nullptr;
bool g_borderlessActive = false;
bool g_savedStateValid = false;
LONG_PTR g_savedStyle = 0;
LONG_PTR g_savedExStyle = 0;
RECT g_savedRect = {};
RECT g_lastRect = {};
bool g_initialBorderlessApplied = false;
bool g_topmostAppliedByUs = false;
bool g_keyWasDown = false;
ULONGLONG g_nextFindTick = 0;
ULONGLONG g_nextSlowTick = 0;

void Log(const wchar_t* message) {
    if (g_log)
        g_log(message);
}

bool RectEquals(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top &&
           a.right == b.right && a.bottom == b.bottom;
}

// 根据窗口所在显示器取得完整显示器区域或工作区。
// 使用 MONITOR_DEFAULTTONEAREST，窗口暂时位于屏幕外时仍能得到合理结果。
bool GetMonitorRect(HWND window, bool workArea, RECT& out) {
    const HMONITOR monitor = MonitorFromWindow(
        window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
        return false;

    out = workArea ? info.rcWork : info.rcMonitor;
    return true;
}

// 判断窗口是否已经是游戏自己创建的独占/伪独占全屏窗口。
// 对这种窗口重复移除边框没有意义，也可能干扰游戏的显示模式切换。
bool IsLikelyExclusiveFullscreen(HWND window) {
    if (!IsWindow(window))
        return false;

    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if (!(style & WS_POPUP) || (style & WS_CAPTION))
        return false;

    RECT windowRect = {};
    RECT monitorRect = {};
    return GetWindowRect(window, &windowRect) &&
           GetMonitorRect(window, false, monitorRect) &&
           RectEquals(windowRect, monitorRect);
}

// 当前游戏窗口被销毁或替换后，清空所有与旧 HWND 绑定的状态。
void ResetWindowState() {
    g_window = nullptr;
    g_borderlessActive = false;
    g_savedStateValid = false;
    g_savedStyle = 0;
    g_savedExStyle = 0;
    SetRectEmpty(&g_savedRect);
    SetRectEmpty(&g_lastRect);
    g_initialBorderlessApplied = false;
    g_topmostAppliedByUs = false;
    g_keyWasDown = false;
}

struct FindContext {
    DWORD processId = 0;
    HWND foreground = nullptr;
    HWND best = nullptr;
    std::uint64_t bestScore = 0;
};

// 判断标准 Windows 对话框类。对话框不直接排除，因为极端情况下它可能是
// 进程唯一可见顶层窗口；但它的优先级应低于正常游戏渲染窗口。
bool IsDialogClass(HWND window) {
    wchar_t className[64] = {};
    if (GetClassNameW(window, className,
            static_cast<int>(sizeof(className) / sizeof(className[0]))) <= 0)
        return false;
    return std::wcscmp(className, L"#32770") == 0;
}

// 为候选窗口计算分层评分。
//
// 这里“面积”只作为同类候选之间的最后比较项，并不存在“窗口必须大于某个
// 分辨率”的门槛。因此游戏以 640x480 启动时，只要它是当前进程的正常顶层
// 窗口，就会被正常识别，不会因为面积小而被忽略。
std::uint64_t ScoreWindow(HWND window,
                          const RECT& windowRect,
                          const RECT& clientRect,
                          LONG_PTR style,
                          HWND foreground) {
    const LONG width = windowRect.right - windowRect.left;
    const LONG height = windowRect.bottom - windowRect.top;
    const LONG clientWidth = clientRect.right - clientRect.left;
    const LONG clientHeight = clientRect.bottom - clientRect.top;

    const std::uint64_t area = static_cast<std::uint64_t>(width) *
                               static_cast<std::uint64_t>(height);
    std::uint64_t score = area;

    // 高位作为“类别优先级”，面积无法跨越这些类别权重。
    if (window == foreground)
        score += (std::uint64_t{1} << 62);
    if (!IsDialogClass(window))
        score += (std::uint64_t{1} << 61);
    if (clientWidth >= 320 && clientHeight >= 200)
        score += (std::uint64_t{1} << 60);
    if (style & (WS_CAPTION | WS_POPUP))
        score += (std::uint64_t{1} << 59);

    return score;
}

BOOL CALLBACK EnumWindowCandidate(HWND window, LPARAM parameter) {
    auto* context = reinterpret_cast<FindContext*>(parameter);

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);

    // 只考虑当前游戏进程的可见、无所有者顶层窗口。
    if (processId != context->processId ||
        !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != nullptr)
        return TRUE;

    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((style & WS_CHILD) || (exStyle & WS_EX_TOOLWINDOW))
        return TRUE;

    RECT windowRect = {};
    RECT clientRect = {};
    if (!GetWindowRect(window, &windowRect) ||
        !GetClientRect(window, &clientRect))
        return TRUE;

    const LONG width = windowRect.right - windowRect.left;
    const LONG height = windowRect.bottom - windowRect.top;
    if (width <= 0 || height <= 0)
        return TRUE;

    const std::uint64_t score = ScoreWindow(
        window, windowRect, clientRect, style, context->foreground);
    if (!context->best || score > context->bestScore) {
        context->best = window;
        context->bestScore = score;
    }
    return TRUE;
}

HWND FindGameWindow() {
    FindContext context;
    context.processId = GetCurrentProcessId();

    // 前台窗口属于当前进程时给予最高优先级；否则它只作为普通 HWND 传入。
    context.foreground = GetForegroundWindow();
    DWORD foregroundProcess = 0;
    if (context.foreground)
        GetWindowThreadProcessId(context.foreground, &foregroundProcess);
    if (foregroundProcess != context.processId)
        context.foreground = nullptr;

    EnumWindows(EnumWindowCandidate, reinterpret_cast<LPARAM>(&context));
    return context.best;
}

// SetWindowLongPtrW 返回旧值；旧值本身可能合法地为 0，因此必须配合
// GetLastError 判断调用是否真正失败。
bool SetWindowLongValue(HWND window, int index, LONG_PTR value) {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(window, index, value);
    return previous != 0 || GetLastError() == ERROR_SUCCESS;
}

bool CenterWindow(HWND window) {
    RECT windowRect = {};
    RECT workRect = {};
    if (!GetWindowRect(window, &windowRect) ||
        !GetMonitorRect(window, true, workRect))
        return false;

    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    const int workWidth = workRect.right - workRect.left;
    const int workHeight = workRect.bottom - workRect.top;

    // 多显示器坐标允许为负数，不能像旧实现那样强制限制到 0。
    const int x = width >= workWidth
        ? workRect.left
        : workRect.left + (workWidth - width) / 2;
    const int y = height >= workHeight
        ? workRect.top
        : workRect.top + (workHeight - height) / 2;

    if (!SetWindowPos(window, nullptr, x, y, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE))
        return false;

    GetWindowRect(window, &g_lastRect);
    return true;
}

bool ApplyBorderlessGeometry(HWND window) {
    RECT monitorRect = {};
    if (!GetMonitorRect(window, false, monitorRect))
        return false;

    const HWND insertAfter = g_config.alwaysOnTop ? HWND_TOPMOST : HWND_TOP;
    return SetWindowPos(window, insertAfter,
        monitorRect.left, monitorRect.top,
        monitorRect.right - monitorRect.left,
        monitorRect.bottom - monitorRect.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE) != FALSE;
}

bool EnableBorderless(HWND window) {
    if (!IsWindow(window) || IsLikelyExclusiveFullscreen(window))
        return false;

    // 第一次启用时保存原始样式和位置，供热键恢复。
    if (!g_borderlessActive) {
        g_savedStyle = GetWindowLongPtrW(window, GWL_STYLE);
        g_savedExStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        g_savedStateValid = GetWindowRect(window, &g_savedRect) != FALSE;
        if (!g_savedStateValid)
            return false;
    }

    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    constexpr LONG_PTR removeStyle =
        WS_CAPTION | WS_BORDER | WS_DLGFRAME | WS_THICKFRAME |
        WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
    constexpr LONG_PTR removeExStyle =
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
        WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;

    const bool styleChanged = SetWindowLongValue(
        window, GWL_STYLE, style & ~removeStyle);
    const bool exStyleChanged = SetWindowLongValue(
        window, GWL_EXSTYLE,
        (exStyle & ~removeExStyle) | WS_EX_APPWINDOW);
    const bool geometryChanged = styleChanged && exStyleChanged &&
        ApplyBorderlessGeometry(window);

    if (!geometryChanged) {
        // 任一步失败都恢复到保存状态，避免留下“无边框样式但尺寸未更新”的半状态。
        SetWindowLongValue(window, GWL_STYLE, g_savedStyle);
        SetWindowLongValue(window, GWL_EXSTYLE, g_savedExStyle);
        SetWindowPos(window, nullptr,
            g_savedRect.left, g_savedRect.top,
            g_savedRect.right - g_savedRect.left,
            g_savedRect.bottom - g_savedRect.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW |
            SWP_NOZORDER | SWP_NOACTIVATE);
        return false;
    }

    g_borderlessActive = true;
    return true;
}

bool DisableBorderless(HWND window) {
    if (!IsWindow(window) ||
        !g_borderlessActive ||
        !g_savedStateValid)
        return false;

    const bool styleRestored = SetWindowLongValue(
        window, GWL_STYLE, g_savedStyle);
    const bool exStyleRestored = SetWindowLongValue(
        window, GWL_EXSTYLE, g_savedExStyle);
    const HWND insertAfter = g_config.alwaysOnTop
        ? HWND_TOPMOST
        : HWND_NOTOPMOST;
    const bool moved = SetWindowPos(window, insertAfter,
        g_savedRect.left, g_savedRect.top,
        g_savedRect.right - g_savedRect.left,
        g_savedRect.bottom - g_savedRect.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE) != FALSE;

    if (styleRestored && exStyleRestored) {
        g_borderlessActive = false;
        g_topmostAppliedByUs = g_config.alwaysOnTop;
        GetWindowRect(window, &g_lastRect);
    }
    return styleRestored && exStyleRestored && moved;
}

// 游戏切换分辨率或显示模式时可能重新写入窗口样式和尺寸。无边框模式下
// 周期检查并修复这些变化，保证窗口继续覆盖当前显示器。
void MaintainBorderless(HWND window) {
    if (!g_borderlessActive)
        return;

    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    RECT current = {};
    RECT monitor = {};
    const bool wrongGeometry = GetWindowRect(window, &current) &&
        GetMonitorRect(window, false, monitor) &&
        !RectEquals(current, monitor);

    if ((style & WS_CAPTION) || wrongGeometry) {
        const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        constexpr LONG_PTR removeStyle =
            WS_CAPTION | WS_BORDER | WS_DLGFRAME | WS_THICKFRAME |
            WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
        constexpr LONG_PTR removeExStyle =
            WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
            WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;

        if (SetWindowLongValue(window, GWL_STYLE, style & ~removeStyle) &&
            SetWindowLongValue(window, GWL_EXSTYLE,
                (exStyle & ~removeExStyle) | WS_EX_APPWINDOW))
            ApplyBorderlessGeometry(window);
    }
}

void MaintainTopmost(HWND window) {
    const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const bool isTopmost = (exStyle & WS_EX_TOPMOST) != 0;

    if (g_config.alwaysOnTop && !isTopmost) {
        if (SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
            g_topmostAppliedByUs = true;
    } else if (!g_config.alwaysOnTop &&
               g_topmostAppliedByUs &&
               isTopmost) {
        if (SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
            g_topmostAppliedByUs = false;
    }
}

void PollToggleKey() {
    if (!g_window || g_config.toggleKey == 0)
        return;

    // 只有游戏处于前台时才响应热键，避免用户在其他程序中释放相同按键时切换。
    if (GetForegroundWindow() != g_window) {
        g_keyWasDown = false;
        return;
    }

    const bool down = (GetAsyncKeyState(g_config.toggleKey) & 0x8000) != 0;
    if (g_keyWasDown && !down) {
        if (g_borderlessActive) {
            if (DisableBorderless(g_window))
                Log(L"[窗口] 已通过热键关闭无边框模式。\n");
        } else {
            if (EnableBorderless(g_window))
                Log(L"[窗口] 已通过热键启用无边框模式。\n");
        }
    }
    g_keyWasDown = down;
}

void OnWindowAcquired(HWND window) {
    g_window = window;
    g_borderlessActive = false;
    g_savedStateValid = false;
    g_initialBorderlessApplied = false;
    g_topmostAppliedByUs = false;
    g_keyWasDown = false;
    GetWindowRect(window, &g_lastRect);
    g_nextSlowTick = 0;

    if (g_config.centerWindow && !g_config.borderless)
        CenterWindow(window);

    RECT client = {};
    GetClientRect(window, &client);
    wchar_t message[192] = {};
    swprintf_s(message, sizeof(message) / sizeof(message[0]),
        L"[窗口] 已识别游戏主窗口：句柄=0x%p，客户区=%ldx%ld。\n",
        window,
        client.right - client.left,
        client.bottom - client.top);
    Log(message);
}

void RunSlowTasks() {
    if (!g_window)
        return;

    if (!g_initialBorderlessApplied) {
        if (!g_config.borderless) {
            g_initialBorderlessApplied = true;
        } else if (EnableBorderless(g_window)) {
            g_initialBorderlessApplied = true;
            Log(L"[窗口] 已应用启动无边框模式。\n");
        }
    }

    MaintainBorderless(g_window);
    MaintainTopmost(g_window);

    // 普通窗口模式下，仅在窗口尺寸变化时重新居中，避免每次轮询都移动窗口。
    if (g_config.centerWindow && !g_borderlessActive) {
        RECT current = {};
        if (GetWindowRect(g_window, &current)) {
            const int width = current.right - current.left;
            const int height = current.bottom - current.top;
            const int oldWidth = g_lastRect.right - g_lastRect.left;
            const int oldHeight = g_lastRect.bottom - g_lastRect.top;

            if (width != oldWidth || height != oldHeight)
                CenterWindow(g_window);
            else
                g_lastRect = current;
        }
    }
}

} // namespace

void Configure(const Config& config, Common::LogCallback logger) {
    g_config = config;
    g_config.pollIntervalMs = std::clamp(
        g_config.pollIntervalMs, 100, 60000);
    if (g_config.toggleKey < 0 || g_config.toggleKey > 255)
        g_config.toggleKey = VK_F10;

    g_log = logger;
    g_configured = true;
    ResetWindowState();
}

void Update() {
    if (!g_configured)
        return;

    const ULONGLONG now = GetTickCount64();

    // 游戏重新创建窗口时，旧 HWND 会失效。检测后重新进入枚举流程。
    if (g_window && !IsWindow(g_window))
        ResetWindowState();

    // 不 Hook CreateWindowExA：每 250ms 枚举一次只发生在尚未取得窗口时，
    // 成功后不再枚举，因此不会产生持续性能负担。
    if (!g_window && now >= g_nextFindTick) {
        g_nextFindTick = now + 250;
        if (HWND found = FindGameWindow())
            OnWindowAcquired(found);
    }

    if (!g_window)
        return;

    PollToggleKey();
    if (now >= g_nextSlowTick) {
        g_nextSlowTick = now +
            static_cast<ULONGLONG>(g_config.pollIntervalMs);
        RunSlowTasks();
    }
}

} // namespace WindowManager
