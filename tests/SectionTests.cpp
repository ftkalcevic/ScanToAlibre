// SectionTests.cpp : slicing a scan, fitting lines and arcs, design intent.

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ScanFile.h"

#include "Orient.h"
#include "Section.h"
#include "Silhouette.h"
#include "Symmetry.h"

using namespace scancore;

namespace
{
    const double kPi = 3.14159265358979323846;

    // A tube round Z: n around, rings every dz from z0 to z1, open ends.
    Mesh TubeMesh(double radius, double z0, double z1, int n, int rings)
    {
        Mesh mesh;
        for (int r = 0; r <= rings; ++r)
            for (int k = 0; k < n; ++k)
            {
                const double a = 2.0 * kPi * k / n;
                mesh.vertices.push_back({ radius * std::cos(a), radius * std::sin(a), z0 + (z1 - z0) * r / rings });
            }
        for (int r = 0; r < rings; ++r)
            for (int k = 0; k < n; ++k)
            {
                const int a = r * n + k, b = r * n + (k + 1) % n, c = a + n, d = b + n;
                mesh.triangles.push_back({ a, b, d });
                mesh.triangles.push_back({ a, d, c });
            }
        return mesh;
    }

    // A closed box from -h to +h, two triangles a face.
    Mesh BoxMesh(double hx, double hy, double hz)
    {
        Mesh mesh;
        for (int i = 0; i < 8; ++i)
            mesh.vertices.push_back({ (i & 1) ? hx : -hx, (i & 2) ? hy : -hy, (i & 4) ? hz : -hz });
        const int quads[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 },
                                  { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for (const auto& q : quads)
        {
            mesh.triangles.push_back({ q[0], q[1], q[2] });
            mesh.triangles.push_back({ q[0], q[2], q[3] });
        }
        return mesh;
    }

    void AddLine(std::vector<Vec2>& p, Vec2 a, Vec2 b, double spacing)
    {
        const int n = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / spacing)));
        for (int k = 0; k < n; ++k)
            p.push_back({ a.x + (b.x - a.x) * k / n, a.y + (b.y - a.y) * k / n });
    }

    void AddArc(std::vector<Vec2>& p, Vec2 c, double r, double from, double to, double spacing)
    {
        const int n = std::max(2, static_cast<int>(std::ceil(std::fabs(to - from) * r / spacing)));
        for (int k = 0; k < n; ++k)
        {
            const double a = from + (to - from) * k / n;
            p.push_back({ c.x + r * std::cos(a), c.y + r * std::sin(a) });
        }
    }

    // A rectangle w by h with corners rounded r, anticlockwise, from the bottom edge.
    std::vector<Vec2> RoundedRectangle(double w, double h, double r, double spacing)
    {
        std::vector<Vec2> p;
        const double x = w / 2, y = h / 2;
        AddLine(p, { -x + r, -y }, { x - r, -y }, spacing);
        AddArc(p, { x - r, -y + r }, r, -kPi / 2, 0, spacing);
        AddLine(p, { x, -y + r }, { x, y - r }, spacing);
        AddArc(p, { x - r, y - r }, r, 0, kPi / 2, spacing);
        AddLine(p, { x - r, y }, { -x + r, y }, spacing);
        AddArc(p, { -x + r, y - r }, r, kPi / 2, kPi, spacing);
        AddLine(p, { -x, y - r }, { -x, -y + r }, spacing);
        AddArc(p, { -x + r, -y + r }, r, kPi, 1.5 * kPi, spacing);
        return p;
    }

    size_t Count(const std::vector<ProfileConstraint>& c, ProfileConstraint::Kind kind)
    {
        size_t n = 0;
        for (const auto& k : c)
            n += (k.kind == kind) ? 1 : 0;
        return n;
    }

    size_t CountKind(const Profile& p, ProfileSegment::Kind kind)
    {
        size_t n = 0;
        for (const auto& s : p.segments)
            n += (s.kind == kind) ? 1 : 0;
        return n;
    }
}

TEST_CASE("a box sliced through its middle is one closed square")
{
    const Mesh box = BoxMesh(10, 20, 5);
    const auto slices = SliceMesh(box, { 0, 0, 1.0 }, { 0, 0, 1 });
    REQUIRE(slices.size() == 1);
    CHECK(slices[0].closed);
    for (const Point3& p : slices[0].points)
    {
        CHECK(p.z == doctest::Approx(1.0));
        const bool onSide = std::fabs(std::fabs(p.x) - 10) < 1e-9 || std::fabs(std::fabs(p.y) - 20) < 1e-9;
        CHECK(onSide);
    }

    SectionFrame frame;
    frame.origin = { 0, 0, 1 };
    std::vector<Vec2> flat;
    for (const Point3& p : slices[0].points)
        flat.push_back(frame.ToPlane(p));
    const Profile profile = FitProfile(flat, true, 0.05);
    CHECK(profile.segments.size() == 4);
    CHECK(CountKind(profile, ProfileSegment::Kind::Line) == 4);
    const auto c = FindConstraints({ profile }, SnapOptions{});
    CHECK(Count(c, ProfileConstraint::Kind::Horizontal) == 2);
    CHECK(Count(c, ProfileConstraint::Kind::Vertical) == 2);
    CHECK(Count(c, ProfileConstraint::Kind::Tangent) == 0);
}

TEST_CASE("a slice missing the mesh is empty, and one along a tube is two open runs")
{
    const Mesh tube = TubeMesh(10, 0, 40, 64, 8);
    CHECK(SliceMesh(tube, { 0, 0, 50 }, { 0, 0, 1 }).empty());

    const auto along = SliceMesh(tube, { 0, 0.3, 0 }, { 0, 1, 0 });
    REQUIRE(along.size() == 2);
    for (const auto& s : along)
    {
        CHECK_FALSE(s.closed);
        CHECK(s.points.size() == 17);     // 9 rings and 8 diagonals
    }
}

TEST_CASE("a tube sliced across is one circle of its radius")
{
    const Mesh tube = TubeMesh(12.5, 0, 30, 256, 3);
    const auto slices = SliceMesh(tube, { 0, 0, 15 }, { 0, 0, 1 });
    REQUIRE(slices.size() == 1);
    CHECK(slices[0].closed);

    SectionFrame frame;
    std::vector<Vec2> flat;
    for (const Point3& p : slices[0].points)
        flat.push_back(frame.ToPlane(p));
    const Profile profile = FitProfile(flat, true, 0.05);
    REQUIRE(profile.segments.size() == 1);
    CHECK(profile.segments[0].kind == ProfileSegment::Kind::Circle);
    CHECK(profile.segments[0].radius == doctest::Approx(12.5).epsilon(0.001));
    CHECK(std::fabs(profile.segments[0].centre.x) < 0.01);
    CHECK(std::fabs(profile.segments[0].centre.y) < 0.01);

    const auto c = FindConstraints({ profile }, SnapOptions{});
    REQUIRE(Count(c, ProfileConstraint::Kind::Radius) == 1);
    for (const auto& k : c)
        if (k.kind == ProfileConstraint::Kind::Radius)
            CHECK(k.value == doctest::Approx(12.5));
}

TEST_CASE("a section frame maps to the plane and back")
{
    SectionFrame frame;
    frame.origin = { 1, 2, 3 };
    frame.xAxis = Normalised({ 1, 1, 0 });
    frame.yAxis = { 0, 0, 1 };
    const Point3 n = frame.Normal();
    CHECK(Dot(n, frame.xAxis) == doctest::Approx(0.0));
    CHECK(Dot(n, frame.yAxis) == doctest::Approx(0.0));
    const Point3 w = frame.ToWorld({ 3.0, -4.0 });
    const Vec2 back = frame.ToPlane(w);
    CHECK(back.x == doctest::Approx(3.0));
    CHECK(back.y == doctest::Approx(-4.0));
}

TEST_CASE("a rounded rectangle fits four lines and four arcs, tangent, horizontal and vertical")
{
    const std::vector<Vec2> p = RoundedRectangle(60, 40, 5, 0.2);
    const Profile profile = FitProfile(p, true, 0.05);
    CHECK(profile.closed);
    CHECK(CountKind(profile, ProfileSegment::Kind::Line) == 4);
    CHECK(CountKind(profile, ProfileSegment::Kind::Arc) == 4);
    CHECK(profile.largestDeviation <= 0.05);
    for (const auto& s : profile.segments)
        if (s.kind == ProfileSegment::Kind::Arc)
        {
            CHECK(s.radius == doctest::Approx(5.0).epsilon(0.01));
            CHECK(s.anticlockwise);
        }
    // Joined end to end, round the loop.
    for (size_t k = 0; k < profile.segments.size(); ++k)
    {
        const auto& a = profile.segments[k];
        const auto& b = profile.segments[(k + 1) % profile.segments.size()];
        CHECK(std::hypot(a.end.x - b.start.x, a.end.y - b.start.y) < 1e-9);
    }

    const auto c = FindConstraints({ profile }, SnapOptions{});
    CHECK(Count(c, ProfileConstraint::Kind::Horizontal) == 2);
    CHECK(Count(c, ProfileConstraint::Kind::Vertical) == 2);
    CHECK(Count(c, ProfileConstraint::Kind::Tangent) == 8);
    CHECK(Count(c, ProfileConstraint::Kind::Radius) == 4);
    for (const auto& k : c)
        if (k.kind == ProfileConstraint::Kind::Radius)
            CHECK(k.value == doctest::Approx(5.0));
}

TEST_CASE("a noisy rounded rectangle still fits within tolerance and keeps its intent")
{
    std::vector<Vec2> p = RoundedRectangle(60, 40, 8, 0.2);
    unsigned seed = 12345;
    for (Vec2& q : p)
    {
        seed = seed * 1103515245u + 12345u;
        const double a = ((seed >> 8) % 10000) / 10000.0 - 0.5;
        seed = seed * 1103515245u + 12345u;
        const double b = ((seed >> 8) % 10000) / 10000.0 - 0.5;
        q.x += a * 0.02;
        q.y += b * 0.02;
    }
    const Profile profile = FitProfile(p, true, 0.05);
    CHECK(profile.largestDeviation <= 0.05);
    CHECK(CountKind(profile, ProfileSegment::Kind::Line) == 4);
    CHECK(CountKind(profile, ProfileSegment::Kind::Arc) == 4);
    const auto c = FindConstraints({ profile }, SnapOptions{});
    CHECK(Count(c, ProfileConstraint::Kind::Horizontal) == 2);
    CHECK(Count(c, ProfileConstraint::Kind::Vertical) == 2);
    CHECK(Count(c, ProfileConstraint::Kind::Tangent) == 8);
    for (const auto& k : c)
        if (k.kind == ProfileConstraint::Kind::Radius)
            CHECK(k.value == doctest::Approx(8.0));
}

TEST_CASE("an open profile of a line and an arc, a turned shoulder")
{
    std::vector<Vec2> p;
    AddLine(p, { 0, 0 }, { 20, 0 }, 0.1);
    AddArc(p, { 20, 3 }, 3, -kPi / 2, 0, 0.1);
    AddLine(p, { 23, 3 }, { 23, 10 }, 0.1);
    p.push_back({ 23, 10 });
    const Profile profile = FitProfile(p, false, 0.02);
    REQUIRE(profile.segments.size() == 3);
    CHECK(profile.segments[0].kind == ProfileSegment::Kind::Line);
    CHECK(profile.segments[1].kind == ProfileSegment::Kind::Arc);
    CHECK(profile.segments[2].kind == ProfileSegment::Kind::Line);
    CHECK(profile.segments[1].radius == doctest::Approx(3.0).epsilon(0.01));
    CHECK(profile.segments[0].start.x == doctest::Approx(0.0));
    CHECK(profile.segments[2].end.y == doctest::Approx(10.0));
    const auto c = FindConstraints({ profile }, SnapOptions{});
    CHECK(Count(c, ProfileConstraint::Kind::Tangent) == 2);
    CHECK(Count(c, ProfileConstraint::Kind::Horizontal) == 1);
    CHECK(Count(c, ProfileConstraint::Kind::Vertical) == 1);
}

TEST_CASE("sharp corners aren't tangent and slanted lines aren't horizontal")
{
    std::vector<Vec2> p;
    AddLine(p, { 0, 0 }, { 30, 0 }, 0.2);
    AddLine(p, { 30, 0 }, { 10, 20 }, 0.2);
    AddLine(p, { 10, 20 }, { 0, 0 }, 0.2);
    const Profile profile = FitProfile(p, true, 0.05);
    CHECK(profile.segments.size() == 3);
    const auto c = FindConstraints({ profile }, SnapOptions{});
    CHECK(Count(c, ProfileConstraint::Kind::Horizontal) == 1);
    CHECK(Count(c, ProfileConstraint::Kind::Vertical) == 0);
    CHECK(Count(c, ProfileConstraint::Kind::Tangent) == 0);
}

TEST_CASE("a line meeting an arc at a corner isn't tangent")
{
    std::vector<Vec2> p;
    AddLine(p, { 0, 0 }, { 20, 0 }, 0.1);
    // A quarter arc starting 30 degrees off the line's direction.
    const double start = -kPi / 2 + kPi / 6;
    const Vec2 c{ 20 - 6 * std::cos(start), -6 * std::sin(start) };
    AddArc(p, c, 6, start, start + kPi / 2, 0.1);
    const Profile profile = FitProfile(p, false, 0.02);
    REQUIRE(profile.segments.size() == 2);
    CHECK(profile.segments[1].kind == ProfileSegment::Kind::Arc);
    const auto constraints = FindConstraints({ profile }, SnapOptions{});
    CHECK(Count(constraints, ProfileConstraint::Kind::Tangent) == 0);
}

TEST_CASE("circles with nearly the same centre are concentric, and radii round")
{
    std::vector<Vec2> outer, inner;
    AddArc(outer, { 0, 0 }, 20.03, 0, 2 * kPi, 0.2);
    AddArc(inner, { 0.02, -0.01 }, 7.48, 0, 2 * kPi, 0.2);
    const Profile a = FitProfile(outer, true, 0.05);
    const Profile b = FitProfile(inner, true, 0.05);
    REQUIRE(a.segments.size() == 1);
    REQUIRE(b.segments.size() == 1);
    SnapOptions options;
    auto c = FindConstraints({ a, b }, options);
    CHECK(Count(c, ProfileConstraint::Kind::Concentric) == 1);
    std::vector<double> radii;
    for (const auto& k : c)
        if (k.kind == ProfileConstraint::Kind::Radius)
            radii.push_back(k.value);
    REQUIRE(radii.size() == 2);
    CHECK(radii[0] == doctest::Approx(20.0));
    CHECK(radii[1] == doctest::Approx(7.5));

    options.concentric = false;
    options.roundTo = 0.0;
    c = FindConstraints({ a, b }, options);
    CHECK(Count(c, ProfileConstraint::Kind::Concentric) == 0);
    for (const auto& k : c)
        if (k.kind == ProfileConstraint::Kind::Radius && k.profile == 0)
            CHECK(k.value == doctest::Approx(20.03).epsilon(0.001));
}

TEST_CASE("a whole section of a box, with its reach either side")
{
    const Mesh box = BoxMesh(10, 20, 5);
    const SectionFrame frame = FrameForPlane({ 0, 0, 2 }, { 0, 0, 1 });
    CHECK(frame.xAxis.x == doctest::Approx(1.0));
    CHECK(frame.yAxis.y == doctest::Approx(1.0));
    const SectionResult result = SectionMesh(box, frame, 0.05, 0.01);
    REQUIRE(result.profiles.size() == 1);
    CHECK(result.profiles[0].segments.size() == 4);
    CHECK(result.reference[0].size() == 4);
    CHECK(result.behind == doctest::Approx(-7.0));
    CHECK(result.ahead == doctest::Approx(3.0));

    // X nearly along the normal: the frame takes Y instead.
    const SectionFrame side = FrameForPlane({}, { 1, 0, 0.1 });
    CHECK(std::fabs(Dot(side.xAxis, side.Normal())) < 1e-12);
    CHECK(side.xAxis.y == doctest::Approx(1.0));
}

TEST_CASE("rounding")
{
    CHECK(RoundTo(12.46, 0.1) == doctest::Approx(12.5));
    CHECK(RoundTo(12.444, 0.01) == doctest::Approx(12.44));
    CHECK(RoundTo(12.444, 0.0) == doctest::Approx(12.444));
}

TEST_CASE("the reference slice simplifies within its tolerance")
{
    std::vector<Vec2> p;
    AddArc(p, { 0, 0 }, 25, 0, 2 * kPi, 0.05);
    const auto s = SimplifyPolyline(p, true, 0.01);
    CHECK(s.size() < p.size() / 4);
    CHECK(s.size() > 10);
    // Every original point within 0.01 of the simplified loop.
    double worst = 0.0;
    for (const Vec2& q : p)
    {
        double best = 1e300;
        for (size_t k = 0; k < s.size(); ++k)
        {
            const Vec2 a = s[k], b = s[(k + 1) % s.size()];
            const double dx = b.x - a.x, dy = b.y - a.y;
            double t = ((q.x - a.x) * dx + (q.y - a.y) * dy) / (dx * dx + dy * dy);
            t = std::max(0.0, std::min(1.0, t));
            best = std::min(best, std::hypot(q.x - a.x - t * dx, q.y - a.y - t * dy));
        }
        worst = std::max(worst, best);
    }
    CHECK(worst <= 0.01 + 1e-12);

    std::vector<Vec2> line;
    AddLine(line, { 0, 0 }, { 10, 0 }, 0.1);
    line.push_back({ 10, 0 });
    const auto open = SimplifyPolyline(line, false, 0.01);
    CHECK(open.size() == 2);
}

// Set SCANTOALIBRE_STL to a scan, then run with -tc="section a real scan*" --no-skip.
TEST_CASE("section a real scan" * doctest::skip())
{
    char* value = nullptr;
    size_t length = 0;
    _dupenv_s(&value, &length, "SCANTOALIBRE_STL");
    REQUIRE(value != nullptr);
    const std::string path(value);
    free(value);
    Mesh mesh;
    ScanFileInfo info;
    std::string error;
    REQUIRE(ReadScanFile(path, mesh, info, error));
    const Box3 box = Bounds(mesh);
    const Point3 middle = (box.low + box.high) * 0.5;
    const Point3 normals[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    const Point3 xs[3] = { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } };
    for (int k = 0; k < 3; ++k)
    {
        const auto t0 = std::chrono::steady_clock::now();
        const auto slices = SliceMesh(mesh, middle, normals[k]);
        const auto t1 = std::chrono::steady_clock::now();
        SectionFrame frame;
        frame.origin = middle;
        frame.xAxis = xs[k];
        frame.yAxis = Cross(normals[k], xs[k]);
        std::vector<Profile> profiles;
        size_t points = 0, segments = 0, reference = 0;
        double worst = 0.0;
        for (const auto& slice : slices)
        {
            std::vector<Vec2> flat;
            for (const Point3& q : slice.points)
                flat.push_back(frame.ToPlane(q));
            points += flat.size();
            profiles.push_back(FitProfile(flat, slice.closed, 0.05));
            segments += profiles.back().segments.size();
            worst = std::max(worst, profiles.back().largestDeviation);
            reference += SimplifyPolyline(flat, slice.closed, 0.01).size();
        }
        const auto constraints = FindConstraints(profiles, SnapOptions{});
        const auto t2 = std::chrono::steady_clock::now();
        std::printf("axis %d: %zu polylines, %zu points, slice %.3f s; %zu segments (largest deviation %.4f), "
                    "%zu constraints, %zu reference points, fit %.3f s\n",
                    k, slices.size(), points, std::chrono::duration<double>(t1 - t0).count(), segments, worst,
                    constraints.size(), reference, std::chrono::duration<double>(t2 - t1).count());
    }
}

namespace
{
    // Points every step along a line, with noise across it.
    void AddLinePoints(std::vector<Vec2>& points, Vec2 a, Vec2 b, double step, double noise, unsigned& seed)
    {
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        const int n = std::max(1, static_cast<int>(length / step));
        for (int k = 0; k < n; ++k)
        {
            seed = seed * 1103515245u + 12345u;
            const double wobble = noise * (static_cast<double>((seed >> 8) % 2001) / 1000.0 - 1.0);
            const double t = static_cast<double>(k) / n;
            points.push_back({ a.x + (b.x - a.x) * t - (b.y - a.y) / length * wobble,
                               a.y + (b.y - a.y) * t + (b.x - a.x) / length * wobble });
        }
    }

    void AddArcPoints(std::vector<Vec2>& points, Vec2 centre, double radius, double from, double to, double step,
                      double noise, unsigned& seed)
    {
        const int n = std::max(1, static_cast<int>(std::fabs(to - from) * radius / step));
        for (int k = 0; k < n; ++k)
        {
            seed = seed * 1103515245u + 12345u;
            const double wobble = noise * (static_cast<double>((seed >> 8) % 2001) / 1000.0 - 1.0);
            const double a = from + (to - from) * k / n;
            points.push_back({ centre.x + (radius + wobble) * std::cos(a), centre.y + (radius + wobble) * std::sin(a) });
        }
    }

    SlicePolyline Slice(const std::vector<Vec2>& points)
    {
        SlicePolyline slice;
        slice.closed = true;
        for (const Vec2& p : points)
            slice.points.push_back({ p.x, p.y, 0.0 });
        return slice;
    }
}

TEST_CASE("the symmetric intent makes a noisy section exactly symmetric")
{
    const double kPi = 3.14159265358979323846;
    unsigned seed = 7;
    // An outline a little off symmetric about x = 0: a bottom across the axis,
    // two sides, and an arc over the top across the axis, the right side 0.02 out.
    std::vector<Vec2> outline;
    const double r = std::sqrt(20.0 * 20.0 + 10.0 * 10.0);
    AddLinePoints(outline, { -20, 0 }, { 20.02, 0 }, 0.2, 0.01, seed);
    AddLinePoints(outline, { 20.02, 0 }, { 20.02, 10 }, 0.2, 0.01, seed);
    AddArcPoints(outline, { 0, 0 }, r, std::atan2(10.0, 20.0), kPi - std::atan2(10.0, 20.0), 0.2, 0.01, seed);
    AddLinePoints(outline, { -20, 10 }, { -20, 0 }, 0.2, 0.01, seed);
    // Two holes, mirrored, a little off; and a slot on one side only.
    std::vector<Vec2> left, right, slot;
    AddArcPoints(left, { -8.02, 5 }, 2.01, 0.0, -2.0 * kPi, 0.1, 0.005, seed);
    AddArcPoints(right, { 7.99, 5.01 }, 1.99, 0.0, -2.0 * kPi, 0.1, 0.005, seed);
    AddLinePoints(slot, { 12, 3 }, { 15, 3 }, 0.1, 0.0, seed);
    AddLinePoints(slot, { 15, 3 }, { 15, 6 }, 0.1, 0.0, seed);
    AddLinePoints(slot, { 15, 6 }, { 12, 3 }, 0.1, 0.0, seed);

    SectionResult result;
    for (const auto* loop : { &outline, &left, &right, &slot })
    {
        result.slices.push_back(Slice(*loop));
        result.profiles.push_back(FitProfile(*loop, true, 0.05));
    }
    const SectionFrame frame;           // the world's XY
    SnapOptions options;
    options.symmetric = true;
    options.haveSymmetryLine = true;
    options.symmetryPoint = { 0, 0 };
    options.symmetryDirection = { 0, 1 };
    const Profile slotBefore = result.profiles[3];
    const std::vector<ProfileConstraint> constraints = ApplyDesignIntent(result, frame, 0.05, options);
    CHECK(result.symmetricSplit == 1);
    CHECK(result.symmetricPaired == 1);

    // Every vertex of the outline has its exact mirror among them, or is on the axis.
    const Profile& o = result.profiles[0];
    REQUIRE(o.closed);
    size_t lines = 0, arcs = 0;
    for (const ProfileSegment& seg : o.segments)
    {
        (seg.kind == ProfileSegment::Kind::Line ? lines : arcs)++;
        bool found = std::fabs(seg.start.x) < 1e-9;
        for (const ProfileSegment& other : o.segments)
            found = found || (std::fabs(seg.start.x + other.start.x) < 1e-9 && std::fabs(seg.start.y - other.start.y) < 1e-9);
        CHECK(found);
    }
    // The bottom and the top each one segment across the axis: four in all.
    CHECK(lines == 3);
    CHECK(arcs == 1);
    for (size_t s = 0; s < o.segments.size(); ++s)
    {
        const Vec2 a = o.segments[s].end, b = o.segments[(s + 1) % o.segments.size()].start;
        CHECK(std::hypot(a.x - b.x, a.y - b.y) < 1e-9);
    }
    // The holes mirror each other; the slot is left as it was.
    REQUIRE(result.profiles[1].segments.size() == 1);
    REQUIRE(result.profiles[2].segments.size() == 1);
    CHECK(result.profiles[1].segments[0].centre.x == doctest::Approx(-result.profiles[2].segments[0].centre.x).epsilon(1e-12));
    CHECK(result.profiles[1].segments[0].centre.y == doctest::Approx(result.profiles[2].segments[0].centre.y).epsilon(1e-12));
    CHECK(result.profiles[1].segments[0].radius == result.profiles[2].segments[0].radius);
    CHECK(result.profiles[3].segments.size() == slotBefore.segments.size());

    // Symmetric constraints: the outline's two pairs of corners, and the holes' centres.
    size_t symmetric = 0;
    for (const ProfileConstraint& c : constraints)
    {
        if (c.kind != ProfileConstraint::Kind::Symmetric)
            continue;
        ++symmetric;
        const ProfileSegment& a = result.profiles[c.profile].segments[c.segment];
        const ProfileSegment& b = result.profiles[c.otherProfile].segments[c.otherSegment];
        const Vec2 pa = c.point == 2 ? a.centre : a.start;
        const Vec2 pb = c.otherPoint == 2 ? b.centre : b.start;
        CHECK(std::fabs(pa.x + pb.x) < 1e-9);
        CHECK(std::fabs(pa.y - pb.y) < 1e-9);
    }
    CHECK(symmetric == 3);

    // With the option off, nothing changes.
    SectionResult untouched;
    untouched.slices.push_back(Slice(outline));
    untouched.profiles.push_back(FitProfile(outline, true, 0.05));
    const size_t before = untouched.profiles[0].segments.size();
    options.symmetric = false;
    const std::vector<ProfileConstraint> none = ApplyDesignIntent(untouched, frame, 0.05, options);
    for (const ProfileConstraint& c : none)
        CHECK(c.kind != ProfileConstraint::Kind::Symmetric);
    CHECK(untouched.profiles[0].segments.size() == before);
}

namespace
{
    // How far a point is from a profile's segments.
    double DistanceToProfile(const Vec2& p, const Profile& profile)
    {
        const double kPi = 3.14159265358979323846;
        double best = 1e300;
        for (const ProfileSegment& s : profile.segments)
        {
            double d = 1e300;
            if (s.kind == ProfileSegment::Kind::Line)
            {
                const double dx = s.end.x - s.start.x, dy = s.end.y - s.start.y;
                const double l2 = dx * dx + dy * dy;
                const double t = l2 > 0.0 ? std::clamp(((p.x - s.start.x) * dx + (p.y - s.start.y) * dy) / l2, 0.0, 1.0) : 0.0;
                d = std::hypot(p.x - s.start.x - dx * t, p.y - s.start.y - dy * t);
            }
            else
            {
                const double r = std::hypot(p.x - s.centre.x, p.y - s.centre.y);
                bool inside = s.kind == ProfileSegment::Kind::Circle;
                if (!inside)
                {
                    const double a0 = std::atan2(s.start.y - s.centre.y, s.start.x - s.centre.x);
                    const double a1 = std::atan2(s.end.y - s.centre.y, s.end.x - s.centre.x);
                    const double ap = std::atan2(p.y - s.centre.y, p.x - s.centre.x);
                    auto ccw = [&](double from, double to) { double v = to - from; while (v < 0) v += 2 * kPi; while (v >= 2 * kPi) v -= 2 * kPi; return v; };
                    inside = s.anticlockwise ? ccw(a0, ap) <= ccw(a0, a1) : ccw(ap, a0) <= ccw(a1, a0);
                }
                d = inside ? std::fabs(r - s.radius)
                           : std::min(std::hypot(p.x - s.start.x, p.y - s.start.y), std::hypot(p.x - s.end.x, p.y - s.end.y));
            }
            best = std::min(best, d);
        }
        return best;
    }
}

// Skipped unless asked for: set SCANTOALIBRE_STL to a symmetric scan and run
//   ScanCoreTests.exe -tc="symmetric sections of a real scan*" --no-skip
// Find Symmetry, its plane aligned onto YZ, then a section on XY and a
// silhouette along Y, each with Symmetric on, as Frank tried in Alibre.
TEST_CASE("symmetric sections of a real scan" * doctest::skip())
{
    char* value = nullptr;
    size_t length = 0;
    REQUIRE(_dupenv_s(&value, &length, "SCANTOALIBRE_STL") == 0);
    REQUIRE(value != nullptr);
    const std::string path(value);
    free(value);
    Mesh mesh;
    ScanFileInfo info;
    std::string message;
    REQUIRE(ReadScanFile(path, mesh, info, message));
    const OrientedBox box = FitOrientedBox(mesh);
    const std::vector<SymmetryCandidate> found = FindSymmetryPlanes(mesh, box, {});
    REQUIRE_FALSE(found.empty());
    Feature target;
    target.kind = Feature::Kind::Plane;
    target.direction = { 1, 0, 0 };
    AlignState state;
    Transform move;
    REQUIRE(AlignStep(found.front().plane, target, false, AxisToPlane::Square, state, move, message));
    TransformMesh(mesh, move);
    const Box3 b = Bounds(mesh);
    for (int view = 0; view < 2; ++view)
    {
        const SectionFrame frame = view == 0 ? FrameForPlane({ 0, 0, 0.5 * (b.low.z + b.high.z) }, { 0, 0, 1 })
                                             : FrameForPlane({ 0, 0, 0 }, { 0, 1, 0 });
        SectionResult r = view == 0 ? SectionMesh(mesh, frame, 0.05, 0.01) : SilhouetteMesh(mesh, frame, 0.05, 0.01, 1.0);
        SnapOptions options;
        options.symmetric = true;
        options.haveSymmetryLine = true;
        // x = 0 in the frame.
        options.symmetryPoint = frame.ToPlane({ 0, frame.origin.y, frame.origin.z });
        const Point3 along = Normalised(Cross({ 1, 0, 0 }, frame.Normal()));
        options.symmetryDirection = { Dot(along, frame.xAxis), Dot(along, frame.yAxis) };
        size_t before = 0;
        for (const Profile& p : r.profiles)
            before += p.segments.size();
        if (view == 0)
            for (const ProfileSegment& seg : r.profiles[0].segments)
                MESSAGE("as fitted: " << (seg.kind == ProfileSegment::Kind::Line ? "line" : "arc") << " (" << seg.start.x << ", "
                        << seg.start.y << ") to (" << seg.end.x << ", " << seg.end.y << ") centre (" << seg.centre.x << ", "
                        << seg.centre.y << ") r " << seg.radius << " fit r " << seg.fitRadius << " fit centre ("
                        << seg.fitCentre.x << ", " << seg.fitCentre.y << ")" << (seg.anticlockwise ? " ccw" : " cw"));
        double worstBefore = 0.0;
        for (size_t i = 0; i < r.slices.size(); ++i)
            for (const Point3& q : r.slices[i].points)
                worstBefore = std::max(worstBefore, DistanceToProfile(frame.ToPlane(q), r.profiles[i]));
        const std::vector<ProfileConstraint> constraints = ApplyDesignIntent(r, frame, 0.05, options);
        double worst = 0.0, sum = 0.0;
        size_t count = 0;
        for (size_t i = 0; i < r.slices.size(); ++i)
            for (const Point3& q : r.slices[i].points)
            {
                const double d = DistanceToProfile(frame.ToPlane(q), r.profiles[i]);
                worst = std::max(worst, d);
                sum += d * d;
                ++count;
            }
        MESSAGE("slice points from the profiles: before, worst " << worstBefore << "; after, worst " << worst
                << ", rms " << std::sqrt(sum / std::max<size_t>(count, 1)));
        if (view == 0)
            for (const ProfileSegment& seg : r.profiles[0].segments)
                MESSAGE((seg.kind == ProfileSegment::Kind::Line ? "line" : "arc") << " (" << seg.start.x << ", " << seg.start.y
                        << ") to (" << seg.end.x << ", " << seg.end.y << ") centre (" << seg.centre.x << ", " << seg.centre.y
                        << ") r " << seg.radius << (seg.anticlockwise ? " ccw" : " cw"));
        size_t after = 0, symmetric = 0;
        for (const Profile& p : r.profiles)
            after += p.segments.size();
        for (const ProfileConstraint& c : constraints)
            symmetric += c.kind == ProfileConstraint::Kind::Symmetric ? 1 : 0;
        MESSAGE(std::string(view == 0 ? "section" : "silhouette") << ": " << r.profiles.size() << " loops, " << r.symmetricSplit
                << " cut at the line, " << r.symmetricPaired << " pairs; " << before << " segments, then " << after
                << "; " << symmetric << " symmetric constraints; largest deviation " << r.largestDeviation);
    }
}

TEST_CASE("the symmetric intent joins a flat arc across the line and stays on the slice")
{
    // As in Frank's mt12cover section: the bottom is one arc of radius about 86
    // across the axis, its fitted centre a little off it, then sides and a top.
    const double kPi = 3.14159265358979323846;
    unsigned seed = 11;
    std::vector<Vec2> outline;
    const Vec2 centre{ 0.3, 61.0 };
    const double r = std::hypot(12.0 - centre.x, -24.0 - centre.y);
    const double a0 = std::atan2(-24.0 - centre.y, -12.0 - centre.x);
    const double a1 = std::atan2(-24.0 - centre.y, 12.0 - centre.x);
    AddArcPoints(outline, centre, r, a0, a1, 0.05, 0.01, seed);
    AddLinePoints(outline, { 12.0, -24.0 }, { 12.0, -18.0 }, 0.05, 0.01, seed);
    AddLinePoints(outline, { 12.0, -18.0 }, { -12.0, -18.0 }, 0.05, 0.01, seed);
    AddLinePoints(outline, { -12.0, -18.0 }, { -12.0, -24.0 }, 0.05, 0.01, seed);
    SectionResult result;
    result.slices.push_back(Slice(outline));
    result.profiles.push_back(FitProfile(outline, true, 0.05));
    SnapOptions options;
    options.symmetric = true;
    options.haveSymmetryLine = true;
    options.symmetryPoint = { 0, 0 };
    options.symmetryDirection = { 0, 1 };
    ApplyDesignIntent(result, SectionFrame(), 0.05, options);

    size_t arcs = 0;
    for (const ProfileSegment& seg : result.profiles[0].segments)
        if (seg.kind == ProfileSegment::Kind::Arc)
        {
            ++arcs;
            CHECK(std::fabs(seg.centre.x) < 1e-9);
        }
    CHECK(arcs == 1);
    double worst = 0.0;
    for (const Vec2& p : outline)
        worst = std::max(worst, DistanceToProfile(p, result.profiles[0]));
    MESSAGE("worst " << worst << " mm from the slice; sides " << result.symmetricGap << " mm apart");
    CHECK(worst < 0.06);
}

TEST_CASE("a radius dimension is the drawn arc's radius, not the free fit's")
{
    // Alibre's radial dimension keeps an arc's centre and sweep and scales it,
    // so a value off the drawn arc moves it: Frank's mt12 section bottom arc,
    // drawn at 98.86 with a free fit of 100.66, moved its ends 1.8 mm.
    Profile profile;
    ProfileSegment arc;
    arc.kind = ProfileSegment::Kind::Arc;
    arc.start = { -12.1941, -24.3247 };
    arc.end = { 11.4381, -24.4215 };
    arc.centre = { 0.0241, 73.7807 };
    arc.radius = 98.8634;
    arc.fitCentre = { 0.0320, 75.5843 };
    arc.fitRadius = 100.664;
    profile.segments.push_back(arc);
    SnapOptions options;
    options.roundTo = 0.1;
    bool found = false;
    for (const ProfileConstraint& c : FindConstraints({ profile }, options))
        if (c.kind == ProfileConstraint::Kind::Radius)
        {
            found = true;
            CHECK(c.value == doctest::Approx(98.9));
        }
    CHECK(found);
}

TEST_CASE("lines only: no arcs or circles, still within the tolerance")
{
    // A slot: two straight sides and round ends, and a lone circle.
    const double kPi = 3.14159265358979323846;
    unsigned seed = 5;
    std::vector<Vec2> slot, circle;
    AddLinePoints(slot, { -10, -3 }, { 10, -3 }, 0.1, 0.005, seed);
    AddArcPoints(slot, { 10, 0 }, 3.0, -kPi / 2, kPi / 2, 0.1, 0.005, seed);
    AddLinePoints(slot, { 10, 3 }, { -10, 3 }, 0.1, 0.005, seed);
    AddArcPoints(slot, { -10, 0 }, 3.0, kPi / 2, 3 * kPi / 2, 0.1, 0.005, seed);
    AddArcPoints(circle, { 0, 20 }, 4.0, 0.0, 2 * kPi, 0.1, 0.005, seed);
    for (const auto* loop : { &slot, &circle })
    {
        const Profile withArcs = FitProfile(*loop, true, 0.05);
        const Profile linesOnly = FitProfile(*loop, true, 0.05, false);
        size_t rounds = 0;
        for (const ProfileSegment& seg : withArcs.segments)
            rounds += seg.kind != ProfileSegment::Kind::Line ? 1 : 0;
        CHECK(rounds > 0);
        for (const ProfileSegment& seg : linesOnly.segments)
            CHECK(seg.kind == ProfileSegment::Kind::Line);
        CHECK(linesOnly.segments.size() > withArcs.segments.size());
        double worst = 0.0;
        for (const Vec2& p : *loop)
            worst = std::max(worst, DistanceToProfile(p, linesOnly));
        CHECK(worst <= 0.06);
    }
}
