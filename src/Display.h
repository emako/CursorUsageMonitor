#pragma once

#include "Text.h"

#include <algorithm>
#include <string>

// Percentage text on the taskbar and in the tooltip.
struct DisplayFormat
{
    bool showDecimal = false;
    bool hidePercent = false;
    bool separateWithSpace = false;
};

inline bool SameDisplay(const DisplayFormat& a, const DisplayFormat& b)
{
    return a.showDecimal == b.showDecimal
        && a.hidePercent == b.hidePercent
        && a.separateWithSpace == b.separateWithSpace;
}

inline std::wstring FormatPercent(double value, const DisplayFormat& fmt)
{
    const double clamped = std::clamp(value, 0.0, 100.0);
    std::wstring text = fmt.showDecimal ? Format(L"%.1f", clamped) : Format(L"%.0f", clamped);
    if (!fmt.hidePercent)
    {
        if (fmt.separateWithSpace)
            text.push_back(L' ');
        text.push_back(L'%');
    }
    return text;
}
