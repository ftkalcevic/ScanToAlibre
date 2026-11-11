// RefineDialog.h : Refine, the second step of the workflow: a modeless dialog
// that decimates (fewer triangles, by quadric edge collapse) and smooths
// (Laplacian, Taubin or Bilateral) the part's scan. Each button changes the
// scan straight away; Undo takes back up to kUndoSteps changes.

#pragma once

#include "StepDialog.h"

class CRefineDlg : public CStepDialog
{
public:
    explicit CRefineDlg(const std::wstring& sessionId);

protected:
    BOOL OnInitDialog() override;

    afx_msg void OnDecimate();
    afx_msg void OnSmooth();
    afx_msg void OnUndo();
    afx_msg void OnDecimateKind();
    afx_msg void OnLimitChanged();

    DECLARE_MESSAGE_MAP()

private:
    void ShowScanInfo();
    void SetStatus(const CString& text);
    void ScanChanged();
    bool ReadNumber(int id, double& value);

    CComboBox m_method;
};
