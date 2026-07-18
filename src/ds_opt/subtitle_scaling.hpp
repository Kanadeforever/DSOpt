#pragma once

// =============================================================================
// subtitle_scaling.hpp
// -----------------------------------------------------------------------------
// 《死亡空间 1》字幕完整排版链修复的公共接口。
//
// 原版在高于 720p 时没有让字幕预排版、自动换行和最终绘制共同使用正确的
// 分辨率倍率，导致字体相对变小、一行容纳过多文字，或在只放大最终字形后
// 出现第三行被裁剪。该模块同时修正：
// - 字幕预排版与可容纳行数计算；
// - 自动换行的字形测量；
// - 最终行距和字形绘制。
// =============================================================================

#include "../common/logging_types.hpp"

namespace SubtitleScaling {

struct Config {
    // 总开关。关闭时模块不会扫描或修改游戏代码。
    bool enabled = true;

    // 开发基准纵向分辨率。死亡空间 1 的目标基准为 720。
    int baseHeight = 720;

    // true：720p 及以下保留原版大小；false：低分辨率也严格等比例缩放。
    bool onlyAboveBase = true;

    // 0 表示自动使用“实际渲染高度 / baseHeight”；正数表示固定倍率。
    float manualScale = 0.0f;
};

// 保存配置和日志回调。不会在此函数内扫描或写入游戏代码。
void Configure(const Config& config, Common::LogCallback logger);

// 由 DSOpt 主工作线程周期调用：
// - 未安装时尝试等待 Steam 解包并安装两条字幕 Hook；
// - 已安装时仅做低频状态记录，真正倍率在字幕 Hook 内同步刷新。
void Update();

// 返回两条字幕 Hook 是否已经完整安装。
bool IsInstalled();

} // namespace SubtitleScaling
