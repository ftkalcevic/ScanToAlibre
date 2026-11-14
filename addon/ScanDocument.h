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

#include "Compare.h"
#include "Mesh.h"
#include "Orient.h"
#include "Section.h"
#include "Silhouette.h"
#include "SurfaceFit.h"

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
    // Smooth or Brush (5 Surface): a click selects a smooth area, or the
    // triangles within a radius, to fit a surface to.
    // None, clicks are Alibre's alone.
    enum class ClickMode { None, Scan, Target, Area, Cylinder, Smooth, Brush, MidPlane, Probe };
    // Show Scan turned off by the user: the scan isn't drawn anywhere, even by
    // the scan view while a dialog is open (Frank: Show Scan is the one control).
    bool scanHidden = false;
    bool orienting = false;
    ClickMode clickMode = ClickMode::None;
    double growAngle = 10.0;                    // degrees
    double growDistance = 0.2;                  // mm
    int chosen = -1;                            // in Features(), or -1
    bool haveTarget = false;                    // Alibre geometry clicked in the view
    scancore::Feature target;                   // in mm

    // Section (4 Section): while its dialog is open the view draws the plane,
    // the slice and its fitted lines and arcs. Scan down: a click chooses one of
    // the scan's planes; Target down: Alibre's design plane or flat face.
    struct Section
    {
        enum class Kind { Slice, Silhouette };
        Kind kind = Kind::Slice;
        double smallestHole = 1.0;              // mm across; silhouette holes and pieces smaller are left out
        // Sketch Options (Frank's, all on by default): the raw slice, the fitted
        // lines and arcs (the raw slice then as construction lines), and whether
        // arcs are fitted (snap.fitArcs) or lines only.
        bool createRaw = true;
        bool createFitted = true;
        scancore::SilhouetteInfo silhouetteInfo;
        bool havePlane = false;
        std::string planeName;                  // UTF-8
        scancore::Point3 point, normal;         // the chosen plane in mm, before the offset
        IDispatchPtr alibrePlane;               // Alibre's design plane or face; null for a scan plane
        bool alibreFace = false;
        double offset = 0.0;                    // mm along the normal
        double tolerance = 0.05;                // mm
        scancore::SnapOptions snap;
        scancore::SectionFrame frame;           // a guess at the sketch's axes, for the preview
        scancore::SectionResult result;
        std::vector<scancore::ProfileConstraint> constraints;
        std::string symmetryName;               // UTF-8: the plane Symmetric uses, empty when none
        int sketches = 0;                       // made so far, for names
    };
    // World planes 5 Surface's Detect found the scan symmetric about: XY, YZ, ZX.
    bool detectedSymmetry[3] = { false, false, false };

    bool sectioning = false;
    Section section;
    // 6 Silhouette: Section's dialog and drawing on a second Section of its own,
    // with its own plane and settings. Only one of the two is open at a time.
    bool silhouetting = false;
    Section silhouette = [] {
        Section s;
        s.kind = Section::Kind::Silhouette;
        return s;
    }();
    // The open one of the two, or nullptr.
    Section* ActiveSection() { return silhouetting ? &silhouette : (sectioning ? &section : nullptr); }
    bool Sectioning() const { return sectioning || silhouetting; }
    // Slices (or outlines) and fits the open one again (after the plane,
    // offset, tolerance or scan changes).
    void UpdateSection();
    // Sets section's symmetry line in frame: where the first symmetry plane
    // square to the frame meets it (fitted planes named "Symmetry plane" or
    // "Mid plane", then world planes Detect found), or none.
    void SetSymmetryLine(Section& section, const scancore::SectionFrame& frame) const;

    // Surface (5 Surface): while its dialog is open, clicks in Smooth or Brush
    // mode add to the selection (or take an area away), and the view draws the
    // selection, the fitted surface and its outline.
    struct Surface
    {
        double smoothAngle = 15.0;              // degrees between neighbouring triangles
        double brushRadius = 3.0;               // mm
        scancore::SurfaceFitOptions options;
        scancore::SurfaceFitResult result;      // ok false when there's nothing fitted
        int surfaces = 0;                       // made so far, for file names
    };
    bool surfacing = false;
    Surface surface;
    // Fits again (after the selection, an option or the scan changes).
    void UpdateSurface();

    // Compare (7 Compare): while its dialog is open the scan is drawn in colour
    // bands by its distance from the part as built so far (read when the dialog
    // opens). Probe down: a click on the scan reads the distance there.
    struct Compare
    {
        double tolerance = 0.1;                 // mm: green either way
        double range = 1.0;                     // mm: beyond it, grey
        bool havePart = false;
        scancore::Mesh part;                    // the part's facets, mm
        double searched = 0.0;                  // the range the values were found within; 0 when there are none
        std::vector<float> meshValues;          // per vertex of mesh: statistics and the probe
        std::vector<float> displayValues;       // per vertex of display: the colours
        scancore::DeviationStats stats;
        bool haveProbe = false;
        scancore::Point3 probe;
        double probeValue = 0.0;                // NaN: not modelled there
    };
    bool comparing = false;
    Compare compare;
    // Every vertex's distance from the part, found again (when the part is read,
    // or the range grows past what was searched).
    void UpdateCompare();
    // The statistics and colours again, after the tolerance or range changes.
    void RebandCompare();

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
    // Compare: one band's triangles (a triangle's band from the mean of its
    // corners), at the drawn level of detail; nullptr when it has none or
    // nothing's compared.
    const DrawArrays* BandArrays(int band);

    // Each triangle's neighbours, for growing a flat area (made when first asked).
    const std::vector<std::array<int, 3>>& Neighbours();

    void SetSelection(std::vector<int> triangles);

    // Changes, each kept for Undo (at most kUndoSteps).
    void ReplaceMesh(scancore::Mesh&& mesh);                     // Refine: the box is fitted again
    void Move(const scancore::Transform& transform, const scancore::AlignState& align);
    void AddFitted(const scancore::Feature& feature);
    // Removes fitted[index] (not the box's own planes, axes and centre).
    void DeleteFitted(size_t index);
    void DeleteAllFitted();                                       // every fitted plane and axis
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

    void ResetBands();

    std::vector<Snapshot> m_undo;               // oldest first
    std::unique_ptr<DrawArrays> m_bandArrays[scancore::kDeviationBands];
    bool m_bandsMade = false;
    bool m_bandsFull = false;                   // made from the full mesh rather than the simplified one
    std::vector<std::array<int, 3>> m_neighbours;
    double m_millimetresPerUnit = 10.0;
};

// A section's slice (4 Section) or outline (6 Silhouette) in the frame, by its kind.
scancore::SectionResult SectionOf(const scancore::Mesh& mesh, ScanDocument::Section& section,
                                  const scancore::SectionFrame& frame);

// A colour band's colour (0 to 255 each), shared by the view and Compare's legend.
void DeviationColour(int band, long rgb[3]);

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

// How closely the reference slice (construction lines in the sketch) follows
// the scan, in mm.
const double kReferenceTolerance = 0.01;
