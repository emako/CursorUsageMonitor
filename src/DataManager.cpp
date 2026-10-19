#include "DataManager.h"

#include "Text.h"

#include <algorithm>
#include <exception>
#include <thread>

namespace
{
constexpr wchar_t kIniSection[] = L"config";
constexpr wchar_t kIniRefreshKey[] = L"refresh_minutes";
constexpr wchar_t kShowDecimalKey[] = L"show_decimal";
constexpr wchar_t kHidePercentKey[] = L"hide_percent";
constexpr wchar_t kSeparateKey[] = L"separate_value_unit_with_space";

int ClampMinutes(int minutes)
{
    return std::clamp(minutes, DataManager::kMinMinutes, DataManager::kMaxMinutes);
}

bool ReadBool(const wchar_t* key, bool fallback, const std::wstring& path)
{
    return GetPrivateProfileIntW(kIniSection, key, fallback ? 1 : 0, path.c_str()) != 0;
}

DisplayFormat ReadDisplay(const std::wstring& path)
{
    DisplayFormat format;
    format.showDecimal = ReadBool(kShowDecimalKey, false, path);
    format.hidePercent = ReadBool(kHidePercentKey, false, path);
    format.separateWithSpace = ReadBool(kSeparateKey, false, path);
    return format;
}

void WriteBool(const wchar_t* key, bool value, const std::wstring& path)
{
    WritePrivateProfileStringW(kIniSection, key, value ? L"1" : L"0", path.c_str());
}

void WriteDisplay(const std::wstring& path, const DisplayFormat& format)
{
    if (path.empty())
        return;
    WriteBool(kShowDecimalKey, format.showDecimal, path);
    WriteBool(kHidePercentKey, format.hidePercent, path);
    WriteBool(kSeparateKey, format.separateWithSpace, path);
}
} // namespace

DataManager& DataManager::Instance()
{
    // Never destroyed: a detached refresh thread may still be running when the process exits.
    static DataManager* instance = new DataManager();
    return *instance;
}

void DataManager::SetConfigDir(const std::wstring& dir)
{
    if (dir.empty())
        return;
    std::lock_guard lock(m_mutex);
    m_iniPath = dir;
    if (m_iniPath.back() != L'\\' && m_iniPath.back() != L'/')
        m_iniPath += L'\\';
    m_iniPath += L"CursorUsageMonitor.ini";
    m_refreshMinutes = ClampMinutes(static_cast<int>(
        GetPrivateProfileIntW(kIniSection, kIniRefreshKey, kDefaultMinutes, m_iniPath.c_str())));
    const DisplayFormat display = ReadDisplay(m_iniPath);
    const bool displayChanged = !SameDisplay(display, m_display);
    m_display = display;
    if (displayChanged && m_snapshot.hasData)
        ++m_snapshot.version;
}

int DataManager::RefreshMinutes() const
{
    std::lock_guard lock(m_mutex);
    return m_refreshMinutes;
}

void DataManager::SetRefreshMinutes(int minutes)
{
    std::lock_guard lock(m_mutex);
    m_refreshMinutes = ClampMinutes(minutes);
    if (!m_iniPath.empty())
    {
        WritePrivateProfileStringW(kIniSection, kIniRefreshKey,
            std::to_wstring(m_refreshMinutes).c_str(), m_iniPath.c_str());
    }
}

DisplayFormat DataManager::Display() const
{
    std::lock_guard lock(m_mutex);
    return m_display;
}

void DataManager::SetDisplay(const DisplayFormat& format)
{
    std::lock_guard lock(m_mutex);
    if (SameDisplay(format, m_display))
        return;
    m_display = format;
    WriteDisplay(m_iniPath, m_display);
    if (m_snapshot.hasData)
        ++m_snapshot.version;
}

ULONGLONG DataManager::DelayMs() const
{
    const ULONGLONG full = static_cast<ULONGLONG>(m_refreshMinutes) * 60 * 1000;
    return m_lastFailed ? (std::min)(full, kRetryMs) : full;
}

void DataManager::Tick()
{
    {
        std::lock_guard lock(m_mutex);
        if (m_busy)
            return;
        if (m_attempted && GetTickCount64() - m_lastFinished < DelayMs())
            return;
        m_busy = true;
        m_attempted = true;
        m_snapshot.refreshing = true;
        ++m_snapshot.version;
    }

    try
    {
        std::thread([this] { Run(); }).detach();
    }
    catch (const std::exception&)
    {
        std::lock_guard lock(m_mutex);
        m_busy = false;
        m_lastFailed = true;
        m_lastFinished = GetTickCount64();
        m_snapshot.refreshing = false;
        m_snapshot.lastError = L"无法启动后台刷新线程";
        ++m_snapshot.version;
    }
}

void DataManager::RefreshNow()
{
    {
        std::lock_guard lock(m_mutex);
        m_attempted = false;
    }
    Tick();
}

void DataManager::Run()
{
    FetchResult result;
    try
    {
        result = FetchUsage();
    }
    catch (const std::exception& ex)
    {
        result.error = L"采集出错：" + Utf8ToWide(ex.what());
    }
    catch (...)
    {
        result.error = L"采集出错";
    }

    std::lock_guard lock(m_mutex);
    if (result.ok)
    {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        m_snapshot.hasData = true;
        m_snapshot.info = std::move(result.info);
        m_snapshot.lastSuccess = now;
        m_snapshot.lastError.clear();
    }
    else
    {
        m_snapshot.lastError = std::move(result.error);
    }
    m_lastFailed = !result.ok;
    m_lastFinished = GetTickCount64();
    m_busy = false;
    m_snapshot.refreshing = false;
    ++m_snapshot.version;
}

uint64_t DataManager::Version() const
{
    std::lock_guard lock(m_mutex);
    return m_snapshot.version;
}

UsageSnapshot DataManager::Snapshot() const
{
    std::lock_guard lock(m_mutex);
    return m_snapshot;
}
