// Section.h : sections through a scan, for sketches.
//
// A plane slices the mesh into polylines (closed loops and open runs). Each,
// laid in the plane's own 2D frame, is fitted with lines and arcs within a
// tolerance, joined end to end. FindConstraints then works out the design
// intent to give Alibre: lines near horizontal or vertical, tangent joins,
// concentric arcs, and rounded sizes. The slice itself, simplified, goes into
// the sketch as reference geometry.

#pragma once

#include <string>
#include <vector>

#include "Mesh.h"

namespace scancore
{
    struct Vec2
    {
        double x = 0.0;
        double y = 0.0;
    };

    // A plane with its own 2D axes: x and y unit and square, the normal x cross y.
    struct SectionFrame
    {
        Point3 origin;
        Point3 xAxis{ 1, 0, 0 };
        Point3 yAxis{ 0, 1, 0 };

        Point3 Normal() const;
        Vec2 ToPlane(const Point3& p) const;
        Point3 ToWorld(const Vec2& p) const;
    };

    struct SlicePolyline
    {
        std::vector<Point3> points;     // a closed loop doesn't repeat its first point
        bool closed = false;
    };

    // Where the plane through point with the given normal cuts the mesh:
    // triangle crossings chained by the edges they share.
    std::vector<SlicePolyline> SliceMesh(const Mesh& mesh, const Point3& point, const Point3& normal);

    struct ProfileSegment
    {
        enum class Kind { Line, Arc, Circle };
        Kind kind = Kind::Line;
        Vec2 start, end;                // a circle starts and ends at the same point
        Vec2 centre;                    // arcs and circles
        double radius = 0.0;
        bool anticlockwise = true;      // arcs: from start to end, seen with the normal towards you
        int firstPoint = 0, lastPoint = 0;  // the slice's points it covers (a loop's last segment may wrap)

        // Least-squares fits to the points it covers, for judging design
        // intent (the segment itself passes through its end points).
        Vec2 fitPoint, fitDirection;        // lines: a point and a unit direction
        Vec2 fitCentre;                     // arcs and circles
        double fitRadius = 0.0;
    };

    struct Profile
    {
        std::vector<ProfileSegment> segments;
        bool closed = false;
        double largestDeviation = 0.0;  // of the slice's points from the segments
    };

    // Lines and arcs through a slice's points (in the plane's frame), within
    // tolerance. A closed loop starts at its sharpest corner, and a loop with
    // none that's round within tolerance becomes one circle. With arcs false,
    // lines only (no arcs or circles).
    Profile FitProfile(const std::vector<Vec2>& points, bool closed, double tolerance, bool arcs = true);

    struct SnapOptions
    {
        bool fitArcs = true;                // false: profiles are fitted with lines only (Sketch Options)
        bool horizontalVertical = true;
        double horizontalVerticalDegrees = 2.0;
        bool tangent = true;
        double tangentDegrees = 3.0;
        bool concentric = true;
        double concentricDistance = 0.05;   // mm, centres this close are concentric
        double roundTo = 0.1;               // mm for arc and circle radii; 0 leaves them as fitted
        // Symmetric: the profiles made exactly symmetric about the symmetry
        // line (where a symmetry plane, square to the sketch, meets it), and
        // their matching points constrained symmetric (MakeSymmetric).
        bool symmetric = false;
        bool haveSymmetryLine = false;
        Vec2 symmetryPoint, symmetryDirection;  // in the frame; the direction unit
        double symmetryDistance = 0.05;     // mm (not used by MakeSymmetric, which takes the fit's tolerance)
        double symmetryDegrees = 2.0;       // a line this near square to the symmetry line runs across it
    };

    struct ProfileConstraint
    {
        enum class Kind { Horizontal, Vertical, Tangent, Concentric, Radius, Symmetric };
        Kind kind = Kind::Horizontal;
        size_t profile = 0, segment = 0;            // the segment it's on
        size_t otherProfile = 0, otherSegment = 0;  // tangent (the next segment) and concentric
        double value = 0.0;                         // radius, rounded
        // Symmetric: which point of each segment (0 start, 1 end, 2 centre)
        // mirrors which, about the symmetry line.
        int point = 0, otherPoint = 0;
    };

    std::vector<ProfileConstraint> FindConstraints(const std::vector<Profile>& profiles, const SnapOptions& options);


    // value to the nearest step (step 0 or less leaves it).
    double RoundTo(double value, double step);

    // Douglas-Peucker: the fewest points keeping every point within tolerance.
    std::vector<Vec2> SimplifyPolyline(const std::vector<Vec2>& points, bool closed, double tolerance);

    // A whole section: the mesh sliced by the frame's plane, each polyline fitted
    // in the frame, and simplified for the reference slice.
    struct SectionResult
    {
        std::vector<SlicePolyline> slices;
        std::vector<Profile> profiles;              // one per slice, in the frame
        std::vector<std::vector<Vec2>> reference;   // one per slice, in the frame
        size_t slicePoints = 0;
        double largestDeviation = 0.0;
        // How far the scan reaches from the plane along its normal: behind is 0
        // or less, ahead 0 or more (a suggested extrude depth).
        double behind = 0.0, ahead = 0.0;
        // Symmetric: loops cut at the symmetry line and made of one half and its
        // mirror image, and loops paired with their mirror image.
        size_t symmetricSplit = 0, symmetricPaired = 0;
        // How far apart the scan's two sides are at most (a point from the other
        // side's mirror image), mm; the fit goes halfway between.
        double symmetricGap = 0.0;
    };
    SectionResult SectionMesh(const Mesh& mesh, const SectionFrame& frame, double tolerance,
                              double referenceTolerance, bool arcs = true);

    // With options.symmetric and a symmetry line, the profiles are fitted again
    // so they're exactly symmetric about it. Fitting each side alone and pairing
    // what came out failed on a real scan (the fit breaks the two sides into
    // different pieces), so it's done before fitting:
    // - a closed slice crossing the line twice is cut there, each half fitted
    //   (FitProfile, open), and the better fit (smaller largest deviation) and
    //   its mirror image make the loop; where a half's end segment meets its
    //   mirror at the line (a line within symmetryDegrees of square to it, or an
    //   arc whose centre is within the tolerance of it) the two become one
    //   segment across it, and half a circle becomes a circle;
    // - a closed slice wholly on one side is paired with the one whose centroid
    //   mirrors its own (within 5 tolerances or 5% of its size) and whose area
    //   is within 10%: the better fit and its mirror image are used for both;
    // - other slices are left as fitted.
    // Returns a Symmetric constraint for each joint (a segment's start, or a
    // circle's centre) of the changed profiles and its exact mirror; joints on
    // the line get none.
    std::vector<ProfileConstraint> MakeSymmetric(SectionResult& result, const SectionFrame& frame, double tolerance,
                                                 const SnapOptions& options);

    // MakeSymmetric, then FindConstraints: the whole design intent.
    std::vector<ProfileConstraint> ApplyDesignIntent(SectionResult& result, const SectionFrame& frame,
                                                     double tolerance, const SnapOptions& options);

    // A frame for a plane: origin, and axes along the projection of X (or Y,
    // when X is nearly square to the plane) and normal cross that.
    SectionFrame FrameForPlane(const Point3& origin, const Point3& normal);
}
