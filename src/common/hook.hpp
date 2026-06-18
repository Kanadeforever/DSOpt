#pragma once
//
// hook.hpp — 轻量级 5 字节 JMP Hook 工具集，供 ASI 插件使用
//
// 本文件提供三个层次的运行时内存修改能力：
//   1. InstallHook — 5 字节 JMP 跳转钩子（带可选的原始函数调用跳板）
//   2. PatchByte / PatchMemory — 直接写入任意字节
//   3. PatchNop — 用 NOP (0x90) 填充指定长度
//
// ===========================================================================
// InstallHook 工作原理
// ===========================================================================
// 典型的 x86 函数开头（例如 MessageBoxA）：
//   0x??   8B FF          mov edi, edi      ← 恰好 5 字节
//   0x??   55             push ebp
//   ...
//
// InstallHook 在目标地址写入：
//   E9 XX XX XX XX        jmp rel32          ← 跳到我们的 detour 函数
//
// 跳板（Trampoline）则保存原始 5 字节 + JMP 回到 target+5，
// 让 detour 函数可以继续调用原始逻辑。
//
// ===========================================================================
// 使用示例
// ===========================================================================
//   // 1. 声明原始函数指针
//   static decltype(&MessageBoxA) g_origMsgBox = MessageBoxA;
//
//   // 2. 编写 Hook 函数
//   int WINAPI Hooked_MsgBox(HWND h, LPCSTR t, LPCSTR c, UINT u) {
//       return g_origMsgBox(h, "[已被拦截]", c, u);  // 通过跳板调用原函数
//   }
//
//   // 3. 在 DllMain 中安装 Hook
//   void* tramp = nullptr;
//   InstallHook(&MessageBoxA, &Hooked_MsgBox, &tramp);
//   g_origMsgBox = (decltype(g_origMsgBox))tramp;  // tramp 即原始调用跳板
//

#include <windows.h>
#include <cstring>

// ---------------------------------------------------------------------------
// InstallHook：在 target 处写入 5 字节 JMP rel32，跳转到 detour
// ---------------------------------------------------------------------------
// 参数：
//   target          — 要拦截的目标函数地址（游戏或系统 API）
//   detour          — 我们的替代函数地址
//   trampoline_out  — [可选] 如果非空，分配一个 10 字节的可执行跳板：
//                        [原始 5 字节] + [JMP 回到 target+5]
//                      可通过函数指针调用，实现"先执行原逻辑再附加处理"
// 返回值：
//   true  — Hook 安装成功
//   false — VirtualProtect 失败（目标地址不可写）或 trampoline 分配失败
// ---------------------------------------------------------------------------
inline bool InstallHook(void* target, void* detour, void** trampoline_out = nullptr) {
    // 第一步：修改目标地址所在页为可读可写可执行
    DWORD oldProtect;
    if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    // 第二步：如果需要跳板，在堆上分配可执行内存并保存原始 5 字节
    // 跳板布局：[原始指令(5B)] [E9] [相对偏移→target+5(4B)]
    if (trampoline_out) {
        void* tramp = VirtualAlloc(nullptr, 10, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!tramp) {
            VirtualProtect(target, 5, oldProtect, &oldProtect);
            return false;
        }
        memcpy(tramp, target, 5);                     // 复制原始 5 字节指令
        uint8_t* jmpBack = (uint8_t*)tramp + 5;
        jmpBack[0] = 0xE9;                             // JMP 操作码
        int32_t relBack = (int32_t)((uint8_t*)target + 5 - jmpBack - 5);
        memcpy(jmpBack + 1, &relBack, 4);              // 计算相对偏移：跳回 target+5
        *trampoline_out = tramp;
    }

    // 第三步：在目标地址写入 JMP rel32
    // 指令格式：E9 [4 字节相对偏移 = detour - (target + 5)]
    uint8_t* p = (uint8_t*)target;
    p[0] = 0xE9;
    int32_t rel = (int32_t)((uint8_t*)detour - p - 5);
    memcpy(p + 1, &rel, 4);

    // 第四步：恢复原始页保护属性，刷新 CPU 指令缓存
    VirtualProtect(target, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, 5);
    return true;
}

// ---------------------------------------------------------------------------
// PatchByte：在目标地址写入单个字节（简单补丁，不涉及 Hook 跳转逻辑）
// ---------------------------------------------------------------------------
// 用于修改常量、标志位等单字节数据（如修改 cmp/jz 的条件码）
// 参数：
//   target — 要修改的内存地址
//   value  — 要写入的字节值
// ---------------------------------------------------------------------------
inline bool PatchByte(void* target, uint8_t value) {
    DWORD old;
    if (!VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(uint8_t*)target = value;
    VirtualProtect(target, 1, old, &old);
    return true;
}

// ---------------------------------------------------------------------------
// PatchMemory：在目标地址写入 N 字节数据
// ---------------------------------------------------------------------------
// 用于批量修改指令或数据（如整条指令替换）
// 参数：
//   target — 起始地址
//   data   — 要写入的数据指针
//   len    — 写入字节数
// ---------------------------------------------------------------------------
inline bool PatchMemory(void* target, const void* data, size_t len) {
    DWORD old;
    if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(target, data, len);
    VirtualProtect(target, len, old, &old);
    return true;
}

// ---------------------------------------------------------------------------
// PatchNop：用 NOP (0x90) 填充目标地址的 N 个字节
// ---------------------------------------------------------------------------
// 常用于禁用某条指令或整个函数调用（如跳过某个 call 或条件分支）
// 参数：
//   target — 起始地址
//   len    — 要 NOP 掉的字节数
// ---------------------------------------------------------------------------
inline bool PatchNop(void* target, size_t len) {
    DWORD old;
    if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memset(target, 0x90, len);
    VirtualProtect(target, len, old, &old);
    return true;
}
