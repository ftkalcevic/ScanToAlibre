// Orient.h : moving, turning and scaling a scan, and lining it up with the
// part's planes and axes.
//
// A fitted box (from the scan's principal directions) gives centre planes, axes
// and a centre; a plane can be fitted to a flat area chosen by growing from a
// click; and alignment goes in 3-2-1 steps, each keeping what earlier steps fixed.

#pragma once

#include <array>
#include <string>
#include <vector>

#include "Mesh.h"

namespace scancore
{
    // A rigid move with a uniform scale: p' = rotation * p * scale + translation
    // (the scale folded into the matrix).
    struct Transform
    {
        double m[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        Point3 t;

        Point3 Apply(const Point3& p) const;
        Point3 ApplyDirection(const Point3& d) const;       // the matrix alone, made unit length
        Transform Then(const Transform& next) const;        // this first, then next

        static Transform Translation(const Point3& offset);
        // About the line through pivot along axis (unit), by angle (radians, anticlockwise seen from the axis's tip).
        static Transform Rotation(const Point3& axis, double angle, const Point3& pivot);
        static Transform Scaling(double factor, const Point3& pivot);
        // The shortest turn taking direction from onto direction to (both unit), about pivot.
        static Transform RotationBetween(const Point3& from, const Point3& to, const Point3& pivot);
    };

    void TransformMesh(Mesh& mesh, const Transform& transform);

    // Eigenvalues (ascending) and unit eigenvectors of a symmetric 3 x 3 matrix, by Jacobi rotations.
    void SymmetricEigen(const double a[3][3], double values[3], Point3 vectors[3]);

    // A box turned to the scan's principal directions: axes[0] along the longest
    // spread, axes[2] the shortest, right-handed; halfSize along each.
    struct OrientedBox
    {
        Point3 centre;
        Point3 axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        double halfSize[3] = { 0, 0, 0 };

        void ApplyTransform(const Transform& transform);
        std::array<Point3, 8> Corners() const;      // corner i has + along axis k when bit k of i is set
    };

    // From the surface (area-weighted triangles), so uneven point spacing doesn't tilt it.
    OrientedBox FitOrientedBox(const Mesh& mesh);

    // Geometry to align: a plane (point and unit normal), an axis (point and unit
    // direction) or a point.
    struct Feature
    {
        enum class Kind { Plane, Axis, Point };
        Kind kind = Kind::Plane;
        Point3 point;
        Point3 direction;
        std::string name;               // UTF-8, for showing
        double halfWidth = 0.0;         // for drawing a plane: half the side of its square; an axis's half length

        void ApplyTransform(const Transform& transform);
    };

    // The box's three centre planes ("Box plane A" square to the longest
    // direction), three axes and its centre.
    std::vector<Feature> BoxFeatures(const OrientedBox& box);

    // Each triangle's neighbours across its edges (-1 where there's none).
    std::vector<std::array<int, 3>> TriangleNeighbours(const Mesh& mesh);

    // Grows from a triangle over neighbours whose normals are within angleDegrees
    // of the area's fitted plane and whose middles are within distance (mm) of it,
    // refitting as it goes. Returns the triangles, sorted.
    std::vector<int> GrowFlatArea(const Mesh& mesh, const std::vector<std::array<int, 3>>& neighbours, int seed,
                                  double angleDegrees, double distance);

    // The least-squares plane through the triangles (area-weighted, over each
    // triangle's surface), its normal on the side the triangles face. rms is the
    // root mean square distance of their corners from it.
    bool FitPlane(const Mesh& mesh, const std::vector<int>& triangles, Feature& plane, double* rms = nullptr);

    // 3-2-1 alignment. Step 1 puts a scan plane on a target plane (or an axis on
    // an axis), turning and moving freely. Step 2 turns only about the line step
    // 1 fixed, and moves only where step 1 allows. Step 3 moves only along what's
    // left. A point onto a point moves only, within whatever's still free.
    struct AlignState
    {
        int step = 0;                   // steps done: 0 to 3
        bool firstWasPlane = true;
        Point3 turnAxis;                // after step 1: the line the scan may still turn about
        Point3 turnPivot;
        Point3 slide;                   // after step 2: the one direction still free
    };

    // Works out the next step's move. flip turns the scan's plane or axis the
    // other way round. Returns false, with a message, if the two can't be aligned
    // at this step (such as a plane onto an axis, or a second plane parallel to the first).
    bool AlignStep(const Feature& scan, const Feature& target, bool flip, AlignState& state, Transform& move,
                   std::string& message);
}
