#pragma once

#include <optional>
#include <string>

struct UsageInfo
{
    std::wstring email;
    std::wstring membership;

    bool hasPlan = false;
    double totalPercent = 0;
    std::optional<double> autoPercent;
    std::optional<double> apiPercent;
    double planUsedCents = 0;
    std::optional<double> planLimitCents;
    std::optional<double> planBonusCents;

    bool onDemandEnabled = false;
    bool onDemandIsTeam = false;
    double onDemandUsedCents = 0;
    std::optional<double> onDemandLimitCents;

    std::optional<long long> cycleEndUnix;
};

struct FetchResult
{
    bool ok = false;
    UsageInfo info;
    std::wstring error;
};

// Blocking: discovers the local Cursor session and calls cursor.com. Run off the UI thread.
FetchResult FetchUsage();
