// SectionDialog.h : 4 Section, a modeless dialog that slices the scan with a
// plane, fits lines and arcs to the slice, and makes an Alibre sketch of them
// with the design intent as constraints and the slice as construction lines.
//
// While it's open the scan view draws the plane, the slice (red) and the fit
// (green). Scan Plane down: a click on the scan chooses one of its box or
// fitted planes. Alibre Plane down: Alibre's design plane or flat face clicked
// in the view is the plane. Each pick is one-shot: choosing a plane lets the
// button up, so the next press picks again (Frank: pick, Create Sketch, pick
// another, Create Sketch, without closing the dialog).
//
// 6 Silhouette is the same dialog on its own template (IDD_SILHOUETTE, with a
// Smallest hole field) and its own ScanDocument::Section: the scan's outline
// along the plane's normal instead of a slice.

#pragma once

#include "StepDialog.h"
#include "ScanDocument.h"

class CSectionDlg : public CStepDialog
{
public:
    // silhouette: 6 Silhouette rather than 4 Section.
    CSectionDlg(const std::wstring& sessionId, bool silhouette);

    // Shows what the view changed: the plane, and the section through it.
    void UpdateFromScan();
    // A plane was picked in the view: the pick button comes up.
    void PlanePicked();

protected:
    BOOL OnInitDialog() override;

    afx_msg void OnDestroy();
    afx_msg void OnPickAlibre();
    afx_msg void OnPickScan();
    afx_msg void OnOffsetChanged();
    afx_msg void OnToleranceChanged();
    afx_msg void OnSmallestHoleChanged();
    afx_msg void OnIntentChanged();
    afx_msg void OnSketchOptionsChanged();
    afx_msg void OnCreate();

    DECLARE_MESSAGE_MAP()

private:
    // This dialog's section: the slice's or the silhouette's.
    ScanDocument::Section& Sec(ScanDocument& scan) const { return m_silhouette ? scan.silhouette : scan.section; }
    void SetStatus(const CString& text);
    void ToggleClickMode(int mode);
    void ShowClickMode();
    // Empties Alibre's selection in this part.
    void ClearAlibreSelection();
    // Slices and fits again, then redraws.
    void Reslice();

    CComboBox m_round;
    bool m_filling = false;     // setting the controls, so their change notices are ignored
    bool m_silhouette = false;
};

// Called by the scan view when the section's plane has changed.
void SectionViewChanged();
// Called by the scan view when a click chose the plane (picking has ended).
void SectionPlanePicked();
