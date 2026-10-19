#pragma once

#include <windows.h>

// Option-dialog strings. Checkbox wording for percent and spacing matches
// TrafficMonitorBar zh-CN.ini / en-US.ini (TXT_HIDE_PERCENTAGE_CHECK, TXT_SEPARATE_VALUE_UNIT_CHECK, TXT_OK, TXT_CANCEL).
namespace Lang {

enum class Id
{
    ZhCn,
    EnUs,
};

inline Id FromLanguageId(unsigned short langId)
{
    if (langId == 0)
        langId = GetUserDefaultUILanguage();
    return PRIMARYLANGID(langId) == LANG_CHINESE ? Id::ZhCn : Id::EnUs;
}

struct Text
{
    const wchar_t* optionsTitle;
    const wchar_t* intervalLabel;
    const wchar_t* intervalHint;
    const wchar_t* invalidInterval;
    const wchar_t* showDecimal;
    const wchar_t* hidePercent;
    const wchar_t* separateSpace;
    const wchar_t* ok;
    const wchar_t* cancel;
    const wchar_t* messageTitle;
};

inline const Text& Get(Id id)
{
    static const Text zh{
        L"Cursor 用量 选项",
        L"刷新间隔（分钟）：",
        L"范围 %d–%d，默认 %d；刷新失败时 1 分钟后重试。",
        L"请输入 %d 到 %d 之间的整数。",
        L"显示小数点",
        L"不显示百分号",
        L"数值和单位用空格分隔",
        L"确定",
        L"取消",
        L"Cursor 用量",
    };
    static const Text en{
        L"Cursor Usage Options",
        L"Refresh interval (minutes):",
        L"Range %d–%d, default %d. Retry after 1 minute on failure.",
        L"Enter an integer between %d and %d.",
        L"Show decimal point",
        L"Don't show percent",
        L"Separate value and unit with space",
        L"OK",
        L"Cancel",
        L"Cursor Usage",
    };
    return id == Id::ZhCn ? zh : en;
}

} // namespace Lang
