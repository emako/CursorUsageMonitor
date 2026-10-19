#include "Plugin.h"

#include "DataManager.h"
#include "Lang.h"
#include "Text.h"
#include "Theme.hpp"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <ctime>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
struct LanguageBinder
{
    LanguageBinder()
    {
        Lang::SetLanguageProvider([]() -> unsigned short {
            return CCursorUsagePlugin::Instance().LanguageId();
        });
    }
};
[[maybe_unused]] LanguageBinder g_languageBinder;

std::wstring Dollars(double cents)
{
    return Format(L"$%.2f", cents / 100.0);
}

std::wstring DollarsShort(double cents)
{
    return std::fmod(cents, 100.0) == 0.0 ? Format(L"$%.0f", cents / 100.0) : Dollars(cents);
}

std::wstring SpendText(const UsageInfo& info)
{
    auto text = Dollars(info.planUsedCents);
    if (info.planLimitCents && *info.planLimitCents > 0)
        text += L"/" + DollarsShort(*info.planLimitCents);
    return text;
}

std::wstring LocalTime(long long unix)
{
    ULARGE_INTEGER ticks{};
    ticks.QuadPart = static_cast<ULONGLONG>(unix + 11644473600LL) * 10000000ULL;
    FILETIME ft{ ticks.LowPart, ticks.HighPart };
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        return {};
    return Format(L"%04u-%02u-%02u %02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute);
}

// Official TrafficMonitor main window class (APP_CLASS_NAME in its stdafx.h).
constexpr wchar_t kOfficialMainClass[] = L"TrafficMonitor_r7XZaS4p";

bool IsOfficialClassName(HWND window)
{
    wchar_t name[64]{};
    return window && GetClassNameW(window, name, 64) > 0 && wcscmp(name, kOfficialMainClass) == 0;
}

BOOL CALLBACK FindOfficialMainWindow(HWND window, LPARAM param)
{
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == GetCurrentProcessId() && IsOfficialClassName(window))
    {
        *reinterpret_cast<bool*>(param) = true;
        return FALSE;
    }
    return TRUE;
}

bool IsOfficialTrafficMonitor(HWND parent)
{
    for (HWND window = parent; window; window = GetParent(window))
    {
        if (IsOfficialClassName(window))
            return true;
    }
    if (parent)
    {
        if (const HWND root = GetAncestor(parent, GA_ROOTOWNER); IsOfficialClassName(root))
            return true;
    }

    bool found = false;
    EnumWindows(FindOfficialMainWindow, reinterpret_cast<LPARAM>(&found));
    return found;
}

constexpr UINT kRefreshOptionsIcon = WM_APP + 2;

struct OptionsDialogState
{
    int* minutes = nullptr;
    DisplayFormat* display = nullptr;
    const Lang::Text* text = nullptr;
    bool themed = false;
    Theme::DialogState theme;
    HICON smallIcon = nullptr;
    HICON largeIcon = nullptr;
};

void SetOptionsIcon(HWND dialog, OptionsDialogState& state)
{
    const auto instance = reinterpret_cast<HINSTANCE>(&__ImageBase);
    const UINT dpi = GetDpiForWindow(dialog);
    const int smallSize = dpi ? GetSystemMetricsForDpi(SM_CXSMICON, dpi) : GetSystemMetrics(SM_CXSMICON);
    const int largeSize = dpi ? GetSystemMetricsForDpi(SM_CXICON, dpi) : GetSystemMetrics(SM_CXICON);
    const auto load = [instance](int size) {
        return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_LOGO), IMAGE_ICON,
            size, size, LR_DEFAULTCOLOR));
    };
    state.smallIcon = load(smallSize);
    state.largeIcon = load(largeSize);
    if (state.smallIcon)
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(state.smallIcon));
    if (state.largeIcon)
        SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(state.largeIcon));

    // DS_MODALFRAME adds WS_EX_DLGMODALFRAME, which hides the caption icon.
    SetWindowLongPtrW(dialog, GWL_EXSTYLE, GetWindowLongPtrW(dialog, GWL_EXSTYLE) & ~WS_EX_DLGMODALFRAME);
    PostMessageW(dialog, kRefreshOptionsIcon, 0, 0);
}

void DestroyOptionsIcon(OptionsDialogState& state)
{
    if (state.smallIcon)
        DestroyIcon(state.smallIcon);
    if (state.largeIcon)
        DestroyIcon(state.largeIcon);
    state.smallIcon = nullptr;
    state.largeIcon = nullptr;
}

OptionsDialogState* DialogState(HWND dialog)
{
    return reinterpret_cast<OptionsDialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
}

INT_PTR CALLBACK OptionsDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        auto* state = reinterpret_cast<OptionsDialogState*>(lParam);
        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
        if (state->text && state->text->rtl)
        {
            SetWindowLongPtrW(hDlg, GWL_EXSTYLE, GetWindowLongPtrW(hDlg, GWL_EXSTYLE) | WS_EX_LAYOUTRTL);
        }
        if (state->themed)
        {
            state->theme.hintId = IDC_INTERVAL_HINT;
            state->theme.editId = IDC_INTERVAL;
            Theme::Apply(hDlg, state->theme);
        }
        const Lang::Text& text = *state->text;
        SetWindowTextW(hDlg, text.optionsTitle);
        SetDlgItemTextW(hDlg, IDC_INTERVAL_LABEL, text.intervalLabel);
        SetDlgItemTextW(hDlg, IDC_INTERVAL_HINT,
            Format(text.intervalHint, DataManager::kMinMinutes, DataManager::kMaxMinutes, DataManager::kDefaultMinutes).c_str());
        SetDlgItemTextW(hDlg, IDC_SHOW_DECIMAL, text.showDecimal);
        SetDlgItemTextW(hDlg, IDC_HIDE_PERCENT, text.hidePercent);
        SetDlgItemTextW(hDlg, IDC_SEPARATE_SPACE, text.separateSpace);
        SetDlgItemTextW(hDlg, IDOK, text.ok);
        SetDlgItemTextW(hDlg, IDCANCEL, text.cancel);
        SetDlgItemInt(hDlg, IDC_INTERVAL, static_cast<UINT>(*state->minutes), FALSE);
        if (state->display)
        {
            CheckDlgButton(hDlg, IDC_SHOW_DECIMAL, state->display->showDecimal ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_HIDE_PERCENT, state->display->hidePercent ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_SEPARATE_SPACE, state->display->separateWithSpace ? BST_CHECKED : BST_UNCHECKED);
        }
        SendDlgItemMessageW(hDlg, IDC_INTERVAL, EM_LIMITTEXT, 4, 0);
        SetOptionsIcon(hDlg, *state);
        Theme::ApplyFont(hDlg, state->theme, state->text ? state->text->font : nullptr);
        if (state->themed)
            PostMessageW(hDlg, Theme::kApplyFrameMessage, 0, 0);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            auto* state = DialogState(hDlg);
            BOOL ok = FALSE;
            const UINT value = GetDlgItemInt(hDlg, IDC_INTERVAL, &ok, FALSE);
            if (!ok || value < static_cast<UINT>(DataManager::kMinMinutes) || value > static_cast<UINT>(DataManager::kMaxMinutes))
            {
                const Lang::Text& text = state && state->text ? *state->text : Lang::Current();
                MessageBoxW(hDlg,
                    Format(text.invalidInterval, DataManager::kMinMinutes, DataManager::kMaxMinutes).c_str(),
                    text.name, MB_ICONWARNING | MB_OK);
                SetFocus(GetDlgItem(hDlg, IDC_INTERVAL));
                return TRUE;
            }
            if (state && state->minutes)
                *state->minutes = static_cast<int>(value);
            if (state && state->display)
            {
                state->display->showDecimal = IsDlgButtonChecked(hDlg, IDC_SHOW_DECIMAL) == BST_CHECKED;
                state->display->hidePercent = IsDlgButtonChecked(hDlg, IDC_HIDE_PERCENT) == BST_CHECKED;
                state->display->separateWithSpace = IsDlgButtonChecked(hDlg, IDC_SEPARATE_SPACE) == BST_CHECKED;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;

    case kRefreshOptionsIcon:
        SetWindowPos(hDlg, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        return TRUE;

    default:
        break;
    }

    if (auto* state = DialogState(hDlg); state && state->themed)
    {
        INT_PTR handled = FALSE;
        if (Theme::HandleMessage(hDlg, state->theme, msg, wParam, lParam, handled))
            return handled;
    }
    return FALSE;
}

} // namespace

CCursorUsagePlugin& CCursorUsagePlugin::Instance()
{
    static CCursorUsagePlugin instance;
    return instance;
}

IPluginItem* CCursorUsagePlugin::GetItem(int index)
{
    switch (index)
    {
    case 0:
        return &m_plan;
    case 1:
        return &m_spend;
    case 2:
        return &m_onDemand;
    case 3:
        return &m_auto;
    case 4:
        return &m_api;
    default:
        return nullptr;
    }
}

void CCursorUsagePlugin::DataRequired()
{
    auto& data = DataManager::Instance();
    data.Tick();
    if (data.Version() == m_shownVersion)
        return;

    const auto snapshot = data.Snapshot();
    m_shownVersion = snapshot.version;
    if (!snapshot.hasData)
        return;

    const auto& info = snapshot.info;
    const DisplayFormat display = data.Display();
    auto setPercent = [&](CUsageItem& item, std::optional<double> value) {
        if (!value)
        {
            item.SetValue(L"--");
            return;
        }
        const double percent = std::clamp(*value, 0.0, 100.0);
        item.SetValue(FormatPercent(percent, display).c_str(), static_cast<float>(percent / 100.0));
    };

    if (info.hasPlan)
    {
        setPercent(m_plan, info.totalPercent);
        m_spend.SetValue(SpendText(info).c_str());
    }
    else
    {
        setPercent(m_plan, std::nullopt);
        m_spend.SetValue(L"--");
    }
    setPercent(m_auto, info.autoPercent);
    setPercent(m_api, info.apiPercent);
    m_onDemand.SetValue(info.onDemandEnabled ? Dollars(info.onDemandUsedCents).c_str() : L"$0");
}

const wchar_t* CCursorUsagePlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:
        return Lang::Current().name;
    case TMI_DESCRIPTION:
        return Lang::Current().description;
    case TMI_AUTHOR:
        return L"ema";
    case TMI_COPYRIGHT:
        return L"Copyright (C) 2026 ema";
    case TMI_VERSION:
        return L"1.0.0";
    case TMI_URL:
        return L"https://cursor.com/dashboard?tab=usage";
    default:
        return L"";
    }
}

ITMPlugin::OptionReturn CCursorUsagePlugin::ShowOptionsDialog(void* hParent)
{
    auto& data = DataManager::Instance();
    const int beforeMinutes = data.RefreshMinutes();
    const DisplayFormat beforeDisplay = data.Display();
    int minutes = beforeMinutes;
    DisplayFormat display = beforeDisplay;
    const Lang::Text& text = Lang::Get(Lang::FromLanguageId(m_app ? m_app->GetLanguageId() : 0));
    OptionsDialogState state;
    state.minutes = &minutes;
    state.display = &display;
    state.text = &text;
    state.themed = !IsOfficialTrafficMonitor(static_cast<HWND>(hParent));
    const INT_PTR result = [&] {
        if (!state.themed)
        {
            return DialogBoxParamW(reinterpret_cast<HINSTANCE>(&__ImageBase), MAKEINTRESOURCEW(IDD_OPTIONS_CLASSIC),
                static_cast<HWND>(hParent), OptionsDialogProc, reinterpret_cast<LPARAM>(&state));
        }
        const Theme::AppModeGuard appMode;
        return DialogBoxParamW(reinterpret_cast<HINSTANCE>(&__ImageBase), MAKEINTRESOURCEW(IDD_OPTIONS),
            static_cast<HWND>(hParent), OptionsDialogProc, reinterpret_cast<LPARAM>(&state));
    }();
    DestroyOptionsIcon(state);
    if (result != IDOK)
        return OR_OPTION_UNCHANGED;
    const bool minutesChanged = minutes != beforeMinutes;
    const bool displayChanged = !SameDisplay(display, beforeDisplay);
    if (!minutesChanged && !displayChanged)
        return OR_OPTION_UNCHANGED;
    if (minutesChanged)
        data.SetRefreshMinutes(minutes);
    if (displayChanged)
        data.SetDisplay(display);
    return OR_OPTION_CHANGED;
}

const wchar_t* CCursorUsagePlugin::GetTooltipInfo()
{
    const auto& text = Lang::Current();
    const auto snapshot = DataManager::Instance().Snapshot();
    const DisplayFormat display = DataManager::Instance().Display();
    std::wstring tip = text.name;

    if (snapshot.hasData)
    {
        const auto& info = snapshot.info;
        if (!info.email.empty())
        {
            tip += L"\r\n";
            tip += text.account;
            tip += info.email;
            if (!info.membership.empty())
            {
                tip += text.membershipOpen;
                tip += info.membership;
                tip += text.membershipClose;
            }
        }
        if (info.hasPlan)
        {
            tip += L"\r\n";
            tip += text.planUsage;
            tip += FormatPercent(info.totalPercent, display);
            if (info.autoPercent || info.apiPercent)
            {
                tip += text.detailOpen;
                tip += FormatPercent(info.autoPercent.value_or(0.0), display);
                tip += text.detailMiddle;
                tip += FormatPercent(info.apiPercent.value_or(0.0), display);
                tip += text.detailClose;
            }
            tip += L"\r\n";
            tip += text.planUsed;
            tip += Dollars(info.planUsedCents);
            if (info.planLimitCents && *info.planLimitCents > 0)
                tip += L" / " + Dollars(*info.planLimitCents);
            if (info.planBonusCents && *info.planBonusCents > 0)
            {
                tip += L"\r\n";
                tip += text.bonusUsed;
                tip += Dollars(*info.planBonusCents);
            }
        }
        if (info.onDemandEnabled)
        {
            tip += L"\r\n";
            tip += text.onDemand;
            tip += Dollars(info.onDemandUsedCents);
            if (info.onDemandLimitCents)
                tip += L" / " + Dollars(*info.onDemandLimitCents);
            if (info.onDemandIsTeam)
                tip += text.team;
        }
        else
        {
            tip += L"\r\n";
            tip += text.onDemandOff;
        }
        if (info.cycleEndUnix)
        {
            const auto end = LocalTime(*info.cycleEndUnix);
            if (!end.empty())
            {
                tip += L"\r\n";
                tip += text.cycleEnd;
                tip += end;
                const long long secondsLeft = *info.cycleEndUnix - static_cast<long long>(std::time(nullptr));
                if (secondsLeft > 0)
                {
                    const long long days = (secondsLeft + 86399) / 86400;
                    tip += Format(days == 1 ? text.dayLeft : text.daysLeft, days);
                }
            }
        }
        if (snapshot.lastSuccess)
        {
            const auto& t = *snapshot.lastSuccess;
            tip += L"\r\n";
            tip += Format(text.updated, t.wHour, t.wMinute, t.wSecond);
        }
    }

    if (snapshot.refreshing)
    {
        tip += L"\r\n";
        tip += text.refreshing;
    }
    if (!snapshot.lastError.empty())
    {
        tip += L"\r\n";
        if (snapshot.hasData)
            tip += text.refreshFailed;
        tip += snapshot.lastError;
    }

    wcsncpy_s(m_tooltip, tip.c_str(), _TRUNCATE);
    return m_tooltip;
}

void CCursorUsagePlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index == EI_CONFIG_DIR && data)
        DataManager::Instance().SetConfigDir(data);
}

int CCursorUsagePlugin::GetCommandCount()
{
    return 1;
}

const wchar_t* CCursorUsagePlugin::GetCommandName(int command_index)
{
    return command_index == 0 ? Lang::Current().refreshNow : nullptr;
}

void CCursorUsagePlugin::OnPluginCommand(int command_index, void*, void*)
{
    if (command_index == 0)
        DataManager::Instance().RefreshNow();
}

unsigned short CCursorUsagePlugin::LanguageId() const
{
    return m_app ? m_app->GetLanguageId() : 0;
}

void CCursorUsagePlugin::OnInitialize(ITrafficMonitor* pApp)
{
    m_app = pApp;
    if (!pApp)
        return;
    if (const wchar_t* dir = pApp->GetPluginConfigDir())
        DataManager::Instance().SetConfigDir(dir);
}

ITMPlugin* TMPluginGetInstance()
{
    return &CCursorUsagePlugin::Instance();
}
