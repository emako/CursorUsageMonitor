#include "DataManager.h"

#include "Text.h"

#include <algorithm>
#include <exception>
#include <thread>

namespace
{
constexpr wchar_t kIniSection[] = L"config";
constexpr wchar_t kIniRefreshKey[] = L"refresh_minutes";

int ClampMinutes(int minutes)
{
    return std::clamp(minutes, DataManager::kMinMinutes, DataManager::kMaxMinutes);
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
