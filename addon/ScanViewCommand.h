// ScanViewCommand.h : the regular command that draws a part's scan and picks
// points on it with the left button.
//
// Alibre only draws add-on graphics while a regular command is active, and any
// native command (or Escape) ends it; Show Scan starts it again.
//
// Picking: Alibre doesn't pick add-on graphics, so a click is turned into a line
// through the scene. The view's projection is fitted from WorldToScreen on the
// scan's box corners (ScreenToWorld is undocumented; what it returns is logged),
// and the line is met with the whole scan, nearest the camera.

#pragma once

#include "CommandBase.h"

#include <string>

#include "ViewProjection.h"

struct ScanDocument;

// Redraws every running scan view (after the scan changes).
void RefreshScanViews();

// Asks Alibre to refresh its buttons through a running scan view's command site
// (UpdateToolbars), so Save lights once the scan has changed: Alibre asks
// HasPersistentDataToSave only when it refreshes them, which a change made in
// a ScanToAlibre dialog doesn't otherwise cause.
void RefreshAlibreButtons();

class CScanViewCommand : public CCommandBase
{
public:
    explicit CScanViewCommand(const std::wstring& sessionId);
    ~CScanViewCommand() override;

    // Asks Alibre to redraw, if this view is still running.
    void Refresh();

    // IAlibreAddOnCommand
    STDMETHOD(On3DRender)() override;
    // A left click picks. Alibre calls OnClick for a regular command, but not
    // OnMouseDown or OnMouseUp (seen in Frank's log).
    STDMETHOD(OnClick)(long screenX, long screenY, enum ADDONMouseButtons buttons,
                       VARIANT_BOOL* pHandled) override;
    STDMETHOD(OnKeyDown)(long keycode, VARIANT_BOOL* pHandled) override;
    // While orienting, Alibre's own selection (its planes, axes and points) is the target.
    STDMETHOD(OnSelectionChange)() override;
    STDMETHOD(OnTerminate)() override;

private:
    // Returns true if the click hit the scan.
    bool Pick(ScanDocument& scan, long x, long y);
    // Orient: choose the box's or a fitted plane, axis or centre; or grow a flat area.
    // Section: choose the box's or a fitted plane.
    bool PickFeature(ScanDocument& scan, long x, long y);
    bool GrowAt(ScanDocument& scan, long x, long y);
    bool FitView(const ScanDocument& scan, scancore::ViewProjection& view);
    // planesOnly: just the box and the planes, for choosing a section's plane.
    void DrawOrient(IADAddOnCanvasDisplay* pCanvas, ScanDocument& scan, bool planesOnly);
    // Section: the plane's outline, the slice and its fitted lines and arcs.
    void DrawSection(IADAddOnCanvasDisplay* pCanvas, ScanDocument& scan);
    // Section: Alibre's selected design plane or flat face becomes the plane.
    void ReadSectionPlane(ScanDocument& scan);
    void ProbeScreenToWorld(long x, long y);

    std::wstring m_sessionId;
    bool m_reportedRender = false;
    bool m_probedScreenToWorld = false;
    bool m_reportedCamera = false;
    int m_renders = 0;
};
