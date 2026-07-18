#include "pe_scanner.hpp"

#include <windows.h>

#include <cstring>

namespace Common {
namespace {

// 取得当前进程主模块的 PE 头和节表，并执行基本完整性检查。
bool GetImageHeaders(std::uint8_t*& base,
                     PIMAGE_NT_HEADERS& nt,
                     PIMAGE_SECTION_HEADER& sections) {
    base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    if (!base)
        return false;

    auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        return false;

    nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.NumberOfSections == 0 ||
        nt->FileHeader.NumberOfSections > 96 ||
        nt->OptionalHeader.SizeOfImage == 0)
        return false;

    sections = IMAGE_FIRST_SECTION(nt);
    return true;
}

} // namespace

PatternResult FindUniquePatternInCode(const std::uint8_t* pattern,
                                      const char* mask) {
    PatternResult result;
    if (!pattern || !mask)
        return result;

    const std::size_t patternLength = std::strlen(mask);
    if (patternLength == 0)
        return result;

    std::uint8_t* imageBase = nullptr;
    PIMAGE_NT_HEADERS nt = nullptr;
    PIMAGE_SECTION_HEADER sections = nullptr;
    if (!GetImageHeaders(imageBase, nt, sections))
        return result;

    const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;

    // 只扫描 IMAGE_SCN_CNT_CODE 节，避免数据区中的偶然字节序列造成误匹配。
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const IMAGE_SECTION_HEADER& section = sections[index];
        if (!(section.Characteristics & IMAGE_SCN_CNT_CODE))
            continue;

        const std::size_t rva = section.VirtualAddress;
        std::size_t size = section.Misc.VirtualSize;
        if (rva >= imageSize)
            continue;
        if (size > imageSize - rva)
            size = imageSize - rva;
        if (size < patternLength)
            continue;

        std::uint8_t* start = imageBase + rva;
        for (std::size_t offset = 0;
             offset <= size - patternLength;
             ++offset) {
            bool matched = true;
            for (std::size_t byte = 0; byte < patternLength; ++byte) {
                if (mask[byte] == 'x' &&
                    start[offset + byte] != pattern[byte]) {
                    matched = false;
                    break;
                }
            }

            if (!matched)
                continue;

            ++result.count;
            result.address = start + offset;
        }
    }

    // 为安全起见，只有唯一命中才允许调用者使用地址。
    if (result.count != 1)
        result.address = nullptr;
    return result;
}

} // namespace Common
