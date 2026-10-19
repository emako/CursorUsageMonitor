#include "UsageClient.h"

#include "Session.h"
#include "Text.h"

#include <winhttp.h>

#include <cstdio>
#include <ctime>

#include "json.hpp"

using json = nlohmann::json;

namespace
{
constexpr wchar_t kHost[] = L"cursor.com";
constexpr wchar_t kUserAgent[] = L"cursor-usage-traffic-monitor/1.0";
constexpr size_t kMaxBody = 8 * 1024 * 1024;

struct HttpResponse
{
    DWORD status = 0;
    DWORD error = 0;
    std::string body;
};

class InternetHandle
{
public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET handle) : m_handle(handle) {}
    ~InternetHandle()
    {
        if (m_handle)
            WinHttpCloseHandle(m_handle);
    }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    InternetHandle& operator=(HINTERNET handle)
    {
        if (m_handle)
            WinHttpCloseHandle(m_handle);
        m_handle = handle;
        return *this;
    }
    operator HINTERNET() const { return m_handle; }

private:
    HINTERNET m_handle = nullptr;
};

class DashboardClient
{
public:
    explicit DashboardClient(const AuthSession& session)
        : m_headers(L"Accept: application/json\r\nCookie: WorkosCursorSessionToken="
              + Utf8ToWide(session.CookieValue()) + L"\r\n")
    {
        m_session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!m_session)
        {
            m_session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (!m_session)
        {
            m_openError = GetLastError();
            return;
        }
        WinHttpSetTimeouts(m_session, 10000, 10000, 15000, 30000);
        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(m_session, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
        m_connect = WinHttpConnect(m_session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!m_connect)
            m_openError = GetLastError();
    }

    HttpResponse Get(const wchar_t* path) const
    {
        HttpResponse response;
        if (!m_connect)
        {
            response.error = m_openError;
            return response;
        }
        InternetHandle request(WinHttpOpenRequest(m_connect, L"GET", path, nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!request
            || !WinHttpSendRequest(request, m_headers.c_str(), static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            || !WinHttpReceiveResponse(request, nullptr))
        {
            response.error = GetLastError();
            return response;
        }

        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        response.status = status;

        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available))
            {
                response.error = GetLastError();
                break;
            }
            if (available == 0 || response.body.size() + available > kMaxBody)
                break;
            const size_t offset = response.body.size();
            response.body.resize(offset + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, response.body.data() + offset, available, &read))
            {
                response.error = GetLastError();
                response.body.resize(offset);
                break;
            }
            response.body.resize(offset + read);
        }
        return response;
    }

private:
    std::wstring m_headers;
    InternetHandle m_session;
    InternetHandle m_connect;
    DWORD m_openError = 0;
};

std::wstring DescribeNetworkError(DWORD code)
{
    switch (code)
    {
    case ERROR_WINHTTP_TIMEOUT:
        return L"连接 cursor.com 超时";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return L"无法解析 cursor.com，请检查网络或代理";
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return L"无法连接 cursor.com，请检查网络或代理";
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return L"与 cursor.com 的连接被中断";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return L"与 cursor.com 建立安全连接失败";
    default:
        return Format(L"网络错误 %lu", code);
    }
}

// Returns false and fills `error` when the call failed. `network` tells the caller other sessions would fail too.
bool Check(const HttpResponse& response, const wchar_t* path, std::wstring& error, bool& network)
{
    network = false;
    if (response.status == 0)
    {
        network = true;
        error = DescribeNetworkError(response.error);
        return false;
    }
    if (response.status == 401 || response.status == 403)
    {
        error = Format(L"登录态无效或已过期（HTTP %lu），请在 Cursor 里重新登录", response.status);
        return false;
    }
    if (response.status < 200 || response.status >= 300)
    {
        error = Format(L"cursor.com 返回 HTTP %lu（%s）", response.status, path);
        return false;
    }
    return true;
}

const json* Child(const json* obj, const char* key)
{
    if (!obj || !obj->is_object())
        return nullptr;
    const auto it = obj->find(key);
    return it != obj->end() && it->is_object() ? &*it : nullptr;
}

std::optional<double> Number(const json* obj, const char* key)
{
    if (!obj || !obj->is_object())
        return std::nullopt;
    const auto it = obj->find(key);
    if (it == obj->end())
        return std::nullopt;
    if (it->is_number())
        return it->get<double>();
    if (it->is_string())
    {
        const auto& text = it->get_ref<const std::string&>();
        char* end = nullptr;
        const double value = strtod(text.c_str(), &end);
        if (!text.empty() && end == text.c_str() + text.size())
            return value;
    }
    return std::nullopt;
}

bool Flag(const json* obj, const char* key)
{
    if (!obj || !obj->is_object())
        return false;
    const auto it = obj->find(key);
    return it != obj->end() && it->is_boolean() && it->get<bool>();
}

std::string String(const json& obj, const char* key)
{
    const auto it = obj.find(key);
    return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// billingCycleEnd is ISO 8601 in usage-summary; other dashboard endpoints send epoch milliseconds.
std::optional<long long> ParseTimestamp(const json& obj, const char* key)
{
    if (const auto ms = Number(&obj, key); ms && *ms > 1e11)
        return static_cast<long long>(*ms / 1000);

    const auto text = String(obj, key);
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, consumed = 0;
    if (sscanf_s(text.c_str(), "%d-%d-%dT%d:%d:%d%n", &year, &month, &day, &hour, &minute, &second, &consumed) != 6)
        return std::nullopt;

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    long long unix = _mkgmtime(&tm);
    if (unix < 0)
        return std::nullopt;

    size_t pos = static_cast<size_t>(consumed);
    if (pos < text.size() && text[pos] == '.')
    {
        ++pos;
        while (pos < text.size() && isdigit(static_cast<unsigned char>(text[pos])))
            ++pos;
    }
    if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
    {
        int offH = 0, offM = 0;
        if (sscanf_s(text.c_str() + pos + 1, "%d:%d", &offH, &offM) >= 1)
        {
            const long long offset = offH * 3600LL + offM * 60LL;
            unix += text[pos] == '+' ? -offset : offset;
        }
    }
    return unix;
}

UsageInfo ParseSummary(const json& summary)
{
    UsageInfo info;
    const json* individual = Child(&summary, "individualUsage");

    if (const json* plan = Child(individual, "plan"))
    {
        info.hasPlan = true;
        info.planUsedCents = Number(plan, "used").value_or(0);
        const json* breakdown = Child(plan, "breakdown");
        info.planLimitCents = Number(plan, "limit");
        if (!info.planLimitCents)
            info.planLimitCents = Number(breakdown, "total");
        info.planBonusCents = Number(breakdown, "bonus");
        info.autoPercent = Number(plan, "autoPercentUsed");
        info.apiPercent = Number(plan, "apiPercentUsed");
        if (const auto total = Number(plan, "totalPercentUsed"))
            info.totalPercent = *total;
        else if (info.planLimitCents && *info.planLimitCents > 0)
            info.totalPercent = info.planUsedCents / *info.planLimitCents * 100.0;
    }

    const json* personal = Child(individual, "onDemand");
    const json* team = Child(Child(&summary, "teamUsage"), "onDemand");
    const json* onDemand = Flag(personal, "enabled") ? personal : Flag(team, "enabled") ? team : nullptr;
    if (onDemand)
    {
        info.onDemandEnabled = true;
        info.onDemandIsTeam = onDemand == team;
        info.onDemandUsedCents = Number(onDemand, "used").value_or(0);
        info.onDemandLimitCents = Number(onDemand, "limit");
    }

    info.cycleEndUnix = ParseTimestamp(summary, "billingCycleEnd");
    return info;
}
} // namespace

FetchResult FetchUsage()
{
    FetchResult result;
    const auto sessions = DiscoverSessions();
    if (sessions.empty())
    {
        result.error = L"未找到 Cursor 登录态，请先在本机登录 Cursor";
        return result;
    }

    std::wstring lastError;
    bool anyLive = false;
    for (const auto& session : sessions)
    {
        if (session.JwtExpired())
            continue;
        anyLive = true;

        DashboardClient client(session);
        bool network = false;

        const auto me = client.Get(L"/api/auth/me");
        if (!Check(me, L"/api/auth/me", lastError, network))
        {
            if (network)
                break;
            continue;
        }
        const auto meJson = json::parse(me.body, nullptr, false);
        if (!meJson.is_object())
        {
            lastError = L"登录态无效或已过期，请在 Cursor 里重新登录";
            continue;
        }

        const auto summary = client.Get(L"/api/usage-summary");
        if (!Check(summary, L"/api/usage-summary", lastError, network))
        {
            if (network)
                break;
            continue;
        }
        const auto summaryJson = json::parse(summary.body, nullptr, false);
        if (!summaryJson.is_object())
        {
            lastError = L"usage-summary 返回的内容无法解析";
            continue;
        }

        result.info = ParseSummary(summaryJson);
        auto email = String(meJson, "email");
        if (email.empty())
            email = session.email;
        auto membership = session.membership;
        if (membership.empty())
            membership = String(summaryJson, "membershipType");
        result.info.email = Utf8ToWide(email);
        result.info.membership = Utf8ToWide(membership);
        result.ok = true;
        return result;
    }

    result.error = anyLive ? lastError : L"本机 Cursor 登录态已过期，请在 Cursor 里重新登录";
    return result;
}
