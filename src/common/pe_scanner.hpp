#pragma once

// =============================================================================
// pe_scanner.hpp
// -----------------------------------------------------------------------------
// 在当前游戏进程主模块的可执行代码节中搜索字节特征码。字幕修复不依赖固定
// EXE 基址，而是通过唯一特征码兼容 Steam 解包内存、GOG 和已验证的其他版本。
// =============================================================================

#include <cstddef>
#include <cstdint>

namespace Common {

struct PatternResult {
    // 仅当匹配数量恰好为 1 时 address 才非空。
    std::uint8_t* address = nullptr;

    // 实际匹配数量。调用者可用它区分“未找到”和“特征码不唯一”。
    std::size_t count = 0;
};

// mask 中 'x' 表示对应字节必须匹配，其他字符表示通配。
PatternResult FindUniquePatternInCode(const std::uint8_t* pattern,
                                      const char* mask);

} // namespace Common
