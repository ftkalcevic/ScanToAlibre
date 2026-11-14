// CompareDialog.h : 7 Compare, a modeless dialog that colours the scan by its
// signed distance from the part as built so far in Alibre.
//
// The part's facets are read when the dialog opens (there's no Update button,
// Frank's choice): close and open it again after changing the part. While it's
// open the scan view draws the scan in colour bands (blues inside the part,
// green within the tolerance, yellow to red outside, grey beyond the range).
// Probe down: a click on the scan shows the distance there.

#pragma once

#include "StepDialog.h"

class CCompareDlg : public CStepDialog
{
public:
    explicit CCompareDlg(const std::wstring& sessionId);

    // Shows the statistics and the probe's reading.
    void UpdateFromScan();

protected:
    BOOL OnInitDialog() override;

    afx_msg void OnDestroy();
    afx_msg void OnToleranceChanged();
    afx_msg void OnRangeChanged();
    afx_msg void OnProbe();
    afx_msg void OnDrawItem(int id, LPDRAWITEMSTRUCT pDraw);

    DECLARE_MESSAGE_MAP()

private:
    void SetStatus(const CString& text);
    void ShowClickMode();
    // Reads the part's facets into the scan's Compare, and searches.
    void ReadPart();
    // Redraws the view and the legend, and shows the statistics.
    void Recoloured();

    bool m_filling = false;     // setting the controls, so their change notices are ignored
};

// Called by the scan view when the probe has read a distance.
void CompareViewChanged();
