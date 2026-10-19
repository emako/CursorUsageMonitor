#include "Plugin.h"

#include "DataManager.h"
#include "Text.h"
#include "Theme.hpp"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <ctime>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
constexpr wchar_t kPluginName[] = L"Cursor 用量";

std::wstring Percent(double value)
{
    return value < 9.95 ? Format(L"%.1f%%", value) : Format(L"%.0f%%", value);
}

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
        if (state->themed)
        {
            state->theme.hintId = IDC_INTERVAL_HINT;
            state->theme.editId = IDC_INTERVAL;
            Theme::Apply(hDlg, state->theme);
        }
        SetWindowTextW(hDlg, L"Cursor 用量 选项");
        SetDlgItemTextW(hDlg, IDC_INTERVAL_LABEL, L"刷新间隔（分钟）：");
        SetDlgItemTextW(hDlg, IDC_INTERVAL_HINT,
            Format(L"范围 %d–%d，默认 %d；刷新失败时 1 分钟后重试。",
                DataManager::kMinMinutes, DataManager::kMaxMinutes, DataManager::kDefaultMinutes).c_str());
        SetDlgItemTextW(hDlg, IDOK, L"确定");
        SetDlgItemTextW(hDlg, IDCANCEL, L"取消");
        SetDlgItemInt(hDlg, IDC_INTERVAL, static_cast<UINT>(*state->minutes), FALSE);
        SendDlgItemMessageW(hDlg, IDC_INTERVAL, EM_LIMITTEXT, 4, 0);
        SetOptionsIcon(hDlg, *state);
        if (state->themed)
        {
            Theme::ApplyFont(hDlg, state->theme);
            PostMessageW(hDlg, Theme::kApplyFrameMessage, 0, 0);
        }
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
                MessageBoxW(hDlg,
                    Format(L"请输入 %d 到 %d 之间的整数。", DataManager::kMinMinutes, DataManager::kMaxMinutes).c_str(),
                    kPluginName, MB_ICONWARNING | MB_OK);
                SetFocus(GetDlgItem(hDlg, IDC_INTERVAL));
                return TRUE;
            }
            if (state && state->minutes)
                *state->minutes = static_cast<int>(value);
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
    auto setPercent = [](CUsageItem& item, std::optional<double> value) {
        if (!value)
        {
            item.SetValue(L"--");
            return;
        }
        const double percent = std::clamp(*value, 0.0, 100.0);
        item.SetValue(Percent(percent).c_str(), static_cast<float>(percent / 100.0));
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
        return kPluginName;
    case TMI_DESCRIPTION:
        return L"在任务栏显示本机 Cursor 账号的套餐占用、已用金额和按量费用。";
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
    const int before = data.RefreshMinutes();
    int minutes = before;
    OptionsDialogState state;
    state.minutes = &minutes;
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
    if (result != IDOK || minutes == before)
        return OR_OPTION_UNCHANGED;
    data.SetRefreshMinutes(minutes);
    return OR_OPTION_CHANGED;
}

const wchar_t* CCursorUsagePlugin::GetTooltipInfo()
{
    const auto snapshot = DataManager::Instance().Snapshot();
    std::wstring tip = kPluginName;

    if (snapshot.hasData)
    {
        const auto& info = snapshot.info;
        if (!info.email.empty())
        {
            tip += L"\r\n账号：" + info.email;
            if (!info.membership.empty())
                tip += L"（" + info.membership + L"）";
        }
        if (info.hasPlan)
        {
            tip += L"\r\n套餐占用：" + Format(L"%.1f%%", info.totalPercent);
            if (info.autoPercent || info.apiPercent)
            {
                tip += Format(L"（Auto %.1f%% · API %.1f%%）",
                    info.autoPercent.value_or(0.0), info.apiPercent.value_or(0.0));
            }
            tip += L"\r\n套餐已用：" + Dollars(info.planUsedCents);
            if (info.planLimitCents && *info.planLimitCents > 0)
                tip += L" / " + Dollars(*info.planLimitCents);
            if (info.planBonusCents && *info.planBonusCents > 0)
                tip += L"\r\n赠送额度已用：" + Dollars(*info.planBonusCents);
        }
        if (info.onDemandEnabled)
        {
            tip += L"\r\n按量费用：" + Dollars(info.onDemandUsedCents);
            if (info.onDemandLimitCents)
                tip += L" / " + Dollars(*info.onDemandLimitCents);
            if (info.onDemandIsTeam)
                tip += L"（团队）";
        }
        else
        {
            tip += L"\r\n按量费用：未开启";
        }
        if (info.cycleEndUnix)
        {
            const auto end = LocalTime(*info.cycleEndUnix);
            if (!end.empty())
            {
                tip += L"\r\n账期结束：" + end;
                const long long secondsLeft = *info.cycleEndUnix - static_cast<long long>(std::time(nullptr));
                if (secondsLeft > 0)
                    tip += Format(L"（剩 %lld 天）", (secondsLeft + 86399) / 86400);
            }
        }
        if (snapshot.lastSuccess)
        {
            const auto& t = *snapshot.lastSuccess;
            tip += Format(L"\r\n更新时间：%02u:%02u:%02u", t.wHour, t.wMinute, t.wSecond);
        }
    }

    if (snapshot.refreshing)
        tip += L"\r\n正在刷新…";
    if (!snapshot.lastError.empty())
        tip += (snapshot.hasData ? L"\r\n上次刷新失败：" : L"\r\n") + snapshot.lastError;

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
    return command_index == 0 ? L"立即刷新" : nullptr;
}

void CCursorUsagePlugin::OnPluginCommand(int command_index, void*, void*)
{
    if (command_index == 0)
        DataManager::Instance().RefreshNow();
}

void CCursorUsagePlugin::OnInitialize(ITrafficMonitor* pApp)
{
    if (!pApp)
        return;
    if (const wchar_t* dir = pApp->GetPluginConfigDir())
        DataManager::Instance().SetConfigDir(dir);
}

ITMPlugin* TMPluginGetInstance()
{
    return &CCursorUsagePlugin::Instance();
}
