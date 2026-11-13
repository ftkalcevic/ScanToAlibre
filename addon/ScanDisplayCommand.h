// ScanDisplayCommand.h : Show Scan, a two-way toggle command that draws the
// part's scan for as long as it's on.
//
// Alibre ends a regular command (the scan view) when any native command starts,
// such as Measure, and its graphics go with it. A two-way toggle stays on
// through native commands (proven by CAMForAlibre's tab command), so this one
// asks for On3DRender callbacks (EnableOn3DRenderCallback) and draws the scan
// itself (proven: Alibre calls it, and the scan stays visible through Measure).
// Without a Design Explorer tab, Alibre's hover code (GeometryListener)
// threw "Object reference not set" on mouse moves, so it adds a small Scan
// tab, as every toggle in Alibre's samples and CAMForAlibre does (testing that
// that cures it). While it's on, the scan view draws only Orient's geometry
// and picked points, not the scan a second time.

#pragma once

#include "CommandBase.h"

#include <string>

class CScanDisplayCommand : public CCommandBase
{
public:
    explicit CScanDisplayCommand(const std::wstring& sessionId);

    STDMETHOD(putref_CommandSite)(struct IADAddOnCommandSite* pSite) override;
    STDMETHOD(IsTwoWayToggle)(VARIANT_BOOL* pValue) override;
    STDMETHOD(AddTab)(VARIANT_BOOL* pValue) override;
    STDMETHOD(get_TabName)(BSTR* pName) override;
    STDMETHOD(OnShowUI)(__int64 hWnd) override;
    // Alibre calls this once a toggle is set up; the first redraw is asked for
    // here, not while the command site is being set (the hover error came once,
    // which may be a redraw before Alibre had finished starting the toggle).
    STDMETHOD(OnComplete)() override;
    STDMETHOD(On3DRender)() override;
    STDMETHOD(OnTerminate)() override;

    const std::wstring& SessionId() const { return m_sessionId; }

private:
    std::wstring m_sessionId;
    int m_renders = 0;
    HWND m_label = nullptr;     // the tab's text
};

// The part's Show Scan command while it's on, or nullptr.
CScanDisplayCommand* FindScanDisplay(const std::wstring& sessionId);

// Show Scan for this part: a new one to turn on, or the running one, which
// Alibre turns off when it's returned again. Holds a reference until it ends.
CScanDisplayCommand* ToggleScanDisplay(const std::wstring& sessionId);

// Whether Show Scan is drawing this part's scan.
bool ScanDisplayOn(const std::wstring& sessionId);

// Redraws every Show Scan (after the scan changes).
void RefreshScanDisplays();

// Drops our references at unload.
void ReleaseScanDisplays();
