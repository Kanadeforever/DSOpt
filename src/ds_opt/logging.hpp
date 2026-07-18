#pragma once

// =============================================================================
// logging.hpp
// -----------------------------------------------------------------------------
// DSOpt 统一日志模块。运行时日志内容必须使用简体中文，并以 UTF-8 BOM 文件
// 写入 DSOpt.log，方便 Windows 记事本和调试工具直接识别编码。
// =============================================================================

#include <windows.h>

namespace Logging {

void Initialize(HMODULE module, bool enabled);
bool IsEnabled();
void Write(const wchar_t* message);

} // namespace Logging
