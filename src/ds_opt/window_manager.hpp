#pragma once

// =============================================================================
// window_manager.hpp
// -----------------------------------------------------------------------------
// 游戏主窗口识别、居中、无边框、置顶和热键切换模块。
//
// 本模块不再修改系统 CreateWindowExA 函数入口。它通过枚举当前进程的顶层
// 窗口取得游戏窗口，可避免跨 Windows 版本复制不完整指令造成的崩溃风险。
// =============================================================================

#include "../common/logging_types.hpp"

namespace WindowManager {

struct Config {
    bool dpiAware = true;
    bool borderless = true;
    bool centerWindow = true;
    bool alwaysOnTop = false;
    int toggleKey = 121;       // 默认 F10。
    int pollIntervalMs = 1000; // 慢速窗口维护周期。
};

void Configure(const Config& config, Common::LogCallback logger);
void Update();

} // namespace WindowManager
