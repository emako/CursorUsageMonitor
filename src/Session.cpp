#include "Session.h"

#include "Text.h"

#include <bcrypt.h>
#include <dpapi.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

#include "json.hpp"
#include "sqlite3.h"

using json = nlohmann::json;

namespace
{
constexpr char kSafeStoragePrefix[] = "cursor-safe-storage:v1:";

std::string Base64UrlDecode(const std::string& in)
{
    static const std::string table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned int buffer = 0;
    int bits = 0;
    for (char c : in)
    {
        if (c == '-')
            c = '+';
        else if (c == '_')
            c = '/';
        else if (c == '=')
            break;
        const auto idx = table.find(c);
        if (idx == std::string::npos)
            return {};
        buffer = (buffer << 6) | static_cast<unsigned int>(idx);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

std::string UriEscape(const std::string& text, const char* keep = "")
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || (c != 0 && strchr(keep, c)))
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

std::string LastSegment(const std::string& sub)
{
    const auto pos = sub.rfind('|');
    return pos == std::string::npos ? sub : sub.substr(pos + 1);
}

bool LooksLikeJwt(const std::string& token)
{
    return std::count(token.begin(), token.end(), '.') == 2;
}

std::wstring KnownFolder(REFKNOWNFOLDERID id)
{
    PWSTR raw = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)))
        path = raw;
    CoTaskMemFree(raw);
    return path;
}

// Standard Base64 via Crypt32 (Chromium safeStorage blobs are not base64url).
std::vector<BYTE> Base64Decode(const std::string& text)
{
    DWORD size = 0;
    if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64,
            nullptr, &size, nullptr, nullptr)
        || size == 0)
        return {};
    std::vector<BYTE> out(size);
    if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64,
            out.data(), &size, nullptr, nullptr))
        return {};
    out.resize(size);
    return out;
}

std::vector<BYTE> DpapiUnprotect(const BYTE* data, DWORD size)
{
    DATA_BLOB in{ size, const_cast<BYTE*>(data) };
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        return {};
    std::vector<BYTE> plain(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return plain;
}

// AES key Cursor (Electron/Chromium) keeps in %APPDATA%\Cursor\Local State under os_crypt.encrypted_key.
std::optional<std::vector<BYTE>> LoadOsCryptKey()
{
    const auto roaming = KnownFolder(FOLDERID_RoamingAppData);
    if (roaming.empty())
        return std::nullopt;
    std::ifstream file(roaming + L"\\Cursor\\Local State", std::ios::binary);
    if (!file)
        return std::nullopt;
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto root = json::parse(content, nullptr, false);
    if (!root.is_object())
        return std::nullopt;
    const auto oscrypt = root.find("os_crypt");
    if (oscrypt == root.end() || !oscrypt->is_object())
        return std::nullopt;
    const auto keyIt = oscrypt->find("encrypted_key");
    if (keyIt == oscrypt->end() || !keyIt->is_string())
        return std::nullopt;

    auto blob = Base64Decode(keyIt->get_ref<const std::string&>());
    static constexpr char kDpapiPrefix[] = "DPAPI";
    if (blob.size() <= sizeof(kDpapiPrefix) - 1
        || memcmp(blob.data(), kDpapiPrefix, sizeof(kDpapiPrefix) - 1) != 0)
        return std::nullopt;
    auto key = DpapiUnprotect(blob.data() + (sizeof(kDpapiPrefix) - 1),
        static_cast<DWORD>(blob.size() - (sizeof(kDpapiPrefix) - 1)));
    return key.size() == 32 ? std::optional(std::move(key)) : std::nullopt;
}

const std::vector<BYTE>* OsCryptKey()
{
    static const auto key = LoadOsCryptKey();
    return key ? &*key : nullptr;
}

std::string AesGcmDecrypt(const std::vector<BYTE>& key, const BYTE* nonce, ULONG nonceLen,
    const BYTE* cipher, ULONG cipherLen, const BYTE* tag, ULONG tagLen)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
        return {};
    if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
            sizeof(BCRYPT_CHAIN_MODE_GCM), 0)
        != 0)
    {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    if (BCryptGenerateSymmetricKey(alg, &keyHandle, nullptr, 0, const_cast<PUCHAR>(key.data()),
            static_cast<ULONG>(key.size()), 0)
        != 0)
    {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce);
    info.cbNonce = nonceLen;
    info.pbTag = const_cast<PUCHAR>(tag);
    info.cbTag = tagLen;

    std::string plain(cipherLen, '\0');
    ULONG plainLen = 0;
    const NTSTATUS status = BCryptDecrypt(keyHandle, const_cast<PUCHAR>(cipher), cipherLen, &info, nullptr, 0,
        reinterpret_cast<PUCHAR>(plain.data()), static_cast<ULONG>(plain.size()), &plainLen, 0);

    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (status != 0)
        return {};
    plain.resize(plainLen);
    return plain;
}

// Cursor 3.27+ may seal tokens as cursor-safe-storage:v1: + base64(v10||nonce||cipher||tag).
// Older builds still store a bare JWT. Returns empty when sealed and decrypt fails.
std::string UnwrapAccessToken(const std::string& stored)
{
    const auto text = Trim(stored);
    if (text.size() < sizeof(kSafeStoragePrefix) - 1
        || text.compare(0, sizeof(kSafeStoragePrefix) - 1, kSafeStoragePrefix) != 0)
        return text;

    const auto* key = OsCryptKey();
    if (!key)
        return {};
    auto blob = Base64Decode(text.substr(sizeof(kSafeStoragePrefix) - 1));
    // Chromium OSCrypt v10 on Windows: "v10" + 12-byte nonce + ciphertext + 16-byte tag.
    constexpr size_t kPrefix = 3, kNonce = 12, kTag = 16;
    if (blob.size() < kPrefix + kNonce + kTag || memcmp(blob.data(), "v10", kPrefix) != 0)
        return {};
    const BYTE* nonce = blob.data() + kPrefix;
    const ULONG cipherLen = static_cast<ULONG>(blob.size() - kPrefix - kNonce - kTag);
    const BYTE* cipher = blob.data() + kPrefix + kNonce;
    const BYTE* tag = blob.data() + blob.size() - kTag;
    return AesGcmDecrypt(*key, nonce, kNonce, cipher, cipherLen, tag, kTag);
}

std::vector<std::wstring> ExistingUnique(const std::vector<std::wstring>& paths)
{
    std::vector<std::wstring> out;
    for (const auto& path : paths)
    {
        const DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        const bool seen = std::any_of(out.begin(), out.end(),
            [&](const std::wstring& p) { return _wcsicmp(p.c_str(), path.c_str()) == 0; });
        if (!seen)
            out.push_back(path);
    }
    return out;
}

std::vector<std::wstring> StateVscdbCandidates()
{
    const auto roaming = KnownFolder(FOLDERID_RoamingAppData);
    if (roaming.empty())
        return {};
    const auto db = roaming + L"\\Cursor\\User\\globalStorage\\state.vscdb";
    return ExistingUnique({ db, db + L".backup" });
}

std::vector<std::wstring> AuthJsonCandidates()
{
    const auto profile = KnownFolder(FOLDERID_Profile);
    const auto roaming = KnownFolder(FOLDERID_RoamingAppData);
    const auto local = KnownFolder(FOLDERID_LocalAppData);
    std::vector<std::wstring> paths;
    if (!profile.empty())
    {
        paths.push_back(profile + L"\\.config\\cursor\\auth.json");
        paths.push_back(profile + L"\\.cursor\\auth.json");
    }
    if (!roaming.empty())
        paths.push_back(roaming + L"\\cursor\\auth.json");
    if (!local.empty())
        paths.push_back(local + L"\\cursor\\auth.json");
    return ExistingUnique(paths);
}

std::string DecodeDbValue(const std::string& raw)
{
    const auto text = Trim(raw);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
    {
        const auto parsed = json::parse(text, nullptr, false);
        if (parsed.is_string())
            return parsed.get<std::string>();
    }
    return text;
}

class Vscdb
{
public:
    explicit Vscdb(const std::wstring& path)
    {
        const auto utf8 = WideToUtf8(path);
        std::string uriPath = utf8;
        std::replace(uriPath.begin(), uriPath.end(), '\\', '/');
        const auto uri = "file:///" + UriEscape(uriPath, "/:");
        // immutable=1 keeps a running Cursor's lock from blocking the read.
        if (TryOpen(uri + "?mode=ro&immutable=1", SQLITE_OPEN_READONLY | SQLITE_OPEN_URI))
            return;
        if (TryOpen(uri + "?mode=ro", SQLITE_OPEN_READONLY | SQLITE_OPEN_URI))
            return;
        TryOpen(utf8, SQLITE_OPEN_READONLY);
    }

    ~Vscdb()
    {
        if (m_db)
            sqlite3_close(m_db);
    }

    Vscdb(const Vscdb&) = delete;
    Vscdb& operator=(const Vscdb&) = delete;

    explicit operator bool() const { return m_db != nullptr; }

    std::string Get(const char* key) const
    {
        if (!m_db)
            return {};
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, "SELECT value FROM ItemTable WHERE key=?1", -1, &stmt, nullptr) != SQLITE_OK)
            return {};
        std::string value;
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        {
            const auto* data = static_cast<const char*>(sqlite3_column_blob(stmt, 0));
            const int size = sqlite3_column_bytes(stmt, 0);
            if (data && size > 0)
                value = DecodeDbValue(std::string(data, size));
        }
        sqlite3_finalize(stmt);
        return value;
    }

private:
    bool TryOpen(const std::string& name, int flags)
    {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(name.c_str(), &db, flags, nullptr) == SQLITE_OK)
        {
            sqlite3_busy_timeout(db, 1000);
            // sqlite opens lazily; touch the table so a broken strategy falls through to the next one.
            sqlite3_stmt* probe = nullptr;
            bool ok = false;
            if (sqlite3_prepare_v2(db, "SELECT 1 FROM ItemTable LIMIT 1", -1, &probe, nullptr) == SQLITE_OK)
            {
                const int rc = sqlite3_step(probe);
                ok = rc == SQLITE_ROW || rc == SQLITE_DONE;
            }
            sqlite3_finalize(probe);
            if (ok)
            {
                m_db = db;
                return true;
            }
        }
        sqlite3_close(db);
        return false;
    }

    sqlite3* m_db = nullptr;
};

std::string GetEnvUtf8(const wchar_t* name)
{
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0)
        return {};
    std::wstring buf(size, L'\0');
    const DWORD len = GetEnvironmentVariableW(name, buf.data(), size);
    buf.resize(len);
    return WideToUtf8(buf);
}

std::optional<AuthSession> FromEnv()
{
    auto raw = GetEnvUtf8(L"CURSOR_SESSION_TOKEN");
    if (Trim(raw).empty())
        raw = GetEnvUtf8(L"WORKOS_CURSOR_SESSION_TOKEN");
    raw = Trim(Trim(raw), "'\"");
    if (raw.empty())
        return std::nullopt;

    std::string token = raw;
    std::string sub;
    if (const auto pos = FindNoCase(raw, "%3A%3A"); pos != std::string::npos)
    {
        sub = raw.substr(0, pos);
        token = raw.substr(pos + 6);
    }
    else if (const auto sep = raw.find("::"); sep != std::string::npos)
    {
        sub = raw.substr(0, sep);
        token = raw.substr(sep + 2);
    }
    if (!LooksLikeJwt(token))
        return std::nullopt;

    AuthSession session;
    session.accessToken = token;
    session.source = "env:CURSOR_SESSION_TOKEN";
    session.sub = sub.empty() ? JwtClaim(token, "sub") : sub;
    return session;
}

std::optional<AuthSession> FromStateVscdb(const std::wstring& path)
{
    Vscdb db(path);
    if (!db)
        return std::nullopt;
    const auto token = UnwrapAccessToken(db.Get("cursorAuth/accessToken"));
    if (token.empty() || !LooksLikeJwt(token))
        return std::nullopt;

    AuthSession session;
    session.accessToken = token;
    session.source = "state.vscdb:" + WideToUtf8(path);
    session.email = db.Get("cursorAuth/cachedEmail");
    session.membership = db.Get("cursorAuth/stripeMembershipType");
    session.sub = JwtClaim(token, "sub");
    return session;
}

std::string FirstString(const json& root, std::initializer_list<const char*> keys)
{
    for (const char* key : keys)
    {
        const auto it = root.find(key);
        if (it != root.end() && it->is_string())
            return it->get<std::string>();
    }
    return {};
}

std::optional<AuthSession> FromAuthJson(const std::wstring& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::nullopt;
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto root = json::parse(content, nullptr, false);
    if (!root.is_object())
        return std::nullopt;
    const auto token = UnwrapAccessToken(FirstString(root, { "accessToken", "access_token", "token" }));
    if (token.empty() || !LooksLikeJwt(token))
        return std::nullopt;

    AuthSession session;
    session.accessToken = token;
    session.source = "auth.json:" + WideToUtf8(path);
    session.email = FirstString(root, { "email" });
    session.membership = FirstString(root, { "membershipType", "membership" });
    session.sub = JwtClaim(token, "sub");
    return session;
}

std::string CurrentSignedInAuthId()
{
    for (const auto& path : StateVscdbCandidates())
    {
        if (path.size() >= 7 && _wcsicmp(path.c_str() + path.size() - 7, L".backup") == 0)
            continue;
        Vscdb db(path);
        if (!db)
            continue;
        auto value = db.Get("glass.lastSignedInAuthId");
        if (!value.empty())
            return value;
    }
    return {};
}

bool SubMatches(const AuthSession& session, const std::string& authId)
{
    if (authId.empty())
        return false;
    const auto sub = session.sub.empty() ? JwtClaim(session.accessToken, "sub") : session.sub;
    if (sub.empty())
        return false;
    return sub == authId || LastSegment(sub) == LastSegment(authId);
}

auto Rank(const AuthSession& s, const std::string& current)
{
    return std::make_tuple(
        s.JwtExpired() ? 1 : 0,
        SubMatches(s, current) ? 0 : 1,
        ContainsNoCase(s.source, ".backup") ? 1 : 0,
        s.source.find("state.vscdb") != std::string::npos ? 0 : 1,
        _stricmp(s.membership.c_str(), "ultra") == 0 ? 0 : 1,
        s.source);
}
} // namespace

std::string JwtClaim(const std::string& token, const std::string& key)
{
    const auto first = token.find('.');
    if (first == std::string::npos)
        return {};
    const auto second = token.find('.', first + 1);
    const auto payload = token.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1);
    const auto claims = json::parse(Base64UrlDecode(payload), nullptr, false);
    if (!claims.is_object())
        return {};
    const auto it = claims.find(key);
    if (it == claims.end() || it->is_null())
        return {};
    return it->is_string() ? it->get<std::string>() : it->dump();
}

std::string AuthSession::CookieValue() const
{
    const auto subject = sub.empty() ? JwtClaim(accessToken, "sub") : sub;
    if (subject.empty())
        return accessToken;
    return UriEscape(LastSegment(subject)) + "%3A%3A" + accessToken;
}

bool AuthSession::JwtExpired(int skewSec) const
{
    const auto raw = JwtClaim(accessToken, "exp");
    if (raw.empty())
        return false;
    char* end = nullptr;
    const double exp = strtod(raw.c_str(), &end);
    if (end == raw.c_str())
        return false;
    return exp < static_cast<double>(std::time(nullptr) - skewSec);
}

std::vector<AuthSession> DiscoverSessions()
{
    std::vector<AuthSession> found;
    std::set<std::string> seen;
    auto add = [&](std::optional<AuthSession> session) {
        if (!session || session->accessToken.empty())
            return;
        if (!seen.insert(session->accessToken).second)
            return;
        found.push_back(std::move(*session));
    };

    add(FromEnv());
    for (const auto& path : StateVscdbCandidates())
        add(FromStateVscdb(path));
    for (const auto& path : AuthJsonCandidates())
        add(FromAuthJson(path));

    const auto current = CurrentSignedInAuthId();
    std::stable_sort(found.begin(), found.end(),
        [&](const AuthSession& a, const AuthSession& b) { return Rank(a, current) < Rank(b, current); });
    return found;
}
