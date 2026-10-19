#include "UsageItem.h"

#include "DataManager.h"
#include "Lang.h"

#include <cwchar>

const wchar_t* CUsageItem::GetItemName() const
{
    const auto& text = Lang::Current();
    switch (m_kind)
    {
    case Kind::Plan:
        return text.itemPlan;
    case Kind::Spend:
        return text.itemSpend;
    case Kind::OnDemand:
        return text.itemOnDemand;
    case Kind::Auto:
        return text.itemAuto;
    case Kind::Api:
        return text.itemApi;
    }
    return L"";
}

const wchar_t* CUsageItem::GetItemId() const
{
    switch (m_kind)
    {
    case Kind::Plan:
        return L"CursorPlan";
    case Kind::Spend:
        return L"CursorSpend";
    case Kind::OnDemand:
        return L"CursorOnDemand";
    case Kind::Auto:
        return L"CursorAuto";
    case Kind::Api:
        return L"CursorApi";
    }
    return L"";
}

const wchar_t* CUsageItem::GetItemLableText() const
{
    const auto& text = Lang::Current();
    switch (m_kind)
    {
    case Kind::Plan:
        return text.labelPlan;
    case Kind::Spend:
        return text.labelSpend;
    case Kind::OnDemand:
        return text.labelOnDemand;
    case Kind::Auto:
        return text.labelAuto;
    case Kind::Api:
        return text.labelApi;
    }
    return L"";
}

const wchar_t* CUsageItem::GetItemValueText() const
{
    return m_value;
}

const wchar_t* CUsageItem::GetItemValueSampleText() const
{
    switch (m_kind)
    {
    case Kind::Plan:
    case Kind::Auto:
    case Kind::Api:
    {
        const auto text = FormatPercent(100.0, DataManager::Instance().Display());
        wcsncpy_s(m_sample, text.c_str(), _TRUNCATE);
        return m_sample;
    }
    case Kind::Spend:
        return L"$999.99/$999";
    case Kind::OnDemand:
        return L"$999.99";
    }
    return L"";
}

int CUsageItem::IsDrawResourceUsageGraph() const
{
    return m_kind == Kind::Plan || m_kind == Kind::Auto || m_kind == Kind::Api ? 1 : 0;
}

float CUsageItem::GetResourceUsageGraphValue() const
{
    return m_graphValue.load();
}

void CUsageItem::SetValue(const wchar_t* text, float graphValue)
{
    m_graphValue.store(graphValue);
    if (wcscmp(m_value, text) != 0)
        wcsncpy_s(m_value, text, _TRUNCATE);
}
