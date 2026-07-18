// =============================================================================
// DSOpt.asi — 《死亡空间》(2008) 综合修复与现代化模组
// -----------------------------------------------------------------------------
// 本文件只负责 DLL 生命周期和各模块调度，不包含具体功能实现。
//
// 设计原则：
// 1. DllMain 中只执行最小、不可避免的加载器安全操作；
// 2. 配置读取、文件 I/O、AOB 扫描、窗口操作和内存补丁全部放到工作线程；
// 3. 字幕、窗口、配置、日志、游戏文件和进程修复保持独立模块边界；
// 4. 运行时日志统一为简体中文。
// =============================================================================

#include <windows.h>

#include <atomic>
#include <cwchar>

#include "app_config.hpp"
#include "game_files.hpp"
#include "logging.hpp"
#include "process_fixes.hpp"
#include "subtitle_scaling.hpp"
#include "window_manager.hpp"

namespace {

#if !defined(DSOPT_ALLOW_HOST_SYNTAX)
static_assert(sizeof(void*) == 4,
    "DSOpt 必须构建为 32 位 x86 ASI 模块。");
#endif

std::atomic<bool> g_running{true};
HMODULE g_module = nullptr;

void LogConfiguration(const AppConfig::Config& config) {
    wchar_t message[640] = {};
    swprintf_s(message, sizeof(message) / sizeof(message[0]),
        L"[DSOpt] 已加载：CPU核心限制=%d，DPI感知=%d，无边框=%d，"
        L"窗口居中=%d，始终置顶=%d，切换热键=%d，窗口维护周期=%d毫秒，"
        L"字幕完整链修复=%d，字幕基准高度=%d，仅修复高于基准=%d，"
        L"手动字幕倍率=%.3f。\n",
        config.crash.coreLimit,
        config.window.dpiAware,
        config.window.borderless,
        config.window.centerWindow,
        config.window.alwaysOnTop,
        config.window.toggleKey,
        config.window.pollIntervalMs,
        config.subtitle.enabled,
        config.subtitle.baseHeight,
        config.subtitle.onlyAboveBase,
        config.subtitle.manualScale);
    Logging::Write(message);
}

DWORD WINAPI BootstrapThread(LPVOID) {
    // 字幕模块会在游戏代码中安装长期存在的 JMP 跳板，因此 ASI 不支持热卸载。
    // 将模块固定到进程生命周期，可防止第三方加载器提前卸载 DLL 后跳入无效地址。
    HMODULE pinned = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&g_module), &pinned);

    // 首次运行先生成默认 INI，再按同一份可见默认值读取配置。
    AppConfig::EnsureDefaultIni(g_module);
    const AppConfig::Config config = AppConfig::Load(g_module);

    Logging::Initialize(g_module, config.debug.log);
    LogConfiguration(config);

    // DPI 感知越早设置越好，应尽可能发生在游戏主窗口创建前。
    ProcessFixes::ApplyDpiAwareness(
        config.window.dpiAware, Logging::Write);
    ProcessFixes::ApplyCoreLimit(
        config.crash.coreLimit, Logging::Write);

    // 用户配置文件修复不依赖游戏窗口，可以在模块初始化阶段一次性执行。
    GameFiles::ApplyDefaultSettings(Logging::Write);
    GameFiles::EnsureJoypadConfig(Logging::Write);

    SubtitleScaling::Configure(config.subtitle, Logging::Write);
    WindowManager::Configure(config.window, Logging::Write);

    // 主循环采用 50ms 周期：
    // - 字幕模块未安装时等待 Steam 代码解包；安装后只记录已应用倍率；
    // - 窗口模块负责热键和按配置周期执行的慢速维护。
    while (g_running.load(std::memory_order_acquire)) {
        SubtitleScaling::Update();
        WindowManager::Update();
        Sleep(50);
    }
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);

        // DllMain 不进行文件读取、AOB 扫描或窗口操作。工作线程一旦开始执行，
        // 系统通常已经释放加载器锁；CreateThread 句柄立即关闭以避免句柄泄漏。
        HANDLE thread = CreateThread(
            nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        // 加载器锁期间不得等待线程、Sleep、写日志或恢复仍可能被执行的 Hook。
        // 模块已被固定到进程生命周期，此分支主要用于正常进程结束。
        g_running.store(false, std::memory_order_release);
    }
    return TRUE;
}
