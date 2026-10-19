#pragma once

#include <windows.h>

#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <uxtheme.h>

#include <algorithm>

// Dark theme for Win32 dialogs: system app theme, immersive caption, and drawn controls.
namespace Theme {

inline constexpr UINT kApplyFrameMessage = WM_APP + 1;

struct DialogState
{
    bool dark = false;
    HBRUSH background = nullptr;
    HBRUSH field = nullptr;
    HFONT font = nullptr;
    int hintId = 0;
    int editId = 0;

    DialogState() = default;
    DialogState(const DialogState&) = delete;
    DialogState& operator=(const DialogState&) = delete;

    ~DialogState()
    {
        if (background)
            DeleteObject(background);
        if (field)
            DeleteObject(field);
        if (font)
            DeleteObject(font);
    }
};

namespace detail {

constexpr COLORREF kBackground = RGB(20, 20, 20);
constexpr COLORREF kCaption = RGB(20, 20, 20);
constexpr COLORREF kText = RGB(250, 250, 250);
constexpr COLORREF kHint = RGB(168, 168, 168);
constexpr COLORREF kField = RGB(38, 38, 38);
constexpr COLORREF kButton = RGB(38, 38, 38);
constexpr COLORREF kButtonHot = RGB(52, 52, 52);
constexpr COLORREF kButtonPressed = RGB(30, 30, 30);
constexpr COLORREF kPrimaryButton = RGB(250, 250, 250);
constexpr COLORREF kPrimaryButtonHot = RGB(232, 232, 232);
constexpr COLORREF kPrimaryButtonPressed = RGB(214, 214, 214);
constexpr COLORREF kPrimaryText = RGB(20, 20, 20);
constexpr DWORD kDwmwaBorderColor = 34;
constexpr DWORD kDwmwaCaptionColor = 35;
constexpr DWORD kDwmwaTextColor = 36;
constexpr COLORREF kDwmwaColorDefault = 0xFFFFFFFF;
constexpr wchar_t kButtonProcProp[] = L"CursorUsageButtonProc";
constexpr wchar_t kButtonHotProp[] = L"CursorUsageButtonHot";
constexpr wchar_t kEditProcProp[] = L"CursorUsageEditProc";
constexpr wchar_t kEditStyleProp[] = L"CursorUsageEditStyle";
constexpr wchar_t kEditExStyleProp[] = L"CursorUsageEditExStyle";

// uxtheme ordinals are undocumented. 135 is AllowDarkModeForApp on 1809 and SetPreferredAppMode from 1903.
enum class PreferredAppMode
{
    Default,
    AllowDark,
    ForceDark,
    ForceLight,
};

using AllowDarkModeForWindowFn = bool(WINAPI*)(HWND, bool);
using AllowDarkModeForAppFn = bool(WINAPI*)(bool);
using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using RefreshImmersiveColorPolicyStateFn = void(WINAPI*)();

struct DarkModeApi
{
    bool supported = false;
    AllowDarkModeForWindowFn allowWindow = nullptr;
    AllowDarkModeForAppFn allowApp = nullptr;
    SetPreferredAppModeFn setPreferred = nullptr;
    RefreshImmersiveColorPolicyStateFn refreshPolicy = nullptr;
};

inline FARPROC ThemeOrdinal(HMODULE module, int ordinal)
{
    return GetProcAddress(module, MAKEINTRESOURCEA(ordinal));
}

inline const DarkModeApi& DarkModeApis()
{
    static const DarkModeApi apis = [] {
        DarkModeApi api;
        using RtlGetNtVersionNumbersFn = void(WINAPI*)(DWORD*, DWORD*, DWORD*);
        const auto rtlGetNtVersionNumbers = reinterpret_cast<RtlGetNtVersionNumbersFn>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers"));
        if (!rtlGetNtVersionNumbers)
            return api;

        DWORD major = 0;
        DWORD minor = 0;
        DWORD build = 0;
        rtlGetNtVersionNumbers(&major, &minor, &build);
        build &= ~0xF0000000;
        if (major < 10 || build < 17763)
            return api;

        const HMODULE theme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!theme)
            return api;

#pragma warning(push)
#pragma warning(disable : 4191)
        api.allowWindow = reinterpret_cast<AllowDarkModeForWindowFn>(ThemeOrdinal(theme, 133));
        api.refreshPolicy = reinterpret_cast<RefreshImmersiveColorPolicyStateFn>(ThemeOrdinal(theme, 104));
        const FARPROC ordinal135 = ThemeOrdinal(theme, 135);
        if (build < 18362)
            api.allowApp = reinterpret_cast<AllowDarkModeForAppFn>(ordinal135);
        else
            api.setPreferred = reinterpret_cast<SetPreferredAppModeFn>(ordinal135);
#pragma warning(pop)

        api.supported = api.allowWindow && (api.allowApp || api.setPreferred);
        return api;
    }();
    return apis;
}

inline bool AppsUseDarkMode()
{
    HIGHCONTRASTW highContrast{};
    highContrast.cbSize = sizeof(highContrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, FALSE)
        && (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0)
        return false;

    DWORD light = 1;
    DWORD size = sizeof(light);
    if (RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS)
        return false;
    return light == 0;
}

struct CompositionAttributeData
{
    DWORD attribute;
    void* data;
    SIZE_T size;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, CompositionAttributeData*);

inline void SetDarkWindowFrame(HWND window, bool dark)
{
    const BOOL value = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(window, 20, &value, sizeof(value))))
        DwmSetWindowAttribute(window, 19, &value, sizeof(value));

    const auto setComposition = reinterpret_cast<SetWindowCompositionAttributeFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
    if (setComposition)
    {
        BOOL compositionValue = value;
        CompositionAttributeData attribute{ 26, &compositionValue, sizeof(compositionValue) };
        setComposition(window, &attribute);
    }

    const COLORREF caption = dark ? kCaption : kDwmwaColorDefault;
    const COLORREF text = dark ? kText : kDwmwaColorDefault;
    DwmSetWindowAttribute(window, kDwmwaCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(window, kDwmwaTextColor, &text, sizeof(text));
    // Leave the frame border to Windows so it follows the accent color and active/inactive state.
    DwmSetWindowAttribute(window, kDwmwaBorderColor, &kDwmwaColorDefault, sizeof(kDwmwaColorDefault));
}

inline void EnsureGdiplus()
{
    static ULONG_PTR token = 0;
    if (token != 0)
        return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, nullptr);
}

inline float CornerRadius(HWND window)
{
    const UINT dpi = GetDpiForWindow(window);
    return std::max(4.f, 6.f * static_cast<float>(dpi) / 96.f);
}

inline Gdiplus::Color GpColor(COLORREF color)
{
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

inline void AddRoundRect(Gdiplus::GraphicsPath& path, float x, float y, float width, float height, float radius)
{
    const float limit = std::min(width, height) / 2.f;
    radius = std::min(radius, std::max(1.f, limit));
    const float diameter = radius * 2.f;
    path.AddArc(x, y, diameter, diameter, 180.f, 90.f);
    path.AddArc(x + width - diameter, y, diameter, diameter, 270.f, 90.f);
    path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0.f, 90.f);
    path.AddArc(x, y + height - diameter, diameter, diameter, 90.f, 90.f);
    path.CloseFigure();
}

inline LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const auto previous = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kButtonProcProp));
    if (msg == WM_MOUSEMOVE && !GetPropW(hwnd, kButtonHotProp))
    {
        SetPropW(hwnd, kButtonHotProp, reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&track);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    else if (msg == WM_MOUSELEAVE)
    {
        RemovePropW(hwnd, kButtonHotProp);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    else if (msg == WM_NCDESTROY)
    {
        RemovePropW(hwnd, kButtonProcProp);
        RemovePropW(hwnd, kButtonHotProp);
    }

    return previous ? CallWindowProcW(previous, hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

inline void SubclassButton(HWND hwnd)
{
    if (GetPropW(hwnd, kButtonProcProp))
        return;
    const auto previous = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ButtonProc)));
    if (previous)
        SetPropW(hwnd, kButtonProcProp, reinterpret_cast<HANDLE>(previous));
}

inline void UnsubclassButton(HWND hwnd)
{
    const auto previous = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kButtonProcProp));
    if (!previous)
        return;
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
    RemovePropW(hwnd, kButtonProcProp);
    RemovePropW(hwnd, kButtonHotProp);
}

inline void SetButtonType(HWND hwnd, LONG type)
{
    const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    SetWindowLongW(hwnd, GWL_STYLE, (style & ~BS_TYPEMASK) | type);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

inline int EditTextHeight(HWND hwnd)
{
    const auto font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
    const HDC hdc = GetDC(hwnd);
    if (!hdc)
        return 0;
    const HGDIOBJ previous = font ? SelectObject(hdc, font) : nullptr;
    TEXTMETRICW metrics{};
    GetTextMetricsW(hdc, &metrics);
    if (previous)
        SelectObject(hdc, previous);
    ReleaseDC(hwnd, hdc);
    return metrics.tmHeight;
}

inline void PlaceEditText(HWND hwnd, RECT& client)
{
    const int textHeight = EditTextHeight(hwnd);
    const int height = client.bottom - client.top;
    if (textHeight > 0 && textHeight < height)
    {
        const int pad = (height - textHeight) / 2;
        client.top += pad;
        client.bottom = client.top + textHeight;
    }

    const UINT dpi = GetDpiForWindow(hwnd);
    const int leftPad = std::max(8, MulDiv(10, static_cast<int>(dpi), 96));
    const int rightPad = std::max(6, MulDiv(8, static_cast<int>(dpi), 96));
    if (client.right - client.left > leftPad + rightPad + 8)
    {
        client.left += leftPad;
        client.right -= rightPad;
    }
}

inline void PaintEditChrome(HWND hwnd)
{
    RECT window{};
    if (!GetWindowRect(hwnd, &window))
        return;
    const float width = static_cast<float>(window.right - window.left);
    const float height = static_cast<float>(window.bottom - window.top);
    if (width <= 1.f || height <= 1.f)
        return;

    const HDC hdc = GetWindowDC(hwnd);
    if (!hdc)
        return;

    EnsureGdiplus();
    const float radius = std::min(CornerRadius(hwnd), std::min(width, height) / 2.f);
    {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        Gdiplus::GraphicsPath cover(Gdiplus::FillModeAlternate);
        cover.AddRectangle(Gdiplus::RectF(0.f, 0.f, width, height));
        AddRoundRect(cover, 0.f, 0.f, width, height, radius);
        const Gdiplus::SolidBrush background(GpColor(kBackground));
        graphics.FillPath(&background, &cover);

        Gdiplus::GraphicsPath field;
        AddRoundRect(field, 0.5f, 0.5f, width - 1.f, height - 1.f, radius);
        const Gdiplus::SolidBrush fill(GpColor(kField));
        graphics.FillPath(&fill, &field);

        const bool focused = GetFocus() == hwnd;
        const Gdiplus::Pen pen(GpColor(focused ? RGB(88, 88, 88) : RGB(58, 58, 58)), 1.f);
        graphics.DrawPath(&pen, &field);
    }
    ReleaseDC(hwnd, hdc);
}

inline LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const auto previous = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kEditProcProp));
    if (msg == WM_NCCALCSIZE && wParam)
    {
        const LRESULT result = previous
            ? CallWindowProcW(previous, hwnd, msg, wParam, lParam)
            : 0;
        PlaceEditText(hwnd, reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0]);
        return result;
    }
    if (msg == WM_NCPAINT)
    {
        PaintEditChrome(hwnd);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        const HBRUSH brush = CreateSolidBrush(kField);
        FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
        DeleteObject(brush);
        return 1;
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS)
    {
        const LRESULT result = previous
            ? CallWindowProcW(previous, hwnd, msg, wParam, lParam)
            : DefWindowProcW(hwnd, msg, wParam, lParam);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
        return result;
    }
    if (msg == WM_NCDESTROY)
        RemovePropW(hwnd, kEditProcProp);

    return previous ? CallWindowProcW(previous, hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

inline void SubclassEdit(HWND hwnd)
{
    if (GetPropW(hwnd, kEditProcProp))
        return;
    if (!GetPropW(hwnd, kEditStyleProp))
    {
        SetPropW(hwnd, kEditStyleProp, reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(GetWindowLongW(hwnd, GWL_STYLE))));
        SetPropW(hwnd, kEditExStyleProp, reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(GetWindowLongW(hwnd, GWL_EXSTYLE))));
    }
    SetWindowLongW(hwnd, GWL_STYLE, GetWindowLongW(hwnd, GWL_STYLE) & ~(WS_BORDER | ES_CENTER));
    SetWindowLongW(hwnd, GWL_EXSTYLE, GetWindowLongW(hwnd, GWL_EXSTYLE) & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    const auto previous = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc)));
    if (previous)
        SetPropW(hwnd, kEditProcProp, reinterpret_cast<HANDLE>(previous));
}

inline void UnsubclassEdit(HWND hwnd)
{
    const auto previous = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kEditProcProp));
    if (previous)
    {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
        RemovePropW(hwnd, kEditProcProp);
    }
    if (const auto style = reinterpret_cast<LONG_PTR>(GetPropW(hwnd, kEditStyleProp)))
    {
        SetWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(style));
        RemovePropW(hwnd, kEditStyleProp);
    }
    if (const auto exStyle = reinterpret_cast<LONG_PTR>(GetPropW(hwnd, kEditExStyleProp)))
    {
        SetWindowLongW(hwnd, GWL_EXSTYLE, static_cast<LONG>(exStyle));
        RemovePropW(hwnd, kEditExStyleProp);
    }
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

inline BOOL CALLBACK ThemeChild(HWND child, LPARAM param)
{
    const bool dark = param != FALSE;
    wchar_t className[32]{};
    if (GetClassNameW(child, className, 32) == 0)
        return TRUE;

    if (_wcsicmp(className, L"Button") == 0)
    {
        // DarkMode_Explorer leaves push buttons light, so draw them ourselves.
        // Checkboxes keep the explorer theme so the box and caption stay readable.
        const DWORD type = static_cast<DWORD>(GetWindowLongW(child, GWL_STYLE)) & BS_TYPEMASK;
        if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON)
        {
            SetWindowTheme(child, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
            return TRUE;
        }
        if (dark && (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || type == BS_OWNERDRAW))
        {
            if (type != BS_OWNERDRAW)
                SetButtonType(child, BS_OWNERDRAW);
            SubclassButton(child);
            SetWindowTheme(child, L"", L"");
            return TRUE;
        }
        if (!dark && type == BS_OWNERDRAW)
        {
            UnsubclassButton(child);
            SetButtonType(child, GetDlgCtrlID(child) == IDOK ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
            SetWindowTheme(child, nullptr, nullptr);
            return TRUE;
        }
    }
    else if (_wcsicmp(className, L"Edit") == 0)
    {
        if (dark)
        {
            SetWindowTheme(child, L"", L"");
            SubclassEdit(child);
        }
        else
        {
            UnsubclassEdit(child);
            SetWindowTheme(child, nullptr, nullptr);
        }
        return TRUE;
    }

    SetWindowTheme(child, dark ? L"" : nullptr, dark ? L"" : nullptr);
    return TRUE;
}

inline void DrawButton(const DRAWITEMSTRUCT* item)
{
    const bool primary = GetDlgCtrlID(item->hwndItem) == IDOK;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool hot = GetPropW(item->hwndItem, kButtonHotProp) != nullptr;

    COLORREF fill = primary ? kPrimaryButton : kButton;
    if (!disabled)
    {
        if (pressed)
            fill = primary ? kPrimaryButtonPressed : kButtonPressed;
        else if (hot)
            fill = primary ? kPrimaryButtonHot : kButtonHot;
    }

    RECT client = item->rcItem;
    const HBRUSH background = CreateSolidBrush(kBackground);
    FillRect(item->hDC, &client, background);
    DeleteObject(background);

    const float width = static_cast<float>(client.right - client.left);
    const float height = static_cast<float>(client.bottom - client.top);
    const float radius = std::min(CornerRadius(item->hwndItem), std::min(width, height) / 2.f);
    EnsureGdiplus();
    {
        Gdiplus::Graphics graphics(item->hDC);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::GraphicsPath shape;
        AddRoundRect(shape,
            static_cast<float>(client.left),
            static_cast<float>(client.top),
            width,
            height,
            radius);
        const Gdiplus::SolidBrush brush(GpColor(fill));
        graphics.FillPath(&brush, &shape);
    }

    wchar_t text[64]{};
    GetWindowTextW(item->hwndItem, text, 64);
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ previousFont = font ? SelectObject(item->hDC, font) : nullptr;
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, disabled ? kHint : (primary ? kPrimaryText : kText));
    DrawTextW(item->hDC, text, -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (previousFont)
        SelectObject(item->hDC, previousFont);
}

inline BOOL CALLBACK ApplyFontToChild(HWND child, LPARAM font)
{
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
    return TRUE;
}

} // namespace detail

class AppModeGuard
{
public:
    AppModeGuard()
    {
        const auto& api = detail::DarkModeApis();
        if (!api.supported)
            return;
        if (api.refreshPolicy)
            api.refreshPolicy();
        if (api.setPreferred)
        {
            m_previous = api.setPreferred(detail::PreferredAppMode::AllowDark);
            m_restorePreferred = true;
        }
        else if (api.allowApp)
        {
            api.allowApp(true);
            m_restoreAllowApp = true;
        }
    }

    ~AppModeGuard()
    {
        const auto& api = detail::DarkModeApis();
        if (m_restorePreferred && api.setPreferred)
            api.setPreferred(m_previous);
        else if (m_restoreAllowApp && api.allowApp)
            api.allowApp(false);
    }

    AppModeGuard(const AppModeGuard&) = delete;
    AppModeGuard& operator=(const AppModeGuard&) = delete;

private:
    detail::PreferredAppMode m_previous = detail::PreferredAppMode::Default;
    bool m_restorePreferred = false;
    bool m_restoreAllowApp = false;
};

inline void Apply(HWND dialog, DialogState& state)
{
    const bool dark = detail::DarkModeApis().supported && detail::AppsUseDarkMode();
    if (dark == state.dark)
        return;

    if (state.background)
    {
        DeleteObject(state.background);
        state.background = nullptr;
    }
    if (state.field)
    {
        DeleteObject(state.field);
        state.field = nullptr;
    }
    if (dark)
    {
        state.background = CreateSolidBrush(detail::kBackground);
        state.field = CreateSolidBrush(detail::kField);
        if (!state.background || !state.field)
            return;
    }

    state.dark = dark;
    if (const auto allowWindow = detail::DarkModeApis().allowWindow)
        allowWindow(dialog, dark);
    // An empty theme class forces a classic light caption. DarkMode_Explorer keeps the immersive frame.
    SetWindowTheme(dialog, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    EnumChildWindows(dialog, detail::ThemeChild, dark ? TRUE : FALSE);
    detail::SetDarkWindowFrame(dialog, dark);
    // SetWindowPos(SWP_FRAMECHANGED) during WM_INITDIALOG makes DialogBox leave the window hidden.
    if (IsWindowVisible(dialog))
    {
        SetWindowPos(dialog, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        RedrawWindow(dialog, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    }
}

inline void ApplyFont(HWND dialog, DialogState& state, const wchar_t* face = nullptr)
{
    if (!state.font)
    {
        LOGFONTW font{};
        if (const auto current = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0)))
            GetObjectW(current, sizeof(font), &font);
        else
            font.lfHeight = -12;
        font.lfWeight = FW_NORMAL;
        font.lfQuality = CLEARTYPE_QUALITY;
        font.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(font.lfFaceName, face && *face ? face : L"Segoe UI");
        state.font = CreateFontIndirectW(&font);
    }
    if (!state.font)
        return;

    SendMessageW(dialog, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), FALSE);
    EnumChildWindows(dialog, detail::ApplyFontToChild, reinterpret_cast<LPARAM>(state.font));
    if (state.editId)
    {
        if (const HWND edit = GetDlgItem(dialog, state.editId))
        {
            SetWindowPos(edit, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }
}

inline void RefreshFrame(HWND dialog)
{
    detail::SetDarkWindowFrame(dialog, true);
    SetWindowPos(dialog, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// Paint, color, and theme-change messages. Returns true when the dialog procedure should return `result`.
inline bool HandleMessage(HWND dialog, DialogState& state, UINT msg, WPARAM wParam, LPARAM lParam, INT_PTR& result)
{
    switch (msg)
    {
    case kApplyFrameMessage:
        if (state.dark)
            RefreshFrame(dialog);
        result = TRUE;
        return true;

    case WM_DRAWITEM:
    {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (!state.dark || !item || item->CtlType != ODT_BUTTON)
            return false;
        detail::DrawButton(item);
        result = TRUE;
        return true;
    }

    case WM_ERASEBKGND:
        if (!state.dark || !state.background)
            return false;
        {
            RECT rect{};
            GetClientRect(dialog, &rect);
            FillRect(reinterpret_cast<HDC>(wParam), &rect, state.background);
        }
        result = TRUE;
        return true;

    case WM_CTLCOLOREDIT:
        if (!state.dark || !state.field)
            return false;
        SetTextColor(reinterpret_cast<HDC>(wParam), detail::kText);
        SetBkColor(reinterpret_cast<HDC>(wParam), detail::kField);
        result = reinterpret_cast<INT_PTR>(state.field);
        return true;

    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
        if (!state.dark || !state.background)
            return false;
        {
            const auto hdc = reinterpret_cast<HDC>(wParam);
            const bool hint = msg == WM_CTLCOLORSTATIC && GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == state.hintId
                && state.hintId != 0;
            SetTextColor(hdc, hint ? detail::kHint : detail::kText);
            SetBkColor(hdc, detail::kBackground);
            result = reinterpret_cast<INT_PTR>(state.background);
        }
        return true;

    case WM_SETTINGCHANGE:
        if (lParam
            && CompareStringOrdinal(reinterpret_cast<LPCWSTR>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
        {
            if (const auto refreshPolicy = detail::DarkModeApis().refreshPolicy)
                refreshPolicy();
        }
        Apply(dialog, state);
        return false;

    default:
        return false;
    }
}

} // namespace Theme
