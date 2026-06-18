#pragma once
//
// pattern.hpp — 特征码（AOB / Array of Bytes）扫描器，供 ASI 插件使用
//
// ===========================================================================
// 使用场景
// ===========================================================================
// 游戏不同版本（Steam 正版 / 破解版 / 汉化版）的同一个函数地址可能不同。
// 通过扫描固定的机器码特征序列（AOB），可以在运行时动态定位目标代码，
// 无需硬编码地址，实现跨版本兼容。
//
// ===========================================================================
// 使用示例
// ===========================================================================
//   查找 "push ebp; mov ebp, esp; sub esp, 0x40" 对应的机器码：
//     uint8_t sig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x40 };
//     void* found = FindPatternInGame(sig, "xxxxxx");
//     if (found) { InstallHook(found, ...); }
//
// ===========================================================================
// Mask 语法
// ===========================================================================
//   'x' — 该字节必须精确匹配
//   其他字符（通常用 '?'）— 通配符，跳过该字节
//   例如："x????x" 匹配首尾固定、中间 4 字节任意的序列
//

#include <windows.h>
#include <cstring>

// ---------------------------------------------------------------------------
// FindPattern：在指定内存范围内扫描特征码
// ---------------------------------------------------------------------------
// 参数：
//   start   — 搜索起始地址
//   size    — 搜索范围大小（字节数）
//   pattern — 目标字节序列
//   mask    — 匹配掩码，'x' = 必须匹配，其他 = 通配
// 返回值：
//   匹配位置的指针，未找到返回 nullptr
//
// 算法：朴素线性扫描（非 KMP/Boyer-Moore）。
//       对于 PE 的 .text 段（通常 2~10 MB），线性扫描足够快
//       （通常在 1ms 以内完成）。
// ---------------------------------------------------------------------------
inline void* FindPattern(const uint8_t* start, size_t size,
                         const uint8_t* pattern, const char* mask) {
    size_t patLen = strlen(mask);
    if (size < patLen) return nullptr;               // 搜索范围小于特征码长度，不可能匹配

    for (size_t i = 0; i <= size - patLen; i++) {
        bool found = true;
        for (size_t j = 0; j < patLen && found; j++) {
            // 仅掩码为 'x' 的字节参与比较，非 'x' 字节直接跳过
            if (mask[j] == 'x' && start[i + j] != pattern[j])
                found = false;
        }
        if (found) return (void*)(start + i);         // 返回首次匹配的地址
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// FindPatternInGame：在整个游戏主 EXE 镜像内扫描特征码
// ---------------------------------------------------------------------------
// 调用时机：
//   DllMain(DLL_PROCESS_ATTACH) 中即可安全调用，
//   此时 EXE 的 PE 映像已经由系统加载器完整映射到进程空间。
//
// 实现细节：
//   1. GetModuleHandle(NULL) 获取游戏主 EXE 的加载基址
//   2. 解析 PE 头（DOS Header → NT Headers）获取完整映像大小
//   3. 在整个映像范围内调用 FindPattern 扫描
//
// 注意：此函数扫描整个 PE 映像（包括 .text/.rdata/.data 等全部节），
//       不只是代码段。如果需要限定代码段，请使用节表迭代方式。
// ---------------------------------------------------------------------------
inline void* FindPatternInGame(const uint8_t* pattern, const char* mask) {
    HMODULE hExe = GetModuleHandle(nullptr);           // NULL = 主 EXE 基址
    if (!hExe) return nullptr;

    auto* base = (uint8_t*)hExe;

    // 验证 DOS 头：e_magic 必须是 "MZ"
    auto* dos = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

    // 通过 e_lfanew 定位 NT 头
    auto* nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    // SizeOfImage = 整个 PE 在内存中的映射大小（已对齐到 SectionAlignment）
    size_t imageSize = nt->OptionalHeader.SizeOfImage;
    return FindPattern(base, imageSize, pattern, mask);
}
