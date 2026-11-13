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

    // The smallest box round the scan, among boxes with one axis along a
    // candidate direction (the largest flat areas' normals, the world axes, the
    // principal directions), each turned about it to fit tightest, but square to
    // the part's large flat areas wherever that costs at most 5% more. axes[0] is
    // its longest side, axes[2] the shortest, right-handed; halfSize along each.
    struct OrientedBox
    {
        Point3 centre;
        Point3 axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        double halfSize[3] = { 0, 0, 0 };

        void ApplyTransform(const Transform& transform);
        std::array<Point3, 8> Corners() const;      // corner i has + along axis k when bit k of i is set
    };

    // The principal directions alone followed where the material is, which
    // tilted the box off a part's flat faces (2 degrees on Frank's popsicle
    // dispenser); the smallest box sits on them.
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

    // Grows from a triangle over a cylinder (outside or a hole): first over the
    // smooth surface (neighbours within angleDegrees of each other), and once
    // the normals turn enough to fit a cylinder, only over triangles within
    // distance (mm) of it whose normals point square to its axis. Refits as it
    // doubles. Returns the triangles, sorted.
    std::vector<int> GrowCylinder(const Mesh& mesh, const std::vector<std::array<int, 3>>& neighbours, int seed,
                                  double angleDegrees, double distance);

    struct CylinderFit
    {
        Feature axis;                   // the centre line: point midway along the triangles, unit direction
        double radius = 0.0;
        bool hole = false;              // the triangles face the axis
        double rms = 0.0;               // of their corners' distances from the cylinder
    };

    // The axis from the triangles' normals (the direction they're all square
    // to), then the centre and radius from a circle fitted to their corners seen
    // along it. Returns false if the normals don't turn enough to tell an axis.
    bool FitCylinder(const Mesh& mesh, const std::vector<int>& triangles, CylinderFit& fit);

    // 3-2-1 alignment, generalised: each step puts a scan plane on a target
    // plane, an axis on an axis, an axis onto a plane (square to it or lying in
    // it), or a point on a point, turning and sliding only in the ways earlier
    // steps left free, and then fixes what it lined up. The first step turns and
    // slides freely; a plane or axis then leaves turning about one line; a later
    // turn fixes that. A step that would fix nothing new is refused.
    struct AlignState
    {
        enum class Turn { Free, About, None };
        int step = 0;                   // steps done
        Turn turn = Turn::Free;
        Point3 turnAxis;                // with About: the line the scan may still turn about
        Point3 turnPivot;
        std::vector<Point3> slides{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };   // free directions, orthonormal

        bool Done() const { return turn == Turn::None && slides.empty(); }
    };

    enum class AxisToPlane { Square, InPlane };

    // Works out the next step's move. flip turns the scan's plane or axis the
    // other way round. Returns false, with a message, if they can't be aligned
    // now (such as a plane onto an axis, or a move earlier steps fixed).
    bool AlignStep(const Feature& scan, const Feature& target, bool flip, AxisToPlane axisToPlane, AlignState& state,
                   Transform& move, std::string& message);

    // What the alignment still leaves free, for showing: such as "turning about
    // (0, 0, 1); sliding in 2 directions".
    std::string FreedomText(const AlignState& state);
}
