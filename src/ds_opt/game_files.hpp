#pragma once

// =============================================================================
// game_files.hpp
// -----------------------------------------------------------------------------
// 管理死亡空间的用户配置文件：
// - settings.txt：只补充缺失的安全默认项；
// - joypad.txt：按照模组既定行为，文件无法读取时删除并重建为空文件。
// =============================================================================

#include "../common/logging_types.hpp"

namespace GameFiles {

void ApplyDefaultSettings(Common::LogCallback logger);
void EnsureJoypadConfig(Common::LogCallback logger);

} // namespace GameFiles
