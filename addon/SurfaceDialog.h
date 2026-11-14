// SurfaceDialog.h : 5 Surface, a modeless dialog that fits a B-spline surface
// to selected areas of the scan and inserts it into the part as an Alibre
// design surface (an IGES file through DesignSurfaces->InsertFromFile).
//
// While it's open the scan view draws the selection (orange), the fitted
// surface (a green grid) and the outline it's trimmed to (yellow). Smooth Area
// down: a click selects the smooth area round it; Brush down: the triangles
// within the radius. Clicking a selected triangle takes that area away again.

#pragma once

#include "StepDialog.h"

class CSurfaceDlg : public CStepDialog
{
public:
    explicit CSurfaceDlg(const std::wstring& sessionId);

    // Shows what the view changed: the selection and the fit.
    void UpdateFromScan();

protected:
    BOOL OnInitDialog() override;

    afx_msg void OnDestroy();
    afx_msg void OnPickSmooth();
    afx_msg void OnPickBrush();
    afx_msg void OnClear();
    afx_msg void OnSelectChanged();
    afx_msg void OnOptionsChanged();
    afx_msg void OnCreate();

    DECLARE_MESSAGE_MAP()

private:
    void SetStatus(const CString& text);
    void ToggleClickMode(int mode);
    void ShowClickMode();
    // Greys the symmetry box of the base plane (a plane can't mirror across itself).
    void ShowSymmetryChoices();
    // Fits again, then redraws.
    void Refit();

    bool m_filling = false;     // setting the controls, so their change notices are ignored
};

// Called by the scan view when the selection has changed.
void SurfaceViewChanged();
