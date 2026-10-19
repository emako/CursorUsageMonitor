#pragma once

#include "PluginInterface.h"
#include "UsageItem.h"

#include <cstdint>

class CCursorUsagePlugin : public ITMPlugin
{
public:
    static CCursorUsagePlugin& Instance();

    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    OptionReturn ShowOptionsDialog(void* hParent) override;
    const wchar_t* GetTooltipInfo() override;
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    int GetCommandCount() override;
    const wchar_t* GetCommandName(int command_index) override;
    void OnPluginCommand(int command_index, void* hWnd, void* para) override;
    void OnInitialize(ITrafficMonitor* pApp) override;
    unsigned short LanguageId() const;

private:
    CCursorUsagePlugin() = default;

    CUsageItem m_plan{ CUsageItem::Kind::Plan };
    CUsageItem m_spend{ CUsageItem::Kind::Spend };
    CUsageItem m_onDemand{ CUsageItem::Kind::OnDemand };
    CUsageItem m_auto{ CUsageItem::Kind::Auto };
    CUsageItem m_api{ CUsageItem::Kind::Api };
    ITrafficMonitor* m_app = nullptr;
    uint64_t m_shownVersion = 0;
    wchar_t m_tooltip[2048] = L"";
};

#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllexport) ITMPlugin* TMPluginGetInstance();
#ifdef __cplusplus
}
#endif
