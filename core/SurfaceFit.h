// SurfaceFit.h : 5 Surface: selecting areas of the scan, and fitting a bicubic
// B-spline surface to them as heights over a base plane, for Alibre to insert.
//
// The surface is a height field: each selected point is (s, t) on the base plane
// and h along its normal. Heights at the control points are solved by least
// squares with a bending (thin-plate) penalty, which also fills holes and the
// corners outside the selection. Control points are added until the fit is
// within the tolerance. Symmetry mirrors the points across world planes through
// the origin, on a domain symmetric about them, so the answer is symmetric.

#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "BSplineSurface.h"
#include "Geometry.h"
#include "Mesh.h"

namespace scancore
{
    // Grows from a triangle over neighbours whose normals differ from the
    // triangle they're reached from by less than angleDegrees. Returns the
    // triangles, sorted.
    std::vector<int> GrowSmoothArea(const Mesh& mesh, const std::vector<std::array<int, 3>>& neighbours, int seed,
                                    double angleDegrees);

    // The triangles joined to the seed that have a corner within radius (mm) of
    // centre, sorted.
    std::vector<int> BrushArea(const Mesh& mesh, const std::vector<std::array<int, 3>>& neighbours, int seed,
                               const Point3& centre, double radius);

    enum class SurfaceBase { Auto, XY, YZ, ZX };
    // Projection: trimmed to the outline of the whole scan projected on the
    // base plane. Selection: trimmed to the selection's outline there.
    // Rectangle: the rectangle round the selection's outline. Box: the
    // rectangle round the scan's box (boxCorners) projected on the base plane.
    enum class SurfacePerimeter { Projection, Rectangle, Box, Selection };

    struct SurfaceFitOptions
    {
        SurfaceBase base = SurfaceBase::Auto;
        bool symmetryXY = false;        // mirrored across the plane z = 0
        bool symmetryYZ = false;        // x = 0
        bool symmetryZX = false;        // y = 0
        SurfacePerimeter perimeter = SurfacePerimeter::Selection;
        double offset = 0.0;            // mm the perimeter is made bigger by, every way
        std::vector<Point3> boxCorners; // the scan's box, for Box
        double tolerance = 0.05;        // mm, for 99% of the points
        double smoothing = 0.2;         // 0 to 1
        int maxControlPoints = 40;      // each way
        size_t maxSamples = 60000;
    };

    // The plane the heights are measured from: point, in-plane directions u and v,
    // and the normal n (u x v), all unit.
    struct SurfaceFrame
    {
        Point3 origin, u, v, n;
    };

    struct SurfaceFitResult
    {
        bool ok = false;
        std::string message;            // why it failed, UTF-8

        SurfaceFrame frame;
        double s0 = 0, s1 = 0, t0 = 0, t1 = 0;     // the domain on the base plane, mm
        BSplineSurface surface;         // parameters 0 to 1 each way, in mm

        // The trim, for Projection and Selection: the outline in the surface's parameters
        // (anticlockwise, not repeating its first point) and the same points on
        // the surface. Empty for Rectangle.
        std::vector<std::array<double, 2>> outlineUV;
        std::vector<Point3> outline;

        size_t samples = 0;             // fitted, with mirrored ones
        double largest = 0.0;           // mm, along the normal
        double within99 = 0.0;          // 99% of the samples are within this
        double rms = 0.0;
        double seconds = 0.0;
    };

    // The frame for these options and triangles; false (with a message) if the
    // options contradict each other (a symmetry plane parallel to the base, or
    // three symmetry planes).
    bool SurfaceBaseFrame(const Mesh& mesh, const std::vector<int>& triangles, const SurfaceFitOptions& options,
                          SurfaceFrame& frame, std::string& message);

    SurfaceFitResult FitScanSurface(const Mesh& mesh, const std::vector<int>& triangles,
                                    const SurfaceFitOptions& options);

    // The height (along the frame's normal) of a height-field surface at a
    // point (s, t) of the domain.
    double SurfaceHeightAt(const SurfaceFitResult& result, double s, double t);

    // The outer outline of a set of points' triangles projected on a plane, as a
    // closed polygon (anticlockwise, not repeating its first point): triangles are
    // given as corner triples (s, t). Holes inside are filled, and the outline is
    // grown by offset (mm). cell is the grid's size; the outline is simplified to
    // within simplify. Only the largest piece's outline is returned.
    std::vector<std::array<double, 2>> ProjectionOutline(const std::vector<std::array<std::array<double, 2>, 3>>& triangles,
                                                          double s0, double s1, double t0, double t1, double cell,
                                                          double simplify, double offset = 0.0);

    // The same, with the triangles given one at a time by number (so a whole
    // scan needn't be copied).
    using ProjectedTriangle = std::array<std::array<double, 2>, 3>;
    std::vector<std::array<double, 2>> ProjectionOutline(size_t count,
                                                          const std::function<void(size_t, ProjectedTriangle&)>& triangle,
                                                          double s0, double s1, double t0, double t1, double cell,
                                                          double simplify, double offset = 0.0);
}
