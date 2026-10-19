#pragma once

#include "Display.h"
#include "UsageClient.h"

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

struct UsageSnapshot
{
    uint64_t version = 0;
    bool hasData = false;
    UsageInfo info;
    bool refreshing = false;
    std::optional<SYSTEMTIME> lastSuccess;
    std::wstring lastError;
};

class DataManager
{
public:
    static constexpr int kDefaultMinutes = 5;
    static constexpr int kMinMinutes = 1;
    static constexpr int kMaxMinutes = 1440;
    static constexpr ULONGLONG kRetryMs = 60 * 1000;

    static DataManager& Instance();

    void SetConfigDir(const std::wstring& dir);
    int RefreshMinutes() const;
    void SetRefreshMinutes(int minutes);
    DisplayFormat Display() const;
    void SetDisplay(const DisplayFormat& format);

    // Cheap; called every DataRequired. Starts a background refresh when one is due.
    void Tick();
    void RefreshNow();

    uint64_t Version() const;
    UsageSnapshot Snapshot() const;

private:
    DataManager() = default;
    void Run();
    ULONGLONG DelayMs() const;

    mutable std::mutex m_mutex;
    UsageSnapshot m_snapshot;
    std::wstring m_iniPath;
    int m_refreshMinutes = kDefaultMinutes;
    DisplayFormat m_display;
    bool m_busy = false;
    bool m_attempted = false;
    bool m_lastFailed = false;
    ULONGLONG m_lastFinished = 0;
};
