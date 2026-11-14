// SectionSeriesDialog.h : Section Series, a modeless dialog that slices the
// scan at a list of offsets from one plane and makes a sketch at each, matched
// for lofting (Create Loft: closed splines with the same number of
// points, or the same lines and arcs, in every slice of a shape), with guide
// curves joining the matching points, then a loft through the sketches.
//
// The plane is picked as in Section (one-shot Pick buttons, through
// ScanDocument::Series::plane). Picking a new plane makes the list again from
// the scan's extent along its normal. The list's offsets can be edited in
// place (double-click or F2), and must go up from row to row: an offset that
// doesn't is refused and the old one stays. While the dialog is open the scan
// view draws every row's slice thin and the chosen row's slice and fit thicker.

#pragma once

#include "StepDialog.h"
#include "ScanDocument.h"

class CSectionSeriesDlg : public CStepDialog
{
public:
    explicit CSectionSeriesDlg(const std::wstring& sessionId);

    // Shows what the view changed: the plane, and the series through it.
    void UpdateFromScan();
    // A plane was picked in the view: the pick button comes up.
    void PlanePicked();

protected:
    BOOL OnInitDialog() override;

    afx_msg void OnDestroy();
    afx_msg void OnPickAlibre();
    afx_msg void OnPickScan();
    afx_msg void OnRangeChanged();
    afx_msg void OnMake();
    afx_msg void OnAdd();
    afx_msg void OnDelete();
    afx_msg void OnLoftChanged();
    afx_msg void OnToleranceChanged();
    afx_msg void OnIntentChanged();
    afx_msg void OnSketchOptionsChanged();
    afx_msg void OnCreate();
    afx_msg void OnListItemChanged(NMHDR* pNotify, LRESULT* pResult);
    afx_msg void OnListDoubleClick(NMHDR* pNotify, LRESULT* pResult);
    afx_msg void OnListKeyDown(NMHDR* pNotify, LRESULT* pResult);
    afx_msg void OnListBeginEdit(NMHDR* pNotify, LRESULT* pResult);
    afx_msg void OnListEndEdit(NMHDR* pNotify, LRESULT* pResult);
    afx_msg LRESULT OnResliceLater(WPARAM, LPARAM);

    DECLARE_MESSAGE_MAP()

private:
    void SetStatus(const CString& text);
    void ToggleClickMode(int mode);
    void ShowClickMode();
    void ClearAlibreSelection();
    // Slices the series again, then redraws and fills the list.
    void Reslice();
    void FillList();
    void ShowOptions();

    CListCtrl m_list;
    CComboBox m_round;
    bool m_filling = false;     // setting the controls, so their change notices are ignored
};

// Called by the scan view (through SectionViewChanged and SectionPlanePicked)
// when the series' plane has changed or was picked.
void SeriesViewChanged();
void SeriesPlanePicked();
