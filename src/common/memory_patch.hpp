#pragma once

// =============================================================================
// memory_patch.hpp
// -----------------------------------------------------------------------------
// 运行时内存补丁的最小公共接口。字幕修复需要在游戏代码中写入 JMP，并为
// 手写 x86 跳板分配可执行内存。本模块只提供底层原语，不包含任何具体游戏
// 地址或字幕逻辑。
// =============================================================================

#include <cstddef>
#include <cstdint>

namespace Common {

// 检查 [address, address + length) 是否位于同一个已提交、可访问内存区域内。
bool IsReadableAddress(const void* address, std::size_t length);

// 临时修改页面保护、写入机器码并刷新指令缓存。
bool WriteCode(void* address, const void* bytes, std::size_t length);

// 分配可读、可写、可执行内存，用于存放运行时生成的 x86 跳板。
std::uint8_t* AllocateExecutable(std::size_t size);

// 释放由 AllocateExecutable 分配的完整区域。
void FreeExecutable(void* address);

} // namespace Common
