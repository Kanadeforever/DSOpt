#include "game_files.hpp"

#include "../common/file_utils.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace GameFiles {
namespace {

void Log(Common::LogCallback logger, const wchar_t* message) {
    if (logger)
        logger(message);
}

// 使用 SHGetFolderPathW 取得旧游戏常用的系统目录，并要求系统创建目录。
std::wstring KnownFolder(int csidl) {
    wchar_t path[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, csidl | CSIDL_FLAG_CREATE,
            nullptr, SHGFP_TYPE_CURRENT, path)))
        return {};
    return path;
}

// 只处理 ASCII 空白，因为 settings.txt 的键名和数值均为 ASCII。
std::string TrimAscii(std::string value) {
    auto isSpace = [](unsigned char ch) {
        return std::isspace(ch) != 0;
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
        [&](char ch) {
            return !isSpace(static_cast<unsigned char>(ch));
        }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
        [&](char ch) {
            return !isSpace(static_cast<unsigned char>(ch));
        }).base(), value.end());
    return value;
}

bool EqualsAsciiInsensitive(const std::string& left, const char* right) {
    if (!right || left.size() != std::strlen(right))
        return false;

    for (std::size_t index = 0; index < left.size(); ++index) {
        const unsigned char a = static_cast<unsigned char>(left[index]);
        const unsigned char b = static_cast<unsigned char>(right[index]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

// 按行解析“键 = 值”，忽略空白、大小写和 ;/# 注释行。
// 这样可避免仅因用户使用不同空格格式而重复写入同一个键。
bool ContainsSettingKey(const std::string& content, const char* key) {
    std::size_t begin = 0;
    while (begin <= content.size()) {
        const std::size_t end = content.find_first_of("\r\n", begin);
        std::string line = content.substr(begin,
            end == std::string::npos ? std::string::npos : end - begin);
        line = TrimAscii(std::move(line));

        if (!line.empty() && line.front() != ';' && line.front() != '#') {
            const std::size_t equals = line.find('=');
            if (equals != std::string::npos) {
                const std::string candidate = TrimAscii(
                    line.substr(0, equals));
                if (EqualsAsciiInsensitive(candidate, key))
                    return true;
            }
        }

        if (end == std::string::npos)
            break;
        begin = end + 1;
        if (begin < content.size() && content[end] == '\r' &&
            content[begin] == '\n')
            ++begin;
    }
    return false;
}

bool EnsureParentDirectory(const std::wstring& filePath) {
    const std::wstring::size_type slash = filePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return false;
    return Common::EnsureDirectoryTree(filePath.substr(0, slash));
}

// 按用户明确要求重建 joypad.txt：
// - 文件不存在：直接创建空文件；
// - 文件存在且可读：保持原样；
// - 文件存在但无法读取：删除并重新创建为空文件。
//
// 删除前会清除只读属性。若 ACL/占用状态仍阻止删除，则记录失败，不伪装成功。
bool RecreateEmptyFile(const std::wstring& path,
                       bool removeExisting,
                       Common::LogCallback logger) {
    if (removeExisting) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesW(path.c_str(),
                attributes & ~FILE_ATTRIBUTE_READONLY);
        }

        if (!DeleteFileW(path.c_str())) {
            wchar_t message[256] = {};
            swprintf_s(message, sizeof(message) / sizeof(message[0]),
                L"[文件] joypad.txt 无法读取，尝试删除失败，错误码=%lu。\n",
                GetLastError());
            Log(logger, message);
            return false;
        }
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        wchar_t message[256] = {};
        swprintf_s(message, sizeof(message) / sizeof(message[0]),
            L"[文件] 创建空 joypad.txt 失败，错误码=%lu。\n",
            GetLastError());
        Log(logger, message);
        return false;
    }

    CloseHandle(file);
    return true;
}

} // namespace

void ApplyDefaultSettings(Common::LogCallback logger) {
    const std::wstring localAppData = KnownFolder(CSIDL_LOCAL_APPDATA);
    if (localAppData.empty()) {
        Log(logger, L"[文件] 无法取得本地应用数据目录。\n");
        return;
    }

    const std::wstring path = Common::JoinPath(localAppData,
        L"Electronic Arts\\Dead Space\\settings.txt");
    if (!EnsureParentDirectory(path)) {
        Log(logger, L"[文件] 无法创建 settings.txt 所在目录。\n");
        return;
    }

    std::vector<std::uint8_t> bytes;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        // 1 MiB 已远高于正常 settings.txt，使用上限防止异常文件占用大量内存。
        constexpr std::size_t kMaximumSettingsSize = 1024 * 1024;
        if (!Common::ReadFileBytes(path, bytes, kMaximumSettingsSize)) {
            Log(logger,
                L"[文件] 现有 settings.txt 无法安全读取，未进行修改。\n");
            return;
        }
    } else {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            Log(logger,
                L"[文件] 无法确认 settings.txt 状态，未进行修改。\n");
            return;
        }
    }

    // 游戏期望 ANSI/UTF-8 风格文本。检测到 UTF-16 时不进行转换，避免破坏文件。
    if (bytes.size() >= 2 &&
        ((bytes[0] == 0xFF && bytes[1] == 0xFE) ||
         (bytes[0] == 0xFE && bytes[1] == 0xFF))) {
        Log(logger,
            L"[文件] settings.txt 为 UTF-16，出于兼容性考虑未进行修改。\n");
        return;
    }

    std::string content(bytes.begin(), bytes.end());
    while (!content.empty() && content.back() == '\0')
        content.pop_back();
    if (content.find('\0') != std::string::npos) {
        Log(logger, L"[文件] settings.txt 含二进制数据，未进行修改。\n");
        return;
    }

    // 搜索键名时忽略 UTF-8 BOM，但保留原始正文，写回时不改变编码标记。
    std::string searchable = content;
    if (searchable.size() >= 3 &&
        static_cast<unsigned char>(searchable[0]) == 0xEF &&
        static_cast<unsigned char>(searchable[1]) == 0xBB &&
        static_cast<unsigned char>(searchable[2]) == 0xBF)
        searchable.erase(0, 3);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    if (screenWidth <= 0 || screenHeight <= 0) {
        screenWidth = 1280;
        screenHeight = 720;
    }

    struct DefaultSetting {
        const char* key;
        std::string value;
    };

    const DefaultSetting defaults[] = {
        {"Window.Width", std::to_string(screenWidth)},
        {"Window.Height", std::to_string(screenHeight)},
        {"Window.Fullscreen", "false"},
        {"Window.VSync", "false"},
    };

    std::string append;
    for (const DefaultSetting& setting : defaults) {
        if (!ContainsSettingKey(searchable, setting.key)) {
            append += setting.key;
            append += " = ";
            append += setting.value;
            append += "\r\n";
        }
    }

    if (append.empty())
        return;

    if (!content.empty() && content.back() != '\n' && content.back() != '\r')
        content += "\r\n";
    content += append;

    if (Common::WriteFileBytesAtomic(path, content.data(), content.size()))
        Log(logger, L"[文件] 已安全补充 settings.txt 中缺失的默认项。\n");
    else
        Log(logger, L"[文件] 更新 settings.txt 失败。\n");
}

void EnsureJoypadConfig(Common::LogCallback logger) {
    const std::wstring documents = KnownFolder(CSIDL_PERSONAL);
    if (documents.empty()) {
        Log(logger, L"[文件] 无法取得文档目录。\n");
        return;
    }

    const std::wstring path = Common::JoinPath(documents,
        L"Electronic Arts\\Dead Space\\joypad.txt");
    if (!EnsureParentDirectory(path)) {
        Log(logger, L"[文件] 无法创建 joypad.txt 所在目录。\n");
        return;
    }

    // 不只检查属性：真正尝试以读取方式打开文件，才能发现 ACL、损坏的重解析点
    // 或其他导致“文件存在但游戏无法读取”的状态。
    HANDLE readable = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (readable != INVALID_HANDLE_VALUE) {
        // 读取最多 1 字节；空文件会正常返回成功且 read=0。这样既不修改文件，
        // 又能确认普通 ReadFile 调用路径可用。
        BYTE probe = 0;
        DWORD read = 0;
        const BOOL readOk = ReadFile(
            readable, &probe, sizeof(probe), &read, nullptr);
        CloseHandle(readable);
        if (readOk)
            return;

        Log(logger,
            L"[文件] joypad.txt 存在但无法读取，将按配置要求删除并重建。\n");
        if (RecreateEmptyFile(path, true, logger))
            Log(logger, L"[文件] 已重建空的 joypad.txt。\n");
        return;
    }

    const DWORD openError = GetLastError();
    if (openError == ERROR_FILE_NOT_FOUND || openError == ERROR_PATH_NOT_FOUND) {
        if (RecreateEmptyFile(path, false, logger))
            Log(logger, L"[文件] 已创建空的 joypad.txt。\n");
        return;
    }

    // 文件存在但无法取得读取句柄：严格执行用户要求，删除后重建。
    Log(logger,
        L"[文件] joypad.txt 无法读取，将按配置要求删除并重建。\n");
    if (RecreateEmptyFile(path, true, logger))
        Log(logger, L"[文件] 已重建空的 joypad.txt。\n");
}

} // namespace GameFiles
