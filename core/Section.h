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
    // none that's round within tolerance becomes one circle.
    Profile FitProfile(const std::vector<Vec2>& points, bool closed, double tolerance);

    struct SnapOptions
    {
        bool horizontalVertical = true;
        double horizontalVerticalDegrees = 2.0;
        bool tangent = true;
        double tangentDegrees = 3.0;
        bool concentric = true;
        double concentricDistance = 0.05;   // mm, centres this close are concentric
        double roundTo = 0.1;               // mm for arc and circle radii; 0 leaves them as fitted
    };

    struct ProfileConstraint
    {
        enum class Kind { Horizontal, Vertical, Tangent, Concentric, Radius };
        Kind kind = Kind::Horizontal;
        size_t profile = 0, segment = 0;            // the segment it's on
        size_t otherProfile = 0, otherSegment = 0;  // tangent (the next segment) and concentric
        double value = 0.0;                         // radius, rounded
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
    };
    SectionResult SectionMesh(const Mesh& mesh, const SectionFrame& frame, double tolerance,
                              double referenceTolerance);

    // A frame for a plane: origin, and axes along the projection of X (or Y,
    // when X is nearly square to the plane) and normal cross that.
    SectionFrame FrameForPlane(const Point3& origin, const Point3& normal);
}
