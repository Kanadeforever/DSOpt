#pragma once

// =============================================================================
// app_config.hpp
// -----------------------------------------------------------------------------
// DSOpt.ini 的数据模型和读取入口。各功能模块只接收自己的配置结构，不直接
// 读取 INI，从而避免配置解析逻辑散落在多个源文件中。
// =============================================================================

#include <windows.h>

#include "subtitle_scaling.hpp"
#include "window_manager.hpp"

namespace AppConfig {

struct CrashFixConfig {
    int coreLimit = 4;
};

struct DebugConfig {
    bool log = false;
};

struct Config {
    CrashFixConfig crash;
    WindowManager::Config window;
    SubtitleScaling::Config subtitle;
    DebugConfig debug;
};

// 首次运行时创建带详细中文说明的默认 INI。
bool EnsureDefaultIni(HMODULE module);

// 读取并校验所有配置项；非法值使用安全默认值。
Config Load(HMODULE module);

} // namespace AppConfig
