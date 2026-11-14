// SectionDialog.h : 4 Section, a modeless dialog that slices the scan with a
// plane, fits lines and arcs to the slice, and makes an Alibre sketch of them
// with the design intent as constraints and the slice as construction lines.
//
// While it's open the scan view draws the plane, the slice (red) and the fit
// (green). Scan Plane down: a click on the scan chooses one of its box or
// fitted planes. Alibre Plane down: Alibre's design plane or flat face clicked
// in the view is the plane.

#pragma once

#include "StepDialog.h"

class CSectionDlg : public CStepDialog
{
public:
    explicit CSectionDlg(const std::wstring& sessionId);

    // Shows what the view changed: the plane, and the section through it.
    void UpdateFromScan();

protected:
    BOOL OnInitDialog() override;

    afx_msg void OnDestroy();
    afx_msg void OnPickAlibre();
    afx_msg void OnPickScan();
    afx_msg void OnOffsetChanged();
    afx_msg void OnToleranceChanged();
    afx_msg void OnIntentChanged();
    afx_msg void OnCreate();

    DECLARE_MESSAGE_MAP()

private:
    void SetStatus(const CString& text);
    void ToggleClickMode(int mode);
    void ShowClickMode();
    // Slices and fits again, then redraws.
    void Reslice();

    CComboBox m_round;
    bool m_filling = false;     // setting the controls, so their change notices are ignored
};

// Called by the scan view when the section's plane has changed.
void SectionViewChanged();
