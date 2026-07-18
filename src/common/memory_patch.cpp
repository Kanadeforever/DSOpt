#include "memory_patch.hpp"

#include <windows.h>

#include <cstring>

namespace Common {

bool IsReadableAddress(const void* address, std::size_t length) {
    if (!address || length == 0)
        return false;

    MEMORY_BASIC_INFORMATION info = {};
    if (!VirtualQuery(address, &info, sizeof(info)) ||
        info.State != MEM_COMMIT ||
        (info.Protect & PAGE_GUARD) ||
        (info.Protect & PAGE_NOACCESS))
        return false;

    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(address);
    const std::uintptr_t end = begin + length;
    const std::uintptr_t regionEnd =
        reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;

    // end < begin 表示无符号地址加法发生溢出；同时要求整个范围落在同一内存区。
    return end >= begin && end <= regionEnd;
}

bool WriteCode(void* address, const void* bytes, std::size_t length) {
    if (!address || !bytes || length == 0)
        return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(address, length, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    std::memcpy(address, bytes, length);
    FlushInstructionCache(GetCurrentProcess(), address, length);

    DWORD ignored = 0;
    VirtualProtect(address, length, oldProtect, &ignored);

    // 一旦 memcpy 完成，调用者就必须把补丁视为“已经生效”。即使恢复页面保护
    // 失败，也不能返回 false 后释放跳板，否则目标代码仍可能跳入已释放内存。
    return true;
}

std::uint8_t* AllocateExecutable(std::size_t size) {
    return static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
}

void FreeExecutable(void* address) {
    if (address)
        VirtualFree(address, 0, MEM_RELEASE);
}

} // namespace Common
