#include "file_utils.hpp"

#include <shlobj.h>

#include <algorithm>
#include <limits>

namespace Common {
namespace {

// 将 size 字节完整写入文件。
// WriteFile 单次写入长度使用 DWORD，因此对超大缓冲区进行分块；同时处理
// “调用成功但写入 0 字节”的异常情况，避免死循环。
bool WriteAll(HANDLE file, const std::uint8_t* data, std::size_t size) {
    while (size > 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            size, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));

        DWORD written = 0;
        if (!WriteFile(file, data, chunk, &written, nullptr) || written == 0)
            return false;

        data += written;
        size -= written;
    }
    return true;
}

} // namespace

std::wstring GetModulePath(HMODULE module) {
    // MAX_PATH 不能覆盖所有合法 Windows 路径，因此从 512 字符开始按需扩容。
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};

        // 预留一个字符用于结尾空字符。长度接近缓冲区上限时按截断处理。
        if (length < buffer.size() - 1)
            return std::wstring(buffer.data(), length);

        // 防止损坏的返回值造成无限扩容。
        if (buffer.size() >= 32768)
            return {};
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring GetModuleDirectory(HMODULE module) {
    std::wstring path = GetModulePath(module);
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return {};

    path.resize(slash);
    return path;
}

std::wstring ReplaceExtension(const std::wstring& path,
                              const wchar_t* extension) {
    if (!extension)
        return path;

    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    const std::wstring::size_type dot = path.find_last_of(L'.');

    // 点号位于最后一个目录分隔符之前时，它属于目录名而不是文件扩展名。
    if (dot == std::wstring::npos ||
        (slash != std::wstring::npos && dot < slash))
        return path + extension;

    return path.substr(0, dot) + extension;
}

std::wstring JoinPath(const std::wstring& left,
                      const std::wstring& right) {
    if (left.empty())
        return right;
    if (right.empty())
        return left;

    const bool leftSlash = left.back() == L'\\' || left.back() == L'/';
    const bool rightSlash = right.front() == L'\\' || right.front() == L'/';

    if (leftSlash && rightSlash)
        return left + right.substr(1);
    if (!leftSlash && !rightSlash)
        return left + L"\\" + right;
    return left + right;
}

bool EnsureDirectoryTree(const std::wstring& directory) {
    if (directory.empty())
        return false;

    // SHCreateDirectoryExW 会递归创建缺失的父目录。
    const int result = SHCreateDirectoryExW(
        nullptr, directory.c_str(), nullptr);
    return result == ERROR_SUCCESS ||
           result == ERROR_ALREADY_EXISTS ||
           result == ERROR_FILE_EXISTS;
}

bool ReadFileBytes(const std::wstring& path,
                   std::vector<std::uint8_t>& out,
                   std::size_t maximumSize) {
    out.clear();

    // 允许其他进程同时读取、写入或删除，尽量避免与游戏自身文件操作冲突。
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > maximumSize) {
        CloseHandle(file);
        return false;
    }

    out.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < out.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            out.size() - offset,
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));

        DWORD read = 0;
        if (!ReadFile(file, out.data() + offset, chunk, &read, nullptr)) {
            CloseHandle(file);
            out.clear();
            return false;
        }
        if (read == 0)
            break;
        offset += read;
    }

    CloseHandle(file);

    // 文件大小在读取期间发生变化时，不返回不完整数据。
    if (offset != out.size()) {
        out.resize(offset);
        return false;
    }
    return true;
}

bool WriteFileBytesAtomic(const std::wstring& path,
                          const void* data,
                          std::size_t size) {
    if (!data && size != 0)
        return false;

    const std::wstring temporary = path + L".dsopt.tmp";

    // 临时文件位于目标文件同一目录，MoveFileExW 替换时可保持同卷原子性。
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    const bool wrote = WriteAll(
        file, static_cast<const std::uint8_t*>(data), size) &&
        FlushFileBuffers(file) != FALSE;
    CloseHandle(file);

    if (!wrote) {
        DeleteFileW(temporary.c_str());
        return false;
    }

    if (!MoveFileExW(temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace Common
