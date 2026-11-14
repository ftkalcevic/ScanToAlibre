// SectionSeriesTests.cpp : a series of sections matched for lofting.

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "ScanFile.h"
#include "SectionSeries.h"

using namespace scancore;

namespace
{
    const double kPi = 3.14159265358979323846;

    // Rings of n points (ring(r, k) gives point k of ring r), joined side by
    // side, with flat caps at both ends when capped.
    Mesh RingMesh(int rings, int n, const std::function<Point3(int, int)>& ring, bool capped)
    {
        Mesh mesh;
        for (int r = 0; r < rings; ++r)
            for (int k = 0; k < n; ++k)
                mesh.vertices.push_back(ring(r, k));
        for (int r = 0; r + 1 < rings; ++r)
            for (int k = 0; k < n; ++k)
            {
                const int a = r * n + k, b = r * n + (k + 1) % n, c = a + n, d = b + n;
                mesh.triangles.push_back({ a, b, d });
                mesh.triangles.push_back({ a, d, c });
            }
        if (capped)
        {
            for (int k = 1; k + 1 < n; ++k)
            {
                mesh.triangles.push_back({ 0, k + 1, k });
                const int top = (rings - 1) * n;
                mesh.triangles.push_back({ top, top + k, top + k + 1 });
            }
        }
        return mesh;
    }

    // Points along each side of a polygon ring, as a scan has: corner(r, c)
    // gives corner c of ring r, and point k is kSide points along from corner k / kSide.
    const int kSide = 20;
    Point3 AlongSides(const std::function<Point3(int, int)>& corner, int r, int k, int corners)
    {
        const int c = k / kSide;
        const double f = static_cast<double>(k % kSide) / kSide;
        const Point3 a = corner(r, c), b = corner(r, (c + 1) % corners);
        return a + (b - a) * f;
    }

    SectionFrame BaseXY()
    {
        return FrameForPlane({ 0, 0, 0 }, { 0, 0, 1 });
    }

    double DistanceToLine(const Point3& p, const Point3& a, const Point3& b)
    {
        const Point3 d = Normalised(b - a);
        const Point3 v = p - a;
        return Length(v - d * Dot(v, d));
    }
}

TEST_CASE("offsets must go up")
{
    std::string message;
    CHECK(ValidateOffsets({ -5.0, 0.0, 2.5 }, message));
    CHECK_FALSE(ValidateOffsets({}, message));
    CHECK_FALSE(ValidateOffsets({ 1.0, 1.0 }, message));
    CHECK_FALSE(ValidateOffsets({ 1.0, 3.0, 2.0 }, message));
    CHECK(message.find("plane 3") != std::string::npos);
    const std::vector<double> even = EvenOffsets(-2.0, 7.0, 4);
    REQUIRE(even.size() == 4);
    CHECK(even[0] == doctest::Approx(-2.0));
    CHECK(even[3] == doctest::Approx(7.0));
    CHECK(even[1] == doctest::Approx(1.0));
}

TEST_CASE("a closed spline goes through its points and follows a circle")
{
    std::vector<Vec2> points;
    for (int k = 0; k < 16; ++k)
        points.push_back({ 10.0 * std::cos(2.0 * kPi * k / 16), 10.0 * std::sin(2.0 * kPi * k / 16) });
    const std::vector<Vec2> curve = ClosedSplineCurve(points, 8);
    REQUIRE(curve.size() == 128);
    for (int k = 0; k < 16; ++k)
    {
        CHECK(curve[k * 8].x == doctest::Approx(points[k].x).epsilon(1e-9));
        CHECK(curve[k * 8].y == doctest::Approx(points[k].y).epsilon(1e-9));
    }
    double worst = 0.0;
    for (const Vec2& p : curve)
        worst = std::max(worst, std::fabs(std::hypot(p.x, p.y) - 10.0));
    CHECK(worst < 0.01);
}

TEST_CASE("a tapered box: splines matched at its corners, guides along its edges")
{
    // A square 20 across at the bottom, 12 across and turned 20 degrees at the top.
    auto corner = [](int r, int k) {
        const double half = r == 0 ? 10.0 : 6.0;
        const double turn = r == 0 ? 0.0 : 20.0 * kPi / 180.0;
        const double a = turn + kPi / 4.0 + k * kPi / 2.0;
        return Point3{ half * std::sqrt(2.0) * std::cos(a), half * std::sqrt(2.0) * std::sin(a), r == 0 ? 0.0 : 30.0 };
    };
    const Mesh mesh = RingMesh(2, 4 * kSide, [&](int r, int k) { return AlongSides(corner, r, k, 4); }, true);
    SeriesOptions options;
    options.tolerance = 0.05;
    const SeriesResult result = SliceSeries(mesh, BaseXY(), EvenOffsets(2.0, 28.0, 10), options);
    REQUIRE(result.slices.size() == 10);
    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].loops == 1);
    REQUIRE(result.chains.size() == 1);
    const SeriesChain& chain = result.chains[0];
    CHECK(chain.splines);
    CHECK(chain.corners == 4);
    REQUIRE(chain.spline.size() == 10);
    for (const auto& points : chain.spline)
        CHECK(points.size() == chain.spline[0].size());
    REQUIRE(chain.guides.size() == 4);
    for (const auto& guide : chain.guides)
    {
        REQUIRE(guide.size() == 10);
        // Along one of the four side edges.
        double best = 1e300;
        for (int k = 0; k < 4; ++k)
        {
            double worst = 0.0;
            for (const Point3& p : guide)
                worst = std::max(worst, DistanceToLine(p, corner(0, k), corner(1, k)));
            best = std::min(best, worst);
        }
        CHECK(best < 0.01);
    }
    // Lines and arcs on the same taper not turned (its sides flat; turned, they're
    // twisted and slice into curves): four lines a slice, started at the same corner.
    auto straight = [](int r, int k) {
        const double half = r == 0 ? 10.0 : 6.0;
        const double a = kPi / 4.0 + k * kPi / 2.0;
        return Point3{ half * std::sqrt(2.0) * std::cos(a), half * std::sqrt(2.0) * std::sin(a), r == 0 ? 0.0 : 30.0 };
    };
    const Mesh flat = RingMesh(2, 4 * kSide, [&](int r, int k) { return AlongSides(straight, r, k, 4); }, true);
    options.splines = false;
    const SeriesResult lines = SliceSeries(flat, BaseXY(), EvenOffsets(2.0, 28.0, 10), options);
    REQUIRE(lines.chains.size() == 1);
    CHECK_FALSE(lines.chains[0].splines);
    CHECK(lines.chains[0].corners == 4);
    for (const Profile& p : lines.chains[0].profiles)
        CHECK(p.segments.size() == 4);
    CHECK(lines.chains[0].guides.size() == 4);
}

TEST_CASE("an ellipse to a circle: no corners, starts on one straight seam")
{
    const int n = 96;
    auto ring = [&](int r, int k) {
        const double z = 30.0 * r / 10;
        const double a = 20.0 - 10.0 * z / 30.0, b = 10.0;
        const double t = 2.0 * kPi * k / n;
        return Point3{ a * std::cos(t), b * std::sin(t), z };
    };
    const Mesh mesh = RingMesh(11, n, ring, false);
    SeriesOptions options;
    options.tolerance = 0.05;
    const SeriesResult result = SliceSeries(mesh, BaseXY(), EvenOffsets(1.0, 29.0, 8), options);
    REQUIRE(result.runs.size() == 1);
    REQUIRE(result.chains.size() == 1);
    const SeriesChain& chain = result.chains[0];
    CHECK(chain.corners == 0);
    CHECK(chain.withinTolerance);
    REQUIRE(chain.spline.size() == 8);
    for (const auto& points : chain.spline)
    {
        REQUIRE(points.size() == chain.spline[0].size());
        // The first point on +X, as the first slice's.
        CHECK(std::fabs(std::atan2(points[0].y, points[0].x)) < 3.0 * kPi / 180.0);
    }
    CHECK(chain.guides.size() == 4);
}

TEST_CASE("lines and arcs with no common corner: an ellipse to a circle")
{
    // Fitted with different numbers of arcs from slice to slice, the last a
    // circle; matched by splitting where a slice has no join.
    const int n = 96;
    auto ring = [&](int r, int k) {
        const double z = 30.0 * r / 10;
        const double a = 20.0 - 10.0 * z / 30.0, b = 10.0;
        const double t = 2.0 * kPi * k / n;
        return Point3{ a * std::cos(t), b * std::sin(t), z };
    };
    const Mesh mesh = RingMesh(11, n, ring, false);
    SeriesOptions options;
    options.splines = false;
    options.tolerance = 0.05;
    const SeriesResult result = SliceSeries(mesh, BaseXY(), EvenOffsets(1.0, 29.9, 8), options);
    REQUIRE(result.chains.size() == 1);
    const SeriesChain& chain = result.chains[0];
    REQUIRE_FALSE(chain.splines);
    CHECK(result.notes.empty());
    REQUIRE(chain.profiles.size() == 8);
    const size_t count = chain.profiles[0].segments.size();
    CHECK(count >= 2);
    for (const Profile& p : chain.profiles)
    {
        REQUIRE(p.segments.size() == count);
        for (size_t k = 0; k < count; ++k)
        {
            const Vec2 a = p.segments[k].end, b = p.segments[(k + 1) % count].start;
            CHECK(std::hypot(a.x - b.x, a.y - b.y) < 1e-6);
        }
    }
    CHECK(chain.largestDeviation <= options.tolerance + 1e-9);
    // Segment k starts at about the same angle in every slice. Joins match by how
    // far round they are, and the same share of the way round an ellipse twice as
    // long as it's wide and of a circle lie up to about 17 degrees apart.
    for (size_t k = 0; k < count; ++k)
    {
        const double first = std::atan2(chain.profiles[0].segments[k].start.y, chain.profiles[0].segments[k].start.x);
        for (const Profile& p : chain.profiles)
        {
            double apart = std::fabs(std::atan2(p.segments[k].start.y, p.segments[k].start.x) - first);
            apart = std::min(apart, 2.0 * kPi - apart);
            CHECK(apart < 20.0 * kPi / 180.0);
        }
    }
    CHECK(chain.guides.size() == 4);
}

TEST_CASE("outlines that don't overlap are found")
{
    auto square = [](double x, double y, double half) {
        return std::vector<Vec2>{ { x - half, y - half }, { x + half, y - half }, { x + half, y + half },
                                  { x - half, y + half } };
    };
    CHECK(OutlinesOverlap(square(0, 0, 5), square(3, 3, 5)));
    CHECK(OutlinesOverlap(square(0, 0, 5), square(0, 0, 1)));      // one inside the other
    CHECK_FALSE(OutlinesOverlap(square(0, 0, 5), square(0, 12, 5)));
    // A thin cross: no corner of either inside the other, but the edges cross.
    const std::vector<Vec2> wide{ { -10, -1 }, { 10, -1 }, { 10, 1 }, { -10, 1 } };
    const std::vector<Vec2> tall{ { -1, -10 }, { 1, -10 }, { 1, 10 }, { -1, 10 } };
    CHECK(OutlinesOverlap(wide, tall));

    // A tube that jumps sideways between two of its slices: the series is one
    // run (the loops still pair), but the jump is reported.
    const int n = 48;
    Mesh mesh = RingMesh(2, n, [&](int r, int k) {
        const double t = 2.0 * kPi * k / n;
        return Point3{ 10.0 * std::cos(t), 10.0 * std::sin(t), r == 0 ? 0.0 : 10.0 };
    }, true);
    const Mesh moved = RingMesh(2, n, [&](int r, int k) {
        const double t = 2.0 * kPi * k / n;
        return Point3{ 4.0 * std::cos(t), 13.0 + 3.0 * std::sin(t), r == 0 ? 10.0 : 20.0 };
    }, true);
    const int base = static_cast<int>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), moved.vertices.begin(), moved.vertices.end());
    for (const auto& t : moved.triangles)
        mesh.triangles.push_back({ t[0] + base, t[1] + base, t[2] + base });
    SeriesOptions options;
    const SeriesResult result = SliceSeries(mesh, BaseXY(), { 2.0, 6.0, 14.0, 18.0 }, options);
    REQUIRE(result.runs.size() == 1);
    REQUIRE(result.gaps.size() == 1);
    CHECK(result.gaps[0].first == 1);
    CHECK(result.gaps[0].second == 2);
    CHECK(result.gaps[0].secondArea < result.gaps[0].firstArea);
}

TEST_CASE("loops appearing split the series into runs")
{
    // A tube along the whole length, and a second beside it from 10 to 20.
    const int n = 48;
    Mesh mesh = RingMesh(2, n, [&](int r, int k) {
        const double t = 2.0 * kPi * k / n;
        return Point3{ 10.0 * std::cos(t), 10.0 * std::sin(t), r == 0 ? 0.0 : 30.0 };
    }, true);
    const Mesh boss = RingMesh(2, n, [&](int r, int k) {
        const double t = 2.0 * kPi * k / n;
        return Point3{ 30.0 + 5.0 * std::cos(t), 5.0 * std::sin(t), r == 0 ? 10.0 : 20.0 };
    }, true);
    const int base = static_cast<int>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), boss.vertices.begin(), boss.vertices.end());
    for (const auto& t : boss.triangles)
        mesh.triangles.push_back({ t[0] + base, t[1] + base, t[2] + base });
    SeriesOptions options;
    const SeriesResult result = SliceSeries(mesh, BaseXY(), EvenOffsets(1.0, 29.0, 15), options);
    REQUIRE(result.runs.size() == 3);
    CHECK(result.runs[0].loops == 1);
    CHECK(result.runs[1].loops == 2);
    CHECK(result.runs[2].loops == 1);
    CHECK(result.chains.size() == 4);
    CHECK(DescribeRuns(result).find("2 loops") != std::string::npos);
}

TEST_CASE("lines and arcs: a roof on a box is matched by splitting")
{
    // A box 20 x 20 from 0 to 15, and on it a house shape (the box with a
    // gable on its top side) from 15 to 30.
    auto box = [](int r, int k) {
        const double z = r * 15.0;
        const Point3 points[4] = { { -10, -10, z }, { 10, -10, z }, { 10, 10, z }, { -10, 10, z } };
        return points[k];
    };
    auto house = [](int r, int k) {
        const double z = 15.0 + r * 15.0;
        const Point3 points[5] = { { -10, -10, z }, { 10, -10, z }, { 10, 10, z }, { 0, 18, z }, { -10, 10, z } };
        return points[k];
    };
    Mesh mesh = RingMesh(2, 4 * kSide, [&](int r, int k) { return AlongSides(box, r, k, 4); }, true);
    const Mesh top = RingMesh(2, 5 * kSide, [&](int r, int k) { return AlongSides(house, r, k, 5); }, true);
    const int base = static_cast<int>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), top.vertices.begin(), top.vertices.end());
    for (const auto& t : top.triangles)
        mesh.triangles.push_back({ t[0] + base, t[1] + base, t[2] + base });
    SeriesOptions options;
    options.splines = false;
    options.tolerance = 0.05;
    const SeriesResult result = SliceSeries(mesh, BaseXY(), EvenOffsets(1.0, 29.0, 8), options);
    REQUIRE(result.chains.size() == 1);
    const SeriesChain& chain = result.chains[0];
    REQUIRE_FALSE(chain.splines);
    CHECK(chain.corners >= 3);
    REQUIRE(chain.profiles.size() == 8);
    for (const Profile& p : chain.profiles)
        CHECK(p.segments.size() == chain.profiles.back().segments.size());
    CHECK(chain.profiles.back().segments.size() == 5);
    CHECK(chain.largestDeviation <= options.tolerance + 1e-9);
    // Each slice's segments join end to end, and segment k starts near segment k
    // of the next slice (matched in order).
    for (const Profile& p : chain.profiles)
        for (size_t k = 0; k < p.segments.size(); ++k)
        {
            const Vec2 a = p.segments[k].end, b = p.segments[(k + 1) % p.segments.size()].start;
            CHECK(std::hypot(a.x - b.x, a.y - b.y) < 1e-6);
        }
    for (size_t s = 0; s + 1 < chain.profiles.size(); ++s)
    {
        const Vec2 a = chain.profiles[s].segments[0].start, b = chain.profiles[s + 1].segments[0].start;
        CHECK(std::hypot(a.x - b.x, a.y - b.y) < 1.0);
    }
}

TEST_CASE("a profile moved into a sketch's axes, turned and mirrored")
{
    SectionFrame from = BaseXY();
    from.origin = { 1, 2, 5 };
    Profile profile;
    ProfileSegment arc;
    arc.kind = ProfileSegment::Kind::Arc;
    arc.centre = { 0, 0 };
    arc.radius = 5;
    arc.start = { 5, 0 };
    arc.end = { 0, 5 };
    arc.anticlockwise = true;
    profile.segments.push_back(arc);
    // The same plane seen from below, its axes turned.
    SectionFrame to;
    to.origin = { 3, -1, 5 };
    to.xAxis = { 0, 1, 0 };
    to.yAxis = { 1, 0, 0 };     // normal -Z
    const Profile moved = MapProfile(profile, from, to);
    const ProfileSegment& m = moved.segments[0];
    CHECK_FALSE(m.anticlockwise);
    // The start, (6, 2, 5) in the world, is (3, 3) in to's axes.
    CHECK(m.start.x == doctest::Approx(3.0));
    CHECK(m.start.y == doctest::Approx(3.0));
    const Point3 back = to.ToWorld(m.centre);
    CHECK(back.x == doctest::Approx(1.0));
    CHECK(back.y == doctest::Approx(2.0));
    // Clockwise from start to end in to's axes: the cross product of the radii is negative.
    const double cross = (m.start.x - m.centre.x) * (m.end.y - m.centre.y) - (m.start.y - m.centre.y) * (m.end.x - m.centre.x);
    CHECK(cross < 0.0);
}

TEST_CASE("section series of a real scan" * doctest::skip())
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
    const Point3 size = box.Size();
    // Along the longest axis.
    Point3 normal{ 1, 0, 0 };
    if (size.y >= size.x && size.y >= size.z)
        normal = { 0, 1, 0 };
    else if (size.z >= size.x && size.z >= size.y)
        normal = { 0, 0, 1 };
    // SCANTOALIBRE_AXIS=x, y or z: that axis instead of the longest.
    char* axis = nullptr;
    _dupenv_s(&axis, &length, "SCANTOALIBRE_AXIS");
    if (axis != nullptr)
    {
        normal = axis[0] == 'x' ? Point3{ 1, 0, 0 } : axis[0] == 'y' ? Point3{ 0, 1, 0 } : Point3{ 0, 0, 1 };
        free(axis);
    }
    const SectionFrame base = FrameForPlane(box.Centre(), normal);
    double first = 0.0, last = 0.0;
    DefaultSeriesRange(mesh, base, first, last);
    for (int splines = 1; splines >= 0; --splines)
    {
        SeriesOptions options;
        options.splines = splines != 0;
        const SeriesResult result = SliceSeries(mesh, base, EvenOffsets(first, last, 10), options);
        std::printf("%s: %s; %zu chains, %zu open, %.2f s\n", splines ? "splines" : "lines and arcs",
                    DescribeRuns(result).c_str(), result.chains.size(), result.openPolylines, result.seconds);
        for (const SeriesChain& chain : result.chains)
            std::printf("  run %zu: %s, %zu corners, %zu points, %zu segments, %zu guides, %.3f mm\n", chain.run + 1,
                        chain.splines ? "splines" : "lines and arcs", chain.corners,
                        chain.spline.empty() ? size_t{ 0 } : chain.spline[0].size(),
                        chain.profiles.empty() ? size_t{ 0 } : chain.profiles[0].segments.size(), chain.guides.size(),
                        chain.largestDeviation);
        for (const std::string& note : result.notes)
            std::printf("  %s\n", note.c_str());
        // Each slice's shortest segment, which must never be next to nothing.
        for (const SeriesChain& chain : result.chains)
            for (size_t s = 0; s < chain.profiles.size(); ++s)
            {
                double shortest = 1e300;
                size_t at = 0;
                const auto& segs = chain.profiles[s].segments;
                for (size_t k = 0; k < segs.size(); ++k)
                {
                    const double d = std::hypot(segs[k].end.x - segs[k].start.x, segs[k].end.y - segs[k].start.y);
                    if (d < shortest)
                    {
                        shortest = d;
                        at = k;
                    }
                }
                std::printf("  slice %zu: shortest chord %.6f mm, segment %zu of %zu (%s)\n", s + 1, shortest, at,
                            segs.size(), segs[at].kind == ProfileSegment::Kind::Line ? "line" : "arc");
            }
    }
}
