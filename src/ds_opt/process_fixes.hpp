#pragma once

// =============================================================================
// process_fixes.hpp
// -----------------------------------------------------------------------------
// 进程级兼容性修复：DPI 感知和 CPU 亲和性限制。
// =============================================================================

#include "../common/logging_types.hpp"

namespace ProcessFixes {

void ApplyDpiAwareness(bool enabled, Common::LogCallback logger);
void ApplyCoreLimit(int coreLimit, Common::LogCallback logger);

} // namespace ProcessFixes
