#pragma once

// =============================================================================
// file_utils.hpp
// -----------------------------------------------------------------------------
// 通用文件与路径辅助函数。这里集中处理：
// 1. ASI 模块路径查询；
// 2. 路径拼接和扩展名替换；
// 3. 多级目录创建；
// 4. 有大小上限的完整文件读取；
// 5. 通过临时文件进行的原子写入。
//
// 将这些操作集中在一个模块中，可以避免 settings.txt、日志和配置文件模块
// 各自实现一套略有差异的路径/文件逻辑。
// =============================================================================

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Common {

// 返回指定模块的完整路径。失败时返回空字符串。
std::wstring GetModulePath(HMODULE module);

// 返回指定模块所在目录，不包含末尾反斜杠。失败时返回空字符串。
std::wstring GetModuleDirectory(HMODULE module);

// 替换路径的扩展名；若路径没有扩展名，则在末尾追加 extension。
std::wstring ReplaceExtension(const std::wstring& path,
                              const wchar_t* extension);

// 安全拼接两个 Windows 路径片段，自动处理连接处的斜杠。
std::wstring JoinPath(const std::wstring& left,
                      const std::wstring& right);

// 创建完整目录树。目录已经存在也视为成功。
bool EnsureDirectoryTree(const std::wstring& directory);

// 完整读取文件到 out。
// maximumSize 用于防止异常大文件造成不受控内存分配。
bool ReadFileBytes(const std::wstring& path,
                   std::vector<std::uint8_t>& out,
                   std::size_t maximumSize);

// 原子写入文件：先写入同目录临时文件并刷新，再替换目标文件。
// 这样可以避免游戏或系统中途退出时留下半写入文件。
bool WriteFileBytesAtomic(const std::wstring& path,
                          const void* data,
                          std::size_t size);

} // namespace Common
