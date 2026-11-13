// ScanDocument.h : a part's scan: what's drawn, the points picked on it, what
// Orient works with, and what's saved inside the part. One per part session.
//
// Saving: Alibre asks HasPersistentDataToSave when it saves the part, then
// calls SaveData with a stream that's kept in the .AD_PRT, and LoadData with
// it when the part is opened (see AddOnInterface). The mesh is saved, and the
// fitted planes and axes with it (ScanData version 2); the fitted
// box is worked out again, and the alignment's progress lasts until the part
// is closed.

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Mesh.h"
#include "Orient.h"

struct ScanDocument
{
    std::string sourceName;         // the file it was loaded from, UTF-8
    scancore::Mesh mesh;            // the whole scan, in mm
    scancore::Box3 bounds;
    scancore::Mesh display;         // simplified for drawing
    std::vector<scancore::Point3> picked;

    bool fullDetail = false;        // draw the whole scan rather than the simplified one

    // Changed since the part was last saved or opened. Alibre asks
    // HasPersistentDataToSave all the time and treats yes as unsaved changes,
    // so it answers this, not just whether there's a scan.
    bool unsaved = false;

    // Orient: the box fitted to the scan (worked out when the mesh is loaded or
    // refined, then moved with the scan), planes and axes fitted to flat areas and cylinders, the
    // triangles chosen for the next fit, and the 3-2-1 alignment's progress.
    scancore::OrientedBox box;
    std::vector<scancore::Feature> fitted;
    std::vector<int> selection;                 // triangle numbers, sorted
    scancore::AlignState align;

    // While the Orient dialog is open the view draws the box and planes, and its
    // push-down buttons say what a click does (at most one is down): Scan, a click
    // chooses one of the scan's planes, axes or its centre; Target, Alibre's own
    // selection becomes the target; Area or Cylinder, a click grows a flat area
    // or a cylinder (outside or a hole) to select.
    // None, clicks are Alibre's alone.
    enum class ClickMode { None, Scan, Target, Area, Cylinder };
    bool orienting = false;
    ClickMode clickMode = ClickMode::None;
    double growAngle = 10.0;                    // degrees
    double growDistance = 0.2;                  // mm
    int chosen = -1;                            // in Features(), or -1
    bool haveTarget = false;                    // Alibre geometry clicked in the view
    scancore::Feature target;                   // in mm

    // The box's centre planes, axes and centre, then the fitted planes and axes.
    std::vector<scancore::Feature> Features() const;

    // The arrays the canvas takes, made once per level of detail, in the
    // part's model units: 1-based float vertices and normals, and 3 then three
    // vertex numbers for each triangle.
    struct DrawArrays
    {
        SAFEARRAY* vertices = nullptr;
        SAFEARRAY* normals = nullptr;
        SAFEARRAY* faces = nullptr;
        size_t triangles = 0;
        ~DrawArrays();
    };
    std::unique_ptr<DrawArrays> displayArrays;
    std::unique_ptr<DrawArrays> fullArrays;
    std::unique_ptr<DrawArrays> selectionArrays;

    // Set when the scan view starts, as the part may not be ready when its
    // scan is read from the file. Changing it remakes the arrays.
    void SetMillimetresPerUnit(double millimetresPerUnit);
    double MillimetresPerUnit() const { return m_millimetresPerUnit; }

    const DrawArrays* Arrays();
    const DrawArrays* SelectionArrays();         // nullptr when nothing's selected

    // Each triangle's neighbours, for growing a flat area (made when first asked).
    const std::vector<std::array<int, 3>>& Neighbours();

    void SetSelection(std::vector<int> triangles);

    // Changes, each kept for Undo (at most kUndoSteps).
    void ReplaceMesh(scancore::Mesh&& mesh);                     // Refine: the box is fitted again
    void Move(const scancore::Transform& transform, const scancore::AlignState& align);
    void AddFitted(const scancore::Feature& feature);
    bool CanUndo() const { return !m_undo.empty(); }
    void Undo();

private:
    struct Snapshot
    {
        scancore::Mesh mesh;
        scancore::OrientedBox box;
        std::vector<scancore::Feature> fitted;
        scancore::AlignState align;
        std::vector<scancore::Point3> picked;
    };
    void Remember();
    void DrawingChanged(bool trianglesChanged);

    std::vector<Snapshot> m_undo;               // oldest first
    std::vector<std::array<int, 3>> m_neighbours;
    double m_millimetresPerUnit = 10.0;
};

// Makes a document from a mesh: its box, fitted box and the simplified copy for drawing.
std::unique_ptr<ScanDocument> MakeScanDocument(scancore::Mesh&& mesh, const std::string& sourceName);

// The scans loaded so far, by session identifier.
ScanDocument* FindScan(const std::wstring& sessionId);
ScanDocument* AddScan(const std::wstring& sessionId, std::unique_ptr<ScanDocument> scan);
void ClearScans();

// Data from a part that couldn't be used (damaged, or from a newer ScanToAlibre):
// kept as it was and written back unchanged when the part is saved, unless a
// new scan is loaded in its place.
void KeepUnreadData(const std::wstring& sessionId, std::string bytes);
const std::string* FindUnreadData(const std::wstring& sessionId);
void ForgetUnreadData(const std::wstring& sessionId);

// How many changes Undo can take back. Each holds a whole scan (about 25 MB
// for a million triangles).
const size_t kUndoSteps = 5;

// The largest number of triangles drawn when not at full detail.
const size_t kDisplayTriangles = 300000;
