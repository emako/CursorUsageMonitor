#pragma once

#include <windows.h>

// UI strings for the languages TrafficBar ships. The host passes
// LocaleNameToLCID of the active BCP-47 tag through ITrafficMonitor::GetLanguageId.
// 0 follows the system UI language. Percent and spacing checkbox wording matches
// TrafficBar TXT_HIDE_PERCENTAGE_CHECK, TXT_SEPARATE_VALUE_UNIT_CHECK, TXT_OK, and TXT_CANCEL.
namespace Lang {

enum class Id
{
    DeDe,
    EnUs,
    Es419,
    EsEs,
    FrFr,
    HeIl,
    HuHu,
    ItIt,
    JaJp,
    KoKr,
    PlPl,
    PtBr,
    RuRu,
    SlSi,
    ThTh,
    TrTr,
    ViVn,
    ZhCn,
    ZhTw,
};

inline Id FromLanguageId(unsigned short langId)
{
    if (langId == 0)
        langId = GetUserDefaultUILanguage();

    switch (langId)
    {
    case 0x0407: return Id::DeDe;
    case 0x0409: return Id::EnUs;
    case 0x580A: return Id::Es419;
    case 0x040A: // Spanish (Spain, traditional sort)
    case 0x0C0A: return Id::EsEs;
    case 0x040C: return Id::FrFr;
    case 0x040D: return Id::HeIl;
    case 0x040E: return Id::HuHu;
    case 0x0410: return Id::ItIt;
    case 0x0411: return Id::JaJp;
    case 0x0412: return Id::KoKr;
    case 0x0415: return Id::PlPl;
    case 0x0416: return Id::PtBr;
    case 0x0419: return Id::RuRu;
    case 0x0424: return Id::SlSi;
    case 0x041E: return Id::ThTh;
    case 0x041F: return Id::TrTr;
    case 0x042A: return Id::ViVn;
    case 0x0804: return Id::ZhCn;
    case 0x0404: return Id::ZhTw;
    default: break;
    }

    const auto primary = PRIMARYLANGID(langId);
    const auto sub = SUBLANGID(langId);
    if (primary == LANG_CHINESE)
    {
        // Taiwan, Hong Kong, and Macau follow zh-TW. Simplified and Singapore follow zh-CN.
        if (sub == SUBLANG_CHINESE_TRADITIONAL || sub == SUBLANG_CHINESE_HONGKONG || sub == SUBLANG_CHINESE_MACAU)
            return Id::ZhTw;
        return Id::ZhCn;
    }
    if (primary == LANG_SPANISH)
    {
        // Spain stays es-ES. Other Spanish regions follow es-419.
        if (sub == SUBLANG_SPANISH || sub == SUBLANG_SPANISH_MODERN)
            return Id::EsEs;
        return Id::Es419;
    }

    switch (primary)
    {
    case LANG_GERMAN: return Id::DeDe;
    case LANG_ENGLISH: return Id::EnUs;
    case LANG_FRENCH: return Id::FrFr;
    case LANG_HEBREW: return Id::HeIl;
    case LANG_HUNGARIAN: return Id::HuHu;
    case LANG_ITALIAN: return Id::ItIt;
    case LANG_JAPANESE: return Id::JaJp;
    case LANG_KOREAN: return Id::KoKr;
    case LANG_POLISH: return Id::PlPl;
    case LANG_PORTUGUESE: return Id::PtBr;
    case LANG_RUSSIAN: return Id::RuRu;
    case LANG_SLOVENIAN: return Id::SlSi;
    case LANG_THAI: return Id::ThTh;
    case LANG_TURKISH: return Id::TrTr;
    case LANG_VIETNAMESE: return Id::ViVn;
    default: return Id::EnUs;
    }
}

// Format placeholders, same in every language:
// intervalHint: %d %d %d (min, max, default)
// invalidInterval: %d %d (min, max)
// dayLeft / daysLeft: %lld
// updated: %02u %02u %02u (hour, minute, second)
// errNetwork / errAuthHttp: %lu
// errHttp: %lu %s (status, path)
struct Text
{
    const wchar_t* name;
    const wchar_t* description;
    const wchar_t* optionsTitle;
    const wchar_t* intervalLabel;
    const wchar_t* intervalHint;
    const wchar_t* invalidInterval;
    const wchar_t* showDecimal;
    const wchar_t* hidePercent;
    const wchar_t* separateSpace;
    const wchar_t* ok;
    const wchar_t* cancel;
    const wchar_t* font;
    const wchar_t* itemPlan;
    const wchar_t* itemSpend;
    const wchar_t* itemOnDemand;
    const wchar_t* itemAuto;
    const wchar_t* itemApi;
    const wchar_t* labelPlan;
    const wchar_t* labelSpend;
    const wchar_t* labelOnDemand;
    const wchar_t* labelAuto;
    const wchar_t* labelApi;
    const wchar_t* refreshNow;
    const wchar_t* account;
    const wchar_t* membershipOpen;
    const wchar_t* membershipClose;
    const wchar_t* planUsage;
    const wchar_t* detailOpen;
    const wchar_t* detailMiddle;
    const wchar_t* detailClose;
    const wchar_t* planUsed;
    const wchar_t* bonusUsed;
    const wchar_t* onDemand;
    const wchar_t* team;
    const wchar_t* onDemandOff;
    const wchar_t* cycleEnd;
    const wchar_t* dayLeft;
    const wchar_t* daysLeft;
    const wchar_t* updated;
    const wchar_t* refreshing;
    const wchar_t* refreshFailed;
    const wchar_t* errThread;
    const wchar_t* errCollectPrefix;
    const wchar_t* errCollect;
    const wchar_t* errTimeout;
    const wchar_t* errDns;
    const wchar_t* errConnect;
    const wchar_t* errInterrupted;
    const wchar_t* errTls;
    const wchar_t* errNetwork;
    const wchar_t* errAuthHttp;
    const wchar_t* errHttp;
    const wchar_t* errNoSession;
    const wchar_t* errAuthInvalid;
    const wchar_t* errParse;
    const wchar_t* errExpired;
    bool rtl;
};

const Text& Get(Id id);

using LanguageProvider = unsigned short (*)();
void SetLanguageProvider(LanguageProvider provider);
const Text& Current();

} // namespace Lang
