#include "subtitle_scaling.hpp"

#include "../common/memory_patch.hpp"
#include "../common/pe_scanner.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace SubtitleScaling {
namespace {

#if !defined(DSOPT_ALLOW_HOST_SYNTAX)
static_assert(sizeof(void*) == 4,
    "字幕 Hook 必须构建为 32 位 x86 模块。");
#endif

// =============================================================================
// 模块状态与跨线程共享数据
// =============================================================================
Config g_config;
Common::LogCallback g_log = nullptr;
bool g_configured = false;
bool g_installed = false;
bool g_waitMessageWritten = false;
ULONGLONG g_nextInstallAttempt = 0;

// 跳板中的 x86 指令直接把这 4 个字节当作 IEEE-754 float 使用。
// 变量按 4 字节对齐，x86 对齐的 32 位读写具备原子性。
alignas(4) volatile LONG g_scaleBits = 0x3F800000; // 1.0f

// 预排版 Hook 每次真正使用字幕倍率前，都会同步读取游戏实际渲染高度并更新：
// - g_scaleBits：本次排版和后续绘制共同使用的倍率；
// - g_appliedHeight：用于日志记录“最近一次实际应用的高度”。
//
// 重要：工作线程不再在分辨率切换时直接改写 g_scaleBits。旧版从 640x480
// 切到 4K 时，工作线程可能先把绘制倍率改成 3.0，而字幕缓存仍是按 1.0
// 预排版的，导致再次截断。现在倍率只在预排版入口同步更新，从根本上保证
// “缓存布局”和“最终绘制”始终使用同一代倍率。
alignas(4) volatile LONG g_appliedHeight = 0;

// 这些浮点常量由运行时生成的 SSE 指令通过绝对地址读取。
alignas(4) float g_baseHeightFloat = 720.0f;
alignas(4) float g_minScale = 0.25f;
alignas(4) float g_maxScale = 8.0f;

volatile std::uint16_t* g_renderHeight = nullptr;
std::uint16_t g_lastLoggedHeight = 0;
LONG g_lastLoggedScaleBits = 0x3F800000;

std::uint8_t* g_layoutHookSite = nullptr;
std::uint8_t g_layoutOriginal[5] = {};
std::uint8_t* g_layoutTrampoline = nullptr;

std::uint8_t* g_drawHookSite = nullptr;
std::uint8_t g_drawOriginal[20] = {};
std::uint8_t* g_drawTrampoline = nullptr;

template <typename T, std::size_t N>
constexpr std::size_t CountOf(T (&)[N]) {
    return N;
}

// =============================================================================
// 无 CRT 格式化依赖的简易日志字符串辅助函数
// =============================================================================
void AppendText(wchar_t* buffer,
                std::size_t capacity,
                const wchar_t* text) {
    if (!buffer || capacity == 0 || !text)
        return;

    std::size_t used = std::wcslen(buffer);
    while (*text && used + 1 < capacity)
        buffer[used++] = *text++;
    buffer[used] = L'\0';
}

void SetText(wchar_t* buffer,
             std::size_t capacity,
             const wchar_t* text) {
    if (!buffer || capacity == 0)
        return;
    buffer[0] = L'\0';
    AppendText(buffer, capacity, text);
}

void Log(const wchar_t* message) {
    if (g_log)
        g_log(message);
}

void AppendUnsigned(wchar_t* buffer,
                    std::size_t capacity,
                    unsigned value) {
    wchar_t temporary[16] = {};
    std::size_t length = 0;
    do {
        temporary[length++] = static_cast<wchar_t>(L'0' + value % 10);
        value /= 10;
    } while (value && length < CountOf(temporary));

    std::size_t used = std::wcslen(buffer);
    while (length && used + 1 < capacity)
        buffer[used++] = temporary[--length];
    buffer[used] = L'\0';
}

void AppendHex(wchar_t* buffer,
               std::size_t capacity,
               std::uintptr_t value) {
    static const wchar_t digits[] = L"0123456789ABCDEF";
    wchar_t temporary[2 + sizeof(std::uintptr_t) * 2 + 1] = {};
    temporary[0] = L'0';
    temporary[1] = L'x';

    const std::size_t digitCount = sizeof(std::uintptr_t) * 2;
    for (std::size_t index = 0; index < digitCount; ++index) {
        const std::size_t shift = (digitCount - 1 - index) * 4;
        temporary[2 + index] = digits[(value >> shift) & 0xF];
    }
    AppendText(buffer, capacity, temporary);
}

void AppendScale(wchar_t* buffer,
                 std::size_t capacity,
                 float scale) {
    const unsigned thousand = static_cast<unsigned>(
        scale * 1000.0f + 0.5f);
    AppendUnsigned(buffer, capacity, thousand / 1000);
    AppendText(buffer, capacity, L".");

    const unsigned fraction = thousand % 1000;
    if (fraction < 100)
        AppendText(buffer, capacity, L"0");
    if (fraction < 10)
        AppendText(buffer, capacity, L"0");
    AppendUnsigned(buffer, capacity, fraction);
}

LONG FloatBits(float value) {
    LONG bits = 0;
    static_assert(sizeof(bits) == sizeof(value),
        "float 与 LONG 必须同为 4 字节。");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float BitsFloat(LONG bits) {
    float value = 1.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void StoreScale(float value) {
    InterlockedExchange(&g_scaleBits, FloatBits(value));
}

float CurrentScale() {
    return BitsFloat(InterlockedCompareExchange(&g_scaleBits, 0, 0));
}

// =============================================================================
// x86 机器码生成辅助函数
// =============================================================================
void Emit8(std::uint8_t* code,
           std::size_t& cursor,
           std::uint8_t value) {
    code[cursor++] = value;
}

void Emit32(std::uint8_t* code,
            std::size_t& cursor,
            std::uint32_t value) {
    std::memcpy(code + cursor, &value, sizeof(value));
    cursor += sizeof(value);
}

void EmitJumpBack(std::uint8_t* code,
                  std::size_t& cursor,
                  std::uint8_t* target) {
    Emit8(code, cursor, 0xE9); // jmp rel32
    const std::intptr_t from = reinterpret_cast<std::intptr_t>(
        code + cursor + sizeof(std::int32_t));
    const std::intptr_t to = reinterpret_cast<std::intptr_t>(target);
    const std::int32_t relative = static_cast<std::int32_t>(to - from);
    std::memcpy(code + cursor, &relative, sizeof(relative));
    cursor += sizeof(relative);
}

// 写入一个带 32 位相对位移的跳转/条件跳转，并返回“位移字段”的偏移。
std::size_t EmitRel32Placeholder(std::uint8_t* code,
                                 std::size_t& cursor,
                                 std::uint8_t firstOpcode,
                                 std::uint8_t secondOpcode = 0) {
    Emit8(code, cursor, firstOpcode);
    if (firstOpcode == 0x0F)
        Emit8(code, cursor, secondOpcode);

    const std::size_t displacementOffset = cursor;
    Emit32(code, cursor, 0);
    return displacementOffset;
}

// 将前面预留的 rel32 位移修正为跳到同一跳板内的 targetOffset。
void PatchRelative32(std::uint8_t* code,
                     std::size_t displacementOffset,
                     std::size_t targetOffset) {
    const std::int32_t relative = static_cast<std::int32_t>(
        targetOffset - (displacementOffset + sizeof(std::int32_t)));
    std::memcpy(code + displacementOffset, &relative, sizeof(relative));
}

void EmitMulssXmm0Scale(std::uint8_t* code, std::size_t& cursor) {
    // mulss xmm0, dword ptr [g_scaleBits]
    Emit8(code, cursor, 0xF3);
    Emit8(code, cursor, 0x0F);
    Emit8(code, cursor, 0x59);
    Emit8(code, cursor, 0x05);
    Emit32(code, cursor, static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&g_scaleBits)));
}

void EmitFmulScale(std::uint8_t* code, std::size_t& cursor) {
    // fmul dword ptr [g_scaleBits]
    Emit8(code, cursor, 0xD8);
    Emit8(code, cursor, 0x0D);
    Emit32(code, cursor, static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&g_scaleBits)));
}

// 在字幕“预排版入口”同步刷新倍率。
//
// 生成的机器码逻辑等价于：
//
//   height = *g_renderHeight;
//   if (240 <= height <= 10000) {
//       if (OnlyAboveBase && height <= BaseHeight)
//           scale = 1.0;
//       else
//           scale = clamp(height / BaseHeight, 0.25, 8.0);
//       g_scaleBits = bits(scale);
//       g_appliedHeight = height;
//   }
//
// 该代码只破坏 EAX、EFLAGS 和 XMM0，但进入/退出时恢复 EAX 与 EFLAGS，
// 而 XMM0 紧接着会被原始 movss xmm0,[esi+18] 覆盖，因此不会改变原函数
// 其他寄存器状态。手动倍率模式下不生成动态刷新指令。
void EmitSynchronousScaleRefresh(std::uint8_t* code,
                                 std::size_t& cursor,
                                 volatile std::uint16_t* renderHeight) {
    if (g_config.manualScale > 0.0f)
        return;

    Emit8(code, cursor, 0x9C); // pushfd
    Emit8(code, cursor, 0x50); // push eax

    // movzx eax, word ptr [renderHeight]
    Emit8(code, cursor, 0x0F);
    Emit8(code, cursor, 0xB7);
    Emit8(code, cursor, 0x05);
    Emit32(code, cursor, static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(renderHeight)));

    // cmp eax, 240 / jb invalid
    Emit8(code, cursor, 0x3D);
    Emit32(code, cursor, 240);
    const std::size_t jumpBelowValid =
        EmitRel32Placeholder(code, cursor, 0x0F, 0x82);

    // cmp eax, 10000 / ja invalid
    Emit8(code, cursor, 0x3D);
    Emit32(code, cursor, 10000);
    const std::size_t jumpAboveValid =
        EmitRel32Placeholder(code, cursor, 0x0F, 0x87);

    std::size_t jumpToSetOne = 0;
    if (g_config.onlyAboveBase) {
        // cmp eax, BaseHeight / jbe set_one
        Emit8(code, cursor, 0x3D);
        Emit32(code, cursor,
            static_cast<std::uint32_t>(g_config.baseHeight));
        jumpToSetOne = EmitRel32Placeholder(code, cursor, 0x0F, 0x86);
    }

    // 保存本次真正应用的渲染高度，供中文日志读取。
    // mov dword ptr [g_appliedHeight], eax
    Emit8(code, cursor, 0xA3);
    Emit32(code, cursor, static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&g_appliedHeight)));

    // cvtsi2ss xmm0,eax
    Emit8(code, cursor, 0xF3);
    Emit8(code, cursor, 0x0F);
    Emit8(code, cursor, 0x2A);
    Emit8(code, cursor, 0xC0);

    // divss xmm0,[g_baseHeightFloat]
    Emit8(code, cursor, 0xF3);
    Emit8(code, cursor, 0x0F);
    Emit8(code, cursor, 0x5E);
    Emit8(code, cursor, 0x05);
    Emit32(code, cursor, static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&g_baseHeightFloat)));

    // maxss xmm0,[g_minScale]
    Emit8(code, cursor, 0xF3);
    Emit8(code, cursor, 0x0F);
    Emit8(code, cursor, 0x5F);
    Emit8(code, cursor, 0x05);
    Emit32(code, cursor, static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&g_minScale)));

    // minss xmm0,[g_maxScale]
    Emit8(code, cursor, 0xF3);
    Emit8(code, cursor, 0x0F);
    Emit8(code, cursor, 0x5D);
    Emit8(code, cursor, 0x05);
    Emit32(code, cursor, static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&g_maxScale)));

    // movss [g_scaleBits],xmm0
    Emit8(code, cursor, 0xF3);
    Emit8(code, cursor, 0x0F);
    Emit8(code, cursor, 0x11);
    Emit8(code, cursor, 0x05);
    Emit32(code, cursor, static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&g_scaleBits)));

    std::size_t jumpAfterSetOne = 0;
    if (g_config.onlyAboveBase) {
        jumpAfterSetOne = EmitRel32Placeholder(code, cursor, 0xE9);

        const std::size_t setOneOffset = cursor;
        PatchRelative32(code, jumpToSetOne, setOneOffset);

        // 低于或等于基准高度时同样记录实际高度。
        Emit8(code, cursor, 0xA3);
        Emit32(code, cursor, static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(&g_appliedHeight)));

        // mov dword ptr [g_scaleBits], 0x3F800000
        Emit8(code, cursor, 0xC7);
        Emit8(code, cursor, 0x05);
        Emit32(code, cursor, static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(&g_scaleBits)));
        Emit32(code, cursor, 0x3F800000);
    }

    const std::size_t doneOffset = cursor;
    PatchRelative32(code, jumpBelowValid, doneOffset);
    PatchRelative32(code, jumpAboveValid, doneOffset);
    if (g_config.onlyAboveBase)
        PatchRelative32(code, jumpAfterSetOne, doneOffset);

    Emit8(code, cursor, 0x58); // pop eax
    Emit8(code, cursor, 0x9D); // popfd
}

// =============================================================================
// 倍率计算与日志
// =============================================================================
float CalculateScale(std::uint16_t height) {
    if (g_config.manualScale > 0.0f)
        return std::clamp(g_config.manualScale, 0.25f, 8.0f);

    if (height < 240 || height > 10000)
        return CurrentScale();

    if (g_config.onlyAboveBase && height <= g_config.baseHeight)
        return 1.0f;

    return std::clamp(
        static_cast<float>(height) /
            static_cast<float>(g_config.baseHeight),
        0.25f, 8.0f);
}

void LogScaleChange(std::uint16_t height,
                    float scale,
                    const wchar_t* prefix) {
    wchar_t message[256] = {};
    SetText(message, CountOf(message), prefix);
    AppendText(message, CountOf(message), L" 渲染高度=");
    AppendUnsigned(message, CountOf(message), height);
    AppendText(message, CountOf(message), L"，字幕倍率=");
    AppendScale(message, CountOf(message), scale);
    AppendText(message, CountOf(message), L"。\n");
    Log(message);
}

// 只记录预排版 Hook 已经实际应用的值，不在工作线程修改 g_scaleBits。
void LogAppliedScaleIfChanged() {
    const LONG appliedHeight = InterlockedCompareExchange(
        &g_appliedHeight, 0, 0);
    const LONG scaleBits = InterlockedCompareExchange(
        &g_scaleBits, 0, 0);

    if (appliedHeight < 240 || appliedHeight > 10000)
        return;

    const auto height = static_cast<std::uint16_t>(appliedHeight);
    if (height == g_lastLoggedHeight && scaleBits == g_lastLoggedScaleBits)
        return;

    g_lastLoggedHeight = height;
    g_lastLoggedScaleBits = scaleBits;
    LogScaleChange(height, BitsFloat(scaleBits),
        L"[字幕] 预排版已同步应用新分辨率：");
}

void FreePendingTrampolines(std::uint8_t*& layout,
                            std::uint8_t*& draw) {
    Common::FreeExecutable(layout);
    Common::FreeExecutable(draw);
    layout = nullptr;
    draw = nullptr;
}

// =============================================================================
// 特征码定位和 Hook 安装
// =============================================================================
bool InstallFullPipelineHooks() {
    // 该函数用于取得游戏当前实际渲染高度的绝对地址。
    // Steam/GOG 具体地址不同，因此从指令立即数中读取，不硬编码版本地址。
    static const std::uint8_t resolutionPattern[] = {
        0x0F,0xB7,0x05, 0,0,0,0,
        0xF3,0x0F,0x10,0x05, 0,0,0,0,
        0xF3,0x0F,0x2A,0xC8,0x8B,0x44,0x24,0x04,
        0x0F,0x28,0xD0,0xF3,0x0F,0x5E,0xD1,0xF3,0x0F,0x11,0x11,
        0x0F,0xB7,0x15, 0,0,0,0,
        0xF3,0x0F,0x2A,0xCA,0xF3,0x0F,0x5E,0xC1,
        0xF3,0x0F,0x11,0x00,0xC3
    };
    static const char resolutionMask[] =
        "xxx????xxxx????xxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxx";
    static_assert(sizeof(resolutionPattern) == sizeof(resolutionMask) - 1,
        "分辨率特征码与掩码长度不一致。");

    // 字幕预排版入口：该倍率参与行高、最大行数和自动换行测量。
    static const std::uint8_t layoutPattern[] = {
        0x8B,0x74,0x24,0x30,
        0xF3,0x0F,0x10,0x46,0x18,
        0x57,0x8B,0xF8,0x8B,0x46,0x10
    };
    static const char layoutMask[] = "xxxxxxxxxxxxxxx";
    static_assert(sizeof(layoutPattern) == sizeof(layoutMask) - 1,
        "预排版特征码与掩码长度不一致。");

    // 字幕最终绘制入口：同一个倍率同时用于行距和字形绘制。
    static const std::uint8_t drawPattern[] = {
        0xD9,0x47,0x18,
        0xF3,0x0F,0x10,0x46,0x34,
        0x6A,0x00,
        0xD9,0x5E,0x34,
        0x0F,0xB6,0x57,0x29,
        0xD9,0x47,0x18,
        0x8B,0x47,0x38
    };
    static const char drawMask[] = "xxxxxxxxxxxxxxxxxxxxxxx";
    static_assert(sizeof(drawPattern) == sizeof(drawMask) - 1,
        "绘制特征码与掩码长度不一致。");

    const Common::PatternResult resolution =
        Common::FindUniquePatternInCode(resolutionPattern, resolutionMask);
    const Common::PatternResult layout =
        Common::FindUniquePatternInCode(layoutPattern, layoutMask);
    const Common::PatternResult draw =
        Common::FindUniquePatternInCode(drawPattern, drawMask);

    // Steam 原始 EXE 启动时仍可能处于 Stub/未解包状态。未唯一匹配时不写入
    // 任何代码，由 Update 每 250ms 重试。
    if (!resolution.address || !layout.address || !draw.address) {
        if (!g_waitMessageWritten) {
            wchar_t message[360] = {};
            SetText(message, CountOf(message),
                L"[字幕] 正在等待兼容的游戏文本代码完成解包（匹配数：分辨率=");
            AppendUnsigned(message, CountOf(message),
                static_cast<unsigned>(resolution.count));
            AppendText(message, CountOf(message), L"，预排版=");
            AppendUnsigned(message, CountOf(message),
                static_cast<unsigned>(layout.count));
            AppendText(message, CountOf(message), L"，绘制=");
            AppendUnsigned(message, CountOf(message),
                static_cast<unsigned>(draw.count));
            AppendText(message, CountOf(message), L"）。\n");
            Log(message);
            g_waitMessageWritten = true;
        }
        return false;
    }

    std::uint32_t renderHeightAddress32 = 0;
    // resolution.address + 37 是：movzx edx,word ptr [绝对高度地址] 的立即数。
    std::memcpy(&renderHeightAddress32,
        resolution.address + 37, sizeof(renderHeightAddress32));
    auto* renderHeight = reinterpret_cast<volatile std::uint16_t*>(
        static_cast<std::uintptr_t>(renderHeightAddress32));

    if (!Common::IsReadableAddress(
            const_cast<std::uint16_t*>(renderHeight),
            sizeof(std::uint16_t)))
        return false;

    const std::uint16_t initialHeight = *renderHeight;
    if (initialHeight < 240 || initialHeight > 10000)
        return false;

    std::uint8_t* layoutSite = layout.address + 4;
    std::uint8_t* drawSite = draw.address;

    // 同步刷新逻辑比旧 v8 跳板更长，因此扩大预排版跳板空间。
    std::uint8_t* layoutTrampoline = Common::AllocateExecutable(256);
    std::uint8_t* drawTrampoline = Common::AllocateExecutable(128);
    if (!layoutTrampoline || !drawTrampoline) {
        FreePendingTrampolines(layoutTrampoline, drawTrampoline);
        return false;
    }

    std::uint8_t layoutOriginal[sizeof(g_layoutOriginal)] = {};
    std::uint8_t drawOriginal[sizeof(g_drawOriginal)] = {};
    std::memcpy(layoutOriginal, layoutSite, sizeof(layoutOriginal));
    std::memcpy(drawOriginal, drawSite, sizeof(drawOriginal));

    // -------------------------------------------------------------------------
    // 生成预排版跳板
    // -------------------------------------------------------------------------
    std::size_t cursor = 0;

    // 先同步读取真实渲染高度并刷新 g_scaleBits；这一步与实际预排版处于同一
    // 渲染线程调用中，不再依赖 50ms 工作线程轮询时序。
    EmitSynchronousScaleRefresh(
        layoutTrampoline, cursor, renderHeight);

    // 恢复被覆盖的原始指令：movss xmm0,[esi+18]
    Emit8(layoutTrampoline, cursor, 0xF3);
    Emit8(layoutTrampoline, cursor, 0x0F);
    Emit8(layoutTrampoline, cursor, 0x10);
    Emit8(layoutTrampoline, cursor, 0x46);
    Emit8(layoutTrampoline, cursor, 0x18);

    // 让预排版、行数容量和自动换行使用同步后的倍率。
    EmitMulssXmm0Scale(layoutTrampoline, cursor);
    EmitJumpBack(layoutTrampoline, cursor,
        layoutSite + sizeof(layoutOriginal));
    FlushInstructionCache(GetCurrentProcess(),
        layoutTrampoline, cursor);

    // -------------------------------------------------------------------------
    // 生成最终绘制跳板
    // -------------------------------------------------------------------------
    // 绘制跳板不主动更新倍率，只使用最近一次预排版已经确定的倍率。
    // 这是修复“低分辨率切到 4K 后再次截断”的关键：如果绘制先改为 3.0，
    // 而当前字幕仍保存着 1.0 的换行缓存，就会再次产生布局/绘制不一致。
    cursor = 0;
    Emit8(drawTrampoline, cursor, 0xD9);
    Emit8(drawTrampoline, cursor, 0x47);
    Emit8(drawTrampoline, cursor, 0x18); // fld [edi+18]
    EmitFmulScale(drawTrampoline, cursor);

    static const std::uint8_t drawMiddle[] = {
        0xF3,0x0F,0x10,0x46,0x34,
        0x6A,0x00,
        0xD9,0x5E,0x34,
        0x0F,0xB6,0x57,0x29
    };
    std::memcpy(drawTrampoline + cursor,
        drawMiddle, sizeof(drawMiddle));
    cursor += sizeof(drawMiddle);

    Emit8(drawTrampoline, cursor, 0xD9);
    Emit8(drawTrampoline, cursor, 0x47);
    Emit8(drawTrampoline, cursor, 0x18); // fld [edi+18]
    EmitFmulScale(drawTrampoline, cursor);
    EmitJumpBack(drawTrampoline, cursor,
        drawSite + sizeof(drawOriginal));
    FlushInstructionCache(GetCurrentProcess(),
        drawTrampoline, cursor);

    // 构造写入游戏代码的 JMP。绘制点覆盖 20 字节，剩余字节用 NOP 填充。
    std::uint8_t layoutPatch[5] = {0xE9,0,0,0,0};
    std::int32_t relative = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(layoutTrampoline) -
        reinterpret_cast<std::intptr_t>(layoutSite + 5));
    std::memcpy(layoutPatch + 1, &relative, sizeof(relative));

    std::uint8_t drawPatch[20] = {};
    std::memset(drawPatch, 0x90, sizeof(drawPatch));
    drawPatch[0] = 0xE9;
    relative = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(drawTrampoline) -
        reinterpret_cast<std::intptr_t>(drawSite + 5));
    std::memcpy(drawPatch + 1, &relative, sizeof(relative));

    // 安装时先使用中性倍率 1.0。若第二条 Hook 写入失败，第一条 Hook 在回滚
    // 之前仍与原版行为一致，不会留下“只修一半”的不稳定状态。
    StoreScale(1.0f);
    InterlockedExchange(&g_appliedHeight, initialHeight);

    if (!Common::WriteCode(
            layoutSite, layoutPatch, sizeof(layoutPatch))) {
        FreePendingTrampolines(layoutTrampoline, drawTrampoline);
        return false;
    }

    if (!Common::WriteCode(drawSite, drawPatch, sizeof(drawPatch))) {
        const bool rolledBack = Common::WriteCode(
            layoutSite, layoutOriginal, sizeof(layoutOriginal));
        if (rolledBack) {
            FreePendingTrampolines(layoutTrampoline, drawTrampoline);
        } else {
            // 回滚失败时不能释放仍被 JMP 引用的预排版跳板，否则会立即崩溃。
            Common::FreeExecutable(drawTrampoline);
            drawTrampoline = nullptr;
            Log(L"[字幕] 严重错误：最终绘制 Hook 安装失败，且预排版 Hook 回滚失败；已保留中性倍率跳板以避免跳入已释放内存。\n");
        }
        return false;
    }

    g_renderHeight = renderHeight;
    g_layoutHookSite = layoutSite;
    g_drawHookSite = drawSite;
    std::memcpy(g_layoutOriginal,
        layoutOriginal, sizeof(g_layoutOriginal));
    std::memcpy(g_drawOriginal,
        drawOriginal, sizeof(g_drawOriginal));
    g_layoutTrampoline = layoutTrampoline;
    g_drawTrampoline = drawTrampoline;

    g_baseHeightFloat = static_cast<float>(g_config.baseHeight);
    const float initialScale = CalculateScale(initialHeight);
    StoreScale(initialScale);
    InterlockedExchange(&g_appliedHeight, initialHeight);
    g_lastLoggedHeight = initialHeight;
    g_lastLoggedScaleBits = FloatBits(initialScale);
    g_installed = true;

    auto* imageBase = reinterpret_cast<std::uint8_t*>(
        GetModuleHandleW(nullptr));
    wchar_t message[420] = {};
    SetText(message, CountOf(message),
        L"[字幕] 已安装完整排版链：预排版 RVA=");
    AppendHex(message, CountOf(message),
        static_cast<std::uintptr_t>(layoutSite - imageBase));
    AppendText(message, CountOf(message), L"，绘制 RVA=");
    AppendHex(message, CountOf(message),
        static_cast<std::uintptr_t>(drawSite - imageBase));
    AppendText(message, CountOf(message), L"，渲染高度地址=");
    AppendHex(message, CountOf(message),
        static_cast<std::uintptr_t>(renderHeightAddress32));
    AppendText(message, CountOf(message), L"。\n");
    Log(message);

    LogScaleChange(initialHeight, initialScale,
        L"[字幕] 初始排版倍率：");
    Log(L"[字幕] 分辨率倍率将在每次字幕预排版入口同步刷新；工作线程不再提前修改绘制倍率。\n");
    return true;
}

} // namespace

void Configure(const Config& config, Common::LogCallback logger) {
    g_config = config;

    if (g_config.baseHeight < 240 || g_config.baseHeight > 4320)
        g_config.baseHeight = 720;

    if (!std::isfinite(g_config.manualScale) ||
        g_config.manualScale < 0.0f)
        g_config.manualScale = 0.0f;
    if (g_config.manualScale > 0.0f)
        g_config.manualScale = std::clamp(
            g_config.manualScale, 0.25f, 8.0f);

    g_baseHeightFloat = static_cast<float>(g_config.baseHeight);
    g_log = logger;
    g_configured = true;
}

void Update() {
    if (!g_configured || !g_config.enabled)
        return;

    if (!g_installed) {
        const ULONGLONG now = GetTickCount64();
        if (now < g_nextInstallAttempt)
            return;

        g_nextInstallAttempt = now + 250;
        InstallFullPipelineHooks();
        return;
    }

    // 这里只读取预排版 Hook 已实际应用的值并写日志，绝不主动修改倍率。
    LogAppliedScaleIfChanged();
}

bool IsInstalled() {
    return g_installed;
}

} // namespace SubtitleScaling
