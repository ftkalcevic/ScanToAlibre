// CylinderTests.cpp : growing and fitting cylinders (outside and holes), and
// aligning with axes: a plane, then a hole's axis, then a turn.

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "Orient.h"

using namespace scancore;

namespace
{
    const double kPi = 3.14159265358979323846;

    // The part of v square to unit n.
    Point3 Reject(const Point3& v, const Point3& n)
    {
        return v - n * Dot(v, n);
    }

    // A tube of the given radius and length about the z axis from z = 0, with a
    // flat cap at each end. Outward-facing, or facing the axis for a hole.
    Mesh Tube(double radius, double length, int segments, int rings, bool hole)
    {
        Mesh mesh;
        for (int r = 0; r <= rings; ++r)
            for (int k = 0; k < segments; ++k)
            {
                const double a = 2.0 * kPi * k / segments;
                mesh.vertices.push_back({ radius * std::cos(a), radius * std::sin(a), length * r / rings });
            }
        auto at = [segments](int r, int k) { return r * segments + (k % segments); };
        for (int r = 0; r < rings; ++r)
            for (int k = 0; k < segments; ++k)
            {
                mesh.triangles.push_back({ at(r, k), at(r, k + 1), at(r + 1, k + 1) });
                mesh.triangles.push_back({ at(r, k), at(r + 1, k + 1), at(r + 1, k) });
            }
        // Caps as fans about a middle vertex.
        const int bottom = static_cast<int>(mesh.vertices.size());
        mesh.vertices.push_back({ 0, 0, 0 });
        const int top = static_cast<int>(mesh.vertices.size());
        mesh.vertices.push_back({ 0, 0, length });
        for (int k = 0; k < segments; ++k)
        {
            mesh.triangles.push_back({ bottom, at(0, k + 1), at(0, k) });
            mesh.triangles.push_back({ top, at(rings, k), at(rings, k + 1) });
        }
        if (hole)
            for (auto& t : mesh.triangles)
                std::swap(t[1], t[2]);
        return mesh;
    }

    int TubeTriangles(int segments, int rings)
    {
        return 2 * segments * rings;
    }

    Transform Skew()
    {
        return Transform::Rotation({ 1, 0, 0 }, 0.4, {})
            .Then(Transform::Rotation({ 0, 1, 0 }, -0.7, {}))
            .Then(Transform::Rotation({ 0, 0, 1 }, 1.1, {}))
            .Then(Transform::Translation({ 17.0, -42.0, 230.0 }));
    }

    bool Parallel(const Point3& a, const Point3& b, double tolerance = 1e-9)
    {
        return std::fabs(std::fabs(Dot(Normalised(a), Normalised(b))) - 1.0) < tolerance;
    }

    double DistanceToLine(const Point3& p, const Feature& axis)
    {
        return Length(Cross(p - axis.point, Normalised(axis.direction)));
    }

    Feature Plane(const Point3& point, const Point3& normal)
    {
        Feature f;
        f.kind = Feature::Kind::Plane;
        f.point = point;
        f.direction = normal;
        f.name = "plane";
        return f;
    }

    Feature Axis(const Point3& point, const Point3& direction)
    {
        Feature f;
        f.kind = Feature::Kind::Axis;
        f.point = point;
        f.direction = direction;
        f.name = "axis";
        return f;
    }
}

TEST_CASE("growing from a tube's side takes the whole tube and stops at its caps, outside or a hole")
{
    for (bool hole : { false, true })
    {
        CAPTURE(hole);
        Mesh mesh = Tube(6.0, 30.0, 48, 10, hole);
        TransformMesh(mesh, Skew());
        const auto neighbours = TriangleNeighbours(mesh);
        const std::vector<int> area = GrowCylinder(mesh, neighbours, 5, 10.0, 0.2);
        CHECK(area.size() == static_cast<size_t>(TubeTriangles(48, 10)));

        CylinderFit fit;
        REQUIRE(FitCylinder(mesh, area, fit));
        CHECK(fit.radius == doctest::Approx(6.0).epsilon(1e-6));
        CHECK(fit.rms < 1e-6);
        CHECK(fit.hole == hole);
        CHECK(Parallel(fit.axis.direction, Skew().ApplyDirection({ 0, 0, 1 })));
        const Feature trueAxis = Axis(Skew().Apply({}), Skew().ApplyDirection({ 0, 0, 1 }));
        CHECK(DistanceToLine(fit.axis.point, trueAxis) < 1e-6);
        // Midway along it.
        CHECK(Length(fit.axis.point - Skew().Apply({ 0, 0, 15.0 })) < 1e-6);
    }
}

TEST_CASE("a quarter of a tube is enough to fit its axis and radius")
{
    Mesh mesh = Tube(20.0, 10.0, 64, 4, false);
    std::vector<int> quarter;
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 16; ++k)
        {
            quarter.push_back(2 * (r * 64 + k));
            quarter.push_back(2 * (r * 64 + k) + 1);
        }
    CylinderFit fit;
    REQUIRE(FitCylinder(mesh, quarter, fit));
    CHECK(fit.radius == doctest::Approx(20.0).epsilon(1e-6));
    CHECK(Parallel(fit.axis.direction, { 0, 0, 1 }));
    CHECK(Length(Reject(fit.axis.point, { 0, 0, 1 })) < 1e-6);
}

TEST_CASE("a flat patch has no axis to fit")
{
    Mesh mesh;
    mesh.vertices = { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 } };
    mesh.triangles = { { 0, 1, 2 }, { 0, 2, 3 } };
    CylinderFit fit;
    CHECK_FALSE(FitCylinder(mesh, { 0, 1 }, fit));
}

TEST_CASE("a face onto XY, a hole's axis onto Z, then a side onto ZX squares the part up")
{
    // The scan: a block's top face plane, a hole square to it at (10, 5), and a
    // side plane, all turned and moved by Skew.
    const Transform skew = Skew();
    Feature top = Plane(skew.Apply({ 0, 0, 10 }), skew.ApplyDirection({ 0, 0, 1 }));
    Feature hole = Axis(skew.Apply({ 10, 5, 0 }), skew.ApplyDirection({ 0, 0, 1 }));
    Feature side = Plane(skew.Apply({ 0, 20, 0 }), skew.ApplyDirection({ 0, 1, 0 }));

    AlignState state;
    std::string message;
    Transform move;
    auto apply = [&](const Transform& m) {
        top.ApplyTransform(m);
        hole.ApplyTransform(m);
        side.ApplyTransform(m);
    };

    REQUIRE(AlignStep(top, Plane({}, { 0, 0, 1 }), false, AxisToPlane::Square, state, move, message));
    apply(move);
    CHECK(state.turn == AlignState::Turn::About);
    CHECK(state.slides.size() == 2);

    // The hole is square to the face, so this slides only, onto the Z axis.
    REQUIRE(AlignStep(hole, Axis({}, { 0, 0, 1 }), false, AxisToPlane::Square, state, move, message));
    apply(move);
    CHECK(state.turn == AlignState::Turn::About);
    CHECK(state.slides.empty());
    CHECK(Length(Reject(hole.point, { 0, 0, 1 })) < 1e-9);
    CHECK(std::fabs(top.point.z) < 1e-9);

    // The side turns it about the Z axis (through the hole); its position is
    // held, so it's turned parallel to ZX, not moved onto it.
    REQUIRE(AlignStep(side, Plane({}, { 0, 1, 0 }), false, AxisToPlane::Square, state, move, message));
    apply(move);
    CHECK(state.Done());
    CHECK_FALSE(message.empty());
    CHECK(Parallel(side.direction, { 0, 1, 0 }));
    CHECK(Length(Reject(hole.point, { 0, 0, 1 })) < 1e-9);
    CHECK(Parallel(hole.direction, { 0, 0, 1 }));
    CHECK(std::fabs(top.point.z) < 1e-9);

    // Nothing's left to do.
    CHECK_FALSE(AlignStep(side, Plane({}, { 0, 1, 0 }), false, AxisToPlane::Square, state, move, message));
}

TEST_CASE("an axis onto a plane goes square to it or lies in it")
{
    const Point3 tilted = Normalised({ 0.3, -0.2, 1.0 });
    {
        AlignState state;
        std::string message;
        Transform move;
        Feature axis = Axis({ 5, 6, 7 }, tilted);
        REQUIRE(AlignStep(axis, Plane({}, { 0, 0, 1 }), false, AxisToPlane::Square, state, move, message));
        axis.ApplyTransform(move);
        CHECK(Parallel(axis.direction, { 0, 0, 1 }));
        CHECK(state.slides.size() == 3);        // where it crosses the plane is still free
    }
    {
        AlignState state;
        std::string message;
        Transform move;
        Feature axis = Axis({ 5, 6, 7 }, Normalised({ 1.0, 0.4, 0.3 }));
        REQUIRE(AlignStep(axis, Plane({}, { 0, 0, 1 }), false, AxisToPlane::InPlane, state, move, message));
        axis.ApplyTransform(move);
        CHECK(std::fabs(axis.direction.z) < 1e-9);
        CHECK(std::fabs(axis.point.z) < 1e-9);
        CHECK(state.slides.size() == 2);
    }
}

TEST_CASE("a point onto a point after a plane slides only within the plane, as near as it can")
{
    AlignState state;
    std::string message;
    Transform move;
    Feature plane = Plane({ 1, 2, 3 }, Normalised({ 0.1, 0.2, 1.0 }));
    REQUIRE(AlignStep(plane, Plane({}, { 0, 0, 1 }), false, AxisToPlane::Square, state, move, message));
    Feature centre;
    centre.kind = Feature::Kind::Point;
    centre.point = move.Apply({ 4, 5, 9 });     // off the plane, as a box's centre is off its face
    Feature origin;
    origin.kind = Feature::Kind::Point;
    REQUIRE(AlignStep(centre, origin, false, AxisToPlane::Square, state, move, message));
    const Point3 after = move.Apply(centre.point);
    CHECK(std::fabs(after.x) < 1e-9);
    CHECK(std::fabs(after.y) < 1e-9);
    CHECK(after.z == doctest::Approx(centre.point.z));      // the plane holds its height
    CHECK(state.slides.empty());
}
