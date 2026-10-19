#pragma once

#include "PluginInterface.h"

#include <atomic>

class CUsageItem : public IPluginItem
{
public:
    enum class Kind
    {
        Plan,
        Spend,
        OnDemand,
        Auto,
        Api,
    };

    explicit CUsageItem(Kind kind) : m_kind(kind) {}

    const wchar_t* GetItemName() const override;
    const wchar_t* GetItemId() const override;
    const wchar_t* GetItemLableText() const override;
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override;
    int IsDrawResourceUsageGraph() const override;
    float GetResourceUsageGraphValue() const override;

    void SetValue(const wchar_t* text, float graphValue = 0.0f);

private:
    Kind m_kind;
    // Fixed buffer: TrafficMonitor reads the returned pointer on its UI thread while DataRequired updates it.
    wchar_t m_value[64] = L"--";
    mutable wchar_t m_sample[32] = L"100%";
    std::atomic<float> m_graphValue{ 0.0f };
};
