// SectionTests.cpp : slicing a scan, fitting lines and arcs, design intent.

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ScanFile.h"

#include "Section.h"

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
