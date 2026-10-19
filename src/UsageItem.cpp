#include "UsageItem.h"

#include <cwchar>

const wchar_t* CUsageItem::GetItemName() const
{
    switch (m_kind)
    {
    case Kind::Plan:
        return L"Cursor 套餐占用";
    case Kind::Spend:
        return L"Cursor 套餐已用";
    case Kind::OnDemand:
        return L"Cursor 按量费用";
    case Kind::Auto:
        return L"Cursor Auto 占用";
    case Kind::Api:
        return L"Cursor API 占用";
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
    switch (m_kind)
    {
    case Kind::Plan:
        return L"套餐: ";
    case Kind::Spend:
        return L"已用: ";
    case Kind::OnDemand:
        return L"按量: ";
    case Kind::Auto:
        return L"Auto: ";
    case Kind::Api:
        return L"API: ";
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
        return L"100%";
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
