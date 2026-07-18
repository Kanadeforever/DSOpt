#include "process_fixes.hpp"

#include <windows.h>

#include <cwchar>

namespace ProcessFixes {
namespace {

void Log(Common::LogCallback logger, const wchar_t* message) {
    if (logger)
        logger(message);
}

} // namespace

void ApplyDpiAwareness(bool enabled, Common::LogCallback logger) {
    if (!enabled)
        return;

    // Windows 10 优先使用 Per-Monitor V2。通过 GetProcAddress 动态解析，确保
    // 同一构建仍能在不导出该 API 的旧版 Windows 上启动。
    using SetContextFn = BOOL (WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto setContext = reinterpret_cast<SetContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 的公开常量值为 -4。
            if (setContext(reinterpret_cast<HANDLE>(
                    static_cast<INT_PTR>(-4)))) {
                Log(logger, L"[进程] 已启用每显示器 V2 DPI 感知。\n");
                return;
            }
        }
    }

    // Windows 8.1 的回退路径：PROCESS_PER_MONITOR_DPI_AWARE = 2。
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        using SetAwarenessFn = HRESULT (WINAPI*)(int);
        auto setAwareness = reinterpret_cast<SetAwarenessFn>(
            GetProcAddress(shcore, "SetProcessDpiAwareness"));
        const HRESULT result = setAwareness ? setAwareness(2) : E_NOTIMPL;
        FreeLibrary(shcore);

        // E_ACCESSDENIED 通常表示进程清单或更早代码已经设置过 DPI 感知，
        // 此时不应继续用较低级 API 覆盖。
        if (SUCCEEDED(result) || result == E_ACCESSDENIED) {
            Log(logger, L"[进程] 已启用每显示器 DPI 感知。\n");
            return;
        }
    }

    // 最低级回退：系统 DPI 感知。
    if (SetProcessDPIAware())
        Log(logger, L"[进程] 已启用系统级 DPI 感知（兼容回退）。\n");
    else
        Log(logger, L"[进程] 无法设置 DPI 感知，继续使用系统当前状态。\n");
}

void ApplyCoreLimit(int coreLimit, Common::LogCallback logger) {
    if (coreLimit == 0)
        return;
    if (coreLimit < 1 || coreLimit > 8)
        coreLimit = 4;

    DWORD_PTR processMask = 0;
    DWORD_PTR systemMask = 0;
    if (!GetProcessAffinityMask(
            GetCurrentProcess(), &processMask, &systemMask) ||
        processMask == 0) {
        Log(logger, L"[进程] 无法读取进程 CPU 亲和性掩码。\n");
        return;
    }
    (void)systemMask;

    // 不再假设可用处理器一定从编号 0 连续排列。应从当前进程已经允许的
    // processMask 中依次选择前 N 个可用位，否则在受限环境中可能得到零掩码。
    DWORD_PTR selected = 0;
    int selectedCount = 0;
    for (unsigned bit = 0;
         bit < sizeof(DWORD_PTR) * 8 && selectedCount < coreLimit;
         ++bit) {
        const DWORD_PTR candidate = static_cast<DWORD_PTR>(1) << bit;
        if (processMask & candidate) {
            selected |= candidate;
            ++selectedCount;
        }
    }

    if (selected == 0 ||
        !SetProcessAffinityMask(GetCurrentProcess(), selected)) {
        Log(logger, L"[进程] 应用 CPU 核心限制失败。\n");
        return;
    }

    wchar_t message[128] = {};
    swprintf_s(message, sizeof(message) / sizeof(message[0]),
        L"[进程] 已限制为当前允许范围内的 %d 个逻辑处理器。\n",
        selectedCount);
    Log(logger, message);
}

} // namespace ProcessFixes
