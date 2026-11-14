// OrientDialog.h : Orient, the third step of the workflow: a modeless dialog to
// move, turn and scale the scan, fit planes to flat areas, and line the scan up
// with Alibre's planes, axes and points in 3-2-1 steps.
//
// While it's open the scan view draws the fitted box (its edges, centre planes,
// axes and centre) and the fitted planes. A click on the scan either chooses one
// of those (Choose mode) or grows a flat area to fit a plane to (Select mode).
// A click on Alibre's own plane, axis or point makes it the target.

#pragma once

#include "StepDialog.h"

#include "Orient.h"

class COrientDlg : public CStepDialog
{
public:
    explicit COrientDlg(const std::wstring& sessionId);

    // Shows what the view changed: the chosen geometry, the target, the selection.
    void UpdateFromScan();
    // With Mid Plane down, the scan view chose the second feature: make the plane.
    void MidPlanePicked();

protected:
    BOOL OnInitDialog() override;

    afx_msg void OnDestroy();
    afx_msg void OnMove();
    afx_msg void OnCentre();
    afx_msg void OnRotate();
    afx_msg void OnScale();
    afx_msg void OnPickScan();
    afx_msg void OnPickTarget();
    afx_msg void OnSelectArea();
    afx_msg void OnGrowChanged();
    afx_msg void OnClearSelection();
    afx_msg void OnFitPlane();
    afx_msg void OnFitAxis();
    afx_msg void OnSelectCylinder();
    afx_msg void OnDelete();
    afx_msg void OnDeleteAll();
    afx_msg void OnFindSymmetry();
    afx_msg void OnMidPlane();
    afx_msg void OnAlign();
    afx_msg void OnStartOver();
    afx_msg void OnUndo();

    DECLARE_MESSAGE_MAP()

private:
    bool ReadNumber(int id, double& value);
    void SetStatus(const CString& text);
    // Moves the scan, starting the alignment over (it no longer holds).
    void MoveScan(const scancore::Transform& transform, const CString& status);
    void ScanChanged();
    // The chosen feature's index in ScanDocument::fitted, or -1 when nothing is
    // chosen or it's one of the box's.
    int ChosenFitted() const;
    // Pushes the button for mode down and the others up; pushing the one that's
    // down puts all of them up.
    void ToggleClickMode(int mode);
    void ShowClickMode();
    // Adds a fitted plane or axis, chooses it to align, and puts the buttons up.
    void AddFitted(const scancore::Feature& feature, const CString& status);

    int m_midFirst = -1;                // with Mid Plane down: the first feature, in Features()

    CComboBox m_rotateAxis;
    CComboBox m_rotatePivot;
    CComboBox m_scalePivot;
    CComboBox m_axisToPlane;
};

// Called by the scan view when what Orient shows has changed.
void OrientViewChanged();
// Called by the scan view when Mid Plane's second feature has been clicked.
void OrientMidPlanePicked();
