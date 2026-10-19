#pragma once

#include <string>
#include <vector>

struct AuthSession
{
    std::string accessToken;
    std::string source;
    std::string email;
    std::string membership;
    std::string sub;

    std::string CookieValue() const;
    bool JwtExpired(int skewSec = 60) const;
};

std::string JwtClaim(const std::string& token, const std::string& key);

// Sessions found on this machine for the current Windows user, best candidate first.
std::vector<AuthSession> DiscoverSessions();
