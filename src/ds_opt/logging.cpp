#include "logging.hpp"

#include "../common/file_utils.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Logging {
namespace {

// 日志模块可能被主工作线程、窗口模块和游戏渲染线程相关逻辑间接调用，使用
// SRWLOCK 保护开关、路径和文件追加操作。SRWLOCK 不需要显式初始化/销毁。
bool g_enabled = false;
std::wstring g_logPath;
SRWLOCK g_lock = SRWLOCK_INIT;

// 保证完整写入缓冲区，处理 WriteFile 的短写入情况。
bool WriteAll(HANDLE file, const void* data, DWORD size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    DWORD remaining = size;

    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(file, bytes, remaining, &written, nullptr) ||
            written == 0)
            return false;

        bytes += written;
        remaining -= written;
    }
    return true;
}

} // namespace

void Initialize(HMODULE module, bool enabled) {
    AcquireSRWLockExclusive(&g_lock);
    g_enabled = enabled;
    g_logPath = Common::JoinPath(
        Common::GetModuleDirectory(module), L"DSOpt.log");
    ReleaseSRWLockExclusive(&g_lock);
}

bool IsEnabled() {
    AcquireSRWLockShared(&g_lock);
    const bool enabled = g_enabled;
    ReleaseSRWLockShared(&g_lock);
    return enabled;
}

void Write(const wchar_t* message) {
    if (!message)
        return;

    // 先在共享锁下复制状态，避免在较慢的编码转换过程中长期占用锁。
    AcquireSRWLockShared(&g_lock);
    const bool enabled = g_enabled;
    const std::wstring path = g_logPath;
    ReleaseSRWLockShared(&g_lock);
    if (!enabled)
        return;

    // 同时输出到调试器，便于使用 DebugView/调试器实时查看简体中文日志。
    OutputDebugStringW(message);

    // 磁盘日志统一使用 UTF-8。WideCharToMultiByte 返回值包含结尾空字符。
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1)
        return;

    std::vector<char> utf8(static_cast<std::size_t>(required));
    if (WideCharToMultiByte(CP_UTF8, 0, message, -1,
            utf8.data(), required, nullptr, nullptr) <= 0)
        return;

    // 文件追加必须串行化，防止多个模块的日志行互相穿插。
    AcquireSRWLockExclusive(&g_lock);
    HANDLE file = CreateFileW(path.c_str(),
        GENERIC_READ | FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size = {};
        if (GetFileSizeEx(file, &size) && size.QuadPart == 0) {
            // 新日志文件写入 UTF-8 BOM，保证 Windows 记事本正确识别中文。
            static const std::uint8_t bom[] = {0xEF, 0xBB, 0xBF};
            WriteAll(file, bom, static_cast<DWORD>(sizeof(bom)));
        }

        // 不写入 std::vector 末尾的 '\0'，日志保持普通文本格式。
        WriteAll(file, utf8.data(), static_cast<DWORD>(utf8.size() - 1));
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace Logging
