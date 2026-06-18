#pragma once
//
// ini.hpp — 自动定位 INI 配置文件读取器，供 ASI 插件使用
//
// ===========================================================================
// 工作原理
// ===========================================================================
// 每个插件通过自身模块句柄（HMODULE）定位 .asi 文件路径，
// 然后自动将扩展名替换为 .ini，读取同目录下的同名配置文件。
//
// 例如：D:\Dead Space\DSOpt.asi → D:\Dead Space\DSOpt.ini
//
// ===========================================================================
// 编码说明
// ===========================================================================
// 全 Unicode (UTF-16 LE) API 实现，支持全球用户路径中的非英文字符
// （如日文用户名、阿拉伯文 Steam 库路径等）。
// INI 文件本身建议 ANSI 编码（Windows INI API 兼容性最佳）。
//
// ===========================================================================
// 重要限制
// ===========================================================================
// ReadIni* 函数依赖传入的 HMODULE，该参数来自 DllMain 的第一个参数。
// 严禁在 DllMain(DLL_PROCESS_ATTACH) 执行之前调用这些函数。
// 在 DllMain 内部调用是安全的（kernel32 已加载完毕）。
//

#include <windows.h>
#include <string>

// ---------------------------------------------------------------------------
// _GetIniPath：根据模块句柄构造对应的 INI 文件完整路径（Unicode 版）
// ---------------------------------------------------------------------------
// 内部辅助函数，不对外暴露。
// 步骤：
//   1. GetModuleFileNameW 获取 .asi 文件的完整路径
//   2. 从右向左查找最后一个 L'.' 字符（扩展名分隔符）
//   3. 将扩展名替换为 L".ini"
// 例如：D:\ゲーム\DSOpt.asi → D:\ゲーム\DSOpt.ini
// ---------------------------------------------------------------------------
inline void _GetIniPath(HMODULE hModule, wchar_t* outPath, size_t outSize) {
    outPath[0] = L'\0';
    if (!GetModuleFileNameW(hModule, outPath, (DWORD)outSize))
        return;

    // 定位最后一个 '.' 并替换为 ".ini"
    wchar_t* dot = wcsrchr(outPath, L'.');
    if (!dot) return;
    wcscpy(dot, L".ini");
}

// ---------------------------------------------------------------------------
// ReadIniInt：从指定节 [section] 读取整数键值
// ---------------------------------------------------------------------------
// 参数：
//   hModule     — DllMain 传入的模块句柄
//   section     — INI 节名（宽字符），如 L"CrashFix"
//   key         — 键名（宽字符），如 L"CoreLimit"
//   default_val — 键不存在或 INI 文件缺失时的默认返回值
// 返回值：
//   解析后的整数值，或 default_val
// 底层调用：GetPrivateProfileIntW（kernel32 API，线程安全）
// ---------------------------------------------------------------------------
inline int ReadIniInt(HMODULE hModule, const wchar_t* section, const wchar_t* key, int default_val) {
    wchar_t path[MAX_PATH];
    _GetIniPath(hModule, path, MAX_PATH);
    return GetPrivateProfileIntW(section, key, default_val, path);
}

// ---------------------------------------------------------------------------
// ReadIniStr：从指定节 [section] 读取字符串键值
// ---------------------------------------------------------------------------
// 参数：
//   hModule     — DllMain 传入的模块句柄
//   section     — INI 节名（宽字符）
//   key         — 键名（宽字符）
//   default_val — 键不存在时的默认返回值（宽字符）
// 返回值：
//   std::wstring 形式的字符串值（最长 255 字符）
// 底层调用：GetPrivateProfileStringW（kernel32 API，线程安全）
// ---------------------------------------------------------------------------
inline std::wstring ReadIniStr(HMODULE hModule, const wchar_t* section, const wchar_t* key, const wchar_t* default_val) {
    wchar_t path[MAX_PATH];
    _GetIniPath(hModule, path, MAX_PATH);
    wchar_t buf[256];
    GetPrivateProfileStringW(section, key, default_val, buf, 256, path);
    return std::wstring(buf);
}
