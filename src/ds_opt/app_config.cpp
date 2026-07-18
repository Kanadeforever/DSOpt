#include "app_config.hpp"

#include "../common/file_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace AppConfig {
namespace {

// 默认配置使用 UTF-16LE 写入，确保中文注释在 Windows 记事本中不会乱码。
// 配置键名保持英文，以兼容旧版本 DSOpt.ini 和现有用户配置。
const wchar_t kDefaultIni[] =
    L"[CrashFix]\r\n"
    L"; 0=禁用；1-8=限制进程使用前 N 个当前允许的逻辑处理器；非法值回退为 4\r\n"
    L"CoreLimit=4\r\n"
    L"\r\n"
    L"[Window]\r\n"
    L"; 优先启用 Per-Monitor V2 DPI 感知，旧版 Windows 自动回退\r\n"
    L"DPIAware=1\r\n"
    L"; 启动后将游戏窗口转换为无边框窗口\r\n"
    L"Borderless=1\r\n"
    L"; 普通窗口模式下，在启动和分辨率变化后自动居中\r\n"
    L"CenterWindow=1\r\n"
    L"; 让游戏窗口保持置顶\r\n"
    L"AlwaysOnTop=0\r\n"
    L"; 无边框切换热键的虚拟键码；0=禁用，121=F10\r\n"
    L"ToggleKey=121\r\n"
    L"; 窗口样式、位置和置顶状态的维护周期，范围 100-60000 毫秒\r\n"
    L"PollInterval=1000\r\n"
    L"\r\n"
    L"[Subtitle]\r\n"
    L"; 同时修复字幕预排版、自动换行、行数容量和最终绘制\r\n"
    L"FixFullPipelineScaling=1\r\n"
    L"; 0=自动使用游戏实际渲染高度/BaseHeight；正数=固定手动倍率\r\n"
    L"Scale=0\r\n"
    L"; 游戏开发分辨率的纵向基准\r\n"
    L"BaseHeight=720\r\n"
    L"; 1=720p及以下保留原版大小；0=低分辨率也严格等比例缩放\r\n"
    L"OnlyAboveBase=1\r\n"
    L"\r\n"
    L"[Debug]\r\n"
    L"; 输出简体中文 UTF-8 日志到 DSOpt.log\r\n"
    L"Log=0\r\n";

std::wstring IniPath(HMODULE module) {
    return Common::ReplaceExtension(Common::GetModulePath(module), L".ini");
}

int ReadInt(const std::wstring& path,
            const wchar_t* section,
            const wchar_t* key,
            int fallback) {
    return GetPrivateProfileIntW(section, key, fallback, path.c_str());
}

// GetPrivateProfileStringW 在缓冲区不足时会截断，因此按需扩容；同时设置
// 32768 字符上限，避免损坏 INI 或异常 API 行为造成无限内存增长。
std::wstring ReadString(const std::wstring& path,
                        const wchar_t* section,
                        const wchar_t* key,
                        const wchar_t* fallback) {
    std::vector<wchar_t> buffer(256);
    for (;;) {
        const DWORD copied = GetPrivateProfileStringW(
            section, key, fallback, buffer.data(),
            static_cast<DWORD>(buffer.size()), path.c_str());

        if (copied < buffer.size() - 1)
            return std::wstring(buffer.data(), copied);
        if (buffer.size() >= 32768)
            return fallback ? std::wstring(fallback) : std::wstring();
        buffer.resize(buffer.size() * 2);
    }
}

float ReadScale(const std::wstring& path) {
    std::wstring text = ReadString(path, L"Subtitle", L"Scale", L"0");

    // 兼容使用逗号作为小数点的区域格式，例如“1,5”。
    std::replace(text.begin(), text.end(), L',', L'.');

    wchar_t* end = nullptr;
    const float value = std::wcstof(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value) || value < 0.0f)
        return 0.0f;

    // 数值后只允许空白；出现额外字符时将其视为无效配置。
    while (end && *end && std::iswspace(*end))
        ++end;
    if (end && *end)
        return 0.0f;

    if (value == 0.0f)
        return 0.0f;
    return std::clamp(value, 0.25f, 8.0f);
}

} // namespace

bool EnsureDefaultIni(HMODULE module) {
    const std::wstring path = IniPath(module);
    if (path.empty())
        return false;

    // 已存在的 INI 完全交给用户管理，不覆盖也不重写。
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
        return true;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    // 写入 UTF-16LE BOM，再写入 wchar_t 配置正文。
    static const unsigned char bom[] = {0xFF, 0xFE};
    DWORD written = 0;
    constexpr DWORD bomSize = static_cast<DWORD>(sizeof(bom));
    bool ok = WriteFile(file, bom, bomSize, &written, nullptr) != FALSE &&
              written == bomSize;

    const DWORD bytes = static_cast<DWORD>(
        std::wcslen(kDefaultIni) * sizeof(wchar_t));
    if (ok) {
        ok = WriteFile(file, kDefaultIni, bytes, &written, nullptr) != FALSE &&
             written == bytes;
    }

    CloseHandle(file);
    if (!ok)
        DeleteFileW(path.c_str());
    return ok;
}

Config Load(HMODULE module) {
    Config config;
    const std::wstring path = IniPath(module);
    if (path.empty())
        return config;

    config.crash.coreLimit = ReadInt(path, L"CrashFix", L"CoreLimit", 4);
    if (config.crash.coreLimit != 0 &&
        (config.crash.coreLimit < 1 || config.crash.coreLimit > 8))
        config.crash.coreLimit = 4;

    config.window.dpiAware =
        ReadInt(path, L"Window", L"DPIAware", 1) != 0;
    config.window.borderless =
        ReadInt(path, L"Window", L"Borderless", 1) != 0;
    config.window.centerWindow =
        ReadInt(path, L"Window", L"CenterWindow", 1) != 0;
    config.window.alwaysOnTop =
        ReadInt(path, L"Window", L"AlwaysOnTop", 0) != 0;

    config.window.toggleKey =
        ReadInt(path, L"Window", L"ToggleKey", VK_F10);
    if (config.window.toggleKey < 0 || config.window.toggleKey > 255)
        config.window.toggleKey = VK_F10;

    config.window.pollIntervalMs =
        ReadInt(path, L"Window", L"PollInterval", 1000);
    config.window.pollIntervalMs = std::clamp(
        config.window.pollIntervalMs, 100, 60000);

    config.subtitle.enabled = ReadInt(
        path, L"Subtitle", L"FixFullPipelineScaling", 1) != 0;
    config.subtitle.baseHeight = ReadInt(
        path, L"Subtitle", L"BaseHeight", 720);
    if (config.subtitle.baseHeight < 240 ||
        config.subtitle.baseHeight > 4320)
        config.subtitle.baseHeight = 720;

    config.subtitle.onlyAboveBase = ReadInt(
        path, L"Subtitle", L"OnlyAboveBase", 1) != 0;
    config.subtitle.manualScale = ReadScale(path);

    config.debug.log = ReadInt(path, L"Debug", L"Log", 0) != 0;
    return config;
}

} // namespace AppConfig
