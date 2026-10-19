#pragma once

#include <windows.h>

#include <string>

inline std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), len);
    return out;
}

inline std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), len, nullptr, nullptr);
    return out;
}

inline std::string Trim(const std::string& text, const char* chars = " \t\r\n")
{
    const auto begin = text.find_first_not_of(chars);
    if (begin == std::string::npos)
        return {};
    const auto end = text.find_last_not_of(chars);
    return text.substr(begin, end - begin + 1);
}

inline size_t FindNoCase(const std::string& text, const std::string& needle)
{
    if (needle.empty())
        return 0;
    for (size_t i = 0; i + needle.size() <= text.size(); ++i)
    {
        if (_strnicmp(text.c_str() + i, needle.c_str(), needle.size()) == 0)
            return i;
    }
    return std::string::npos;
}

inline bool ContainsNoCase(const std::string& text, const std::string& needle)
{
    return FindNoCase(text, needle) != std::string::npos;
}

template <typename... Args>
std::wstring Format(const wchar_t* fmt, Args... args)
{
    wchar_t buf[512];
    swprintf_s(buf, fmt, args...);
    return buf;
}
