// SilhouetteTests.cpp : the scan's outline seen along a plane's normal.

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>

#include "Orient.h"
#include "ScanFile.h"
#include "Section.h"
#include "Silhouette.h"

using namespace scancore;

namespace
{
    const double kPi = 3.14159265358979323846;

    // A closed box from low to high, each face two triangles, facing out.
    void AddBox(Mesh& mesh, const Point3& low, const Point3& high)
    {
        const int base = static_cast<int>(mesh.vertices.size());
        for (int i = 0; i < 8; ++i)
            mesh.vertices.push_back({ (i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y, (i & 4) ? high.z : low.z });
        const int quads[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 },
                                  { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for (const auto& q : quads)
        {
            mesh.triangles.push_back({ base + q[0], base + q[1], base + q[2] });
            mesh.triangles.push_back({ base + q[0], base + q[2], base + q[3] });
        }
    }

    // An L-bracket: a base, an upright at one end, and a tab sticking out
    // sideways from the top of the upright.
    Mesh Bracket()
    {
        Mesh mesh;
        AddBox(mesh, { 0, 0, 0 }, { 60, 40, 5 });
        AddBox(mesh, { 0, 0, 0 }, { 5, 40, 40 });
        AddBox(mesh, { 0, 40, 30 }, { 5, 50, 40 });
        return mesh;
    }

    // A plate 50 x 50 x 5 with square through-holes, built of boxes around them.
    Mesh Plate(const std::vector<std::array<double, 4>>& holes)
    {
        std::vector<double> xs{ 0, 50 }, ys{ 0, 50 };
        for (const auto& h : holes)
        {
            xs.push_back(h[0]);
            xs.push_back(h[2]);
            ys.push_back(h[1]);
            ys.push_back(h[3]);
        }
        std::sort(xs.begin(), xs.end());
        std::sort(ys.begin(), ys.end());
        Mesh mesh;
        for (size_t i = 0; i + 1 < xs.size(); ++i)
            for (size_t j = 0; j + 1 < ys.size(); ++j)
            {
                const double mx = 0.5 * (xs[i] + xs[i + 1]), my = 0.5 * (ys[j] + ys[j + 1]);
                bool hole = false;
                for (const auto& h : holes)
                    hole = hole || (mx > h[0] && mx < h[2] && my > h[1] && my < h[3]);
                if (!hole && xs[i + 1] > xs[i] && ys[j + 1] > ys[j])
                    AddBox(mesh, { xs[i], ys[j], 0 }, { xs[i + 1], ys[j + 1], 5 });
            }
        return mesh;
    }

    Mesh Cylinder(double radius, double height, int segments)
    {
        Mesh mesh;
        mesh.vertices.push_back({ 0, 0, 0 });
        mesh.vertices.push_back({ 0, 0, height });
        for (int k = 0; k < segments; ++k)
        {
            const double a = 2.0 * kPi * k / segments;
            mesh.vertices.push_back({ radius * std::cos(a), radius * std::sin(a), 0 });
            mesh.vertices.push_back({ radius * std::cos(a), radius * std::sin(a), height });
        }
        for (int k = 0; k < segments; ++k)
        {
            const int b0 = 2 + 2 * k, t0 = b0 + 1;
            const int b1 = 2 + 2 * ((k + 1) % segments), t1 = b1 + 1;
            mesh.triangles.push_back({ 0, b1, b0 });
            mesh.triangles.push_back({ 1, t0, t1 });
            mesh.triangles.push_back({ b0, b1, t1 });
            mesh.triangles.push_back({ b0, t1, t0 });
        }
        return mesh;
    }

    double DistanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b)
    {
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
        return std::hypot(p.x - a.x - dx * t, p.y - a.y - dy * t);
    }

    double DistanceToPolygon(const Vec2& p, const std::vector<Vec2>& polygon)
    {
        double best = 1e300;
        for (size_t i = 0; i < polygon.size(); ++i)
            best = std::min(best, DistanceToSegment(p, polygon[i], polygon[(i + 1) % polygon.size()]));
        return best;
    }

    double Area(const std::vector<Vec2>& loop)
    {
        double area = 0.0;
        for (size_t i = 0; i < loop.size(); ++i)
            area += loop[i].x * loop[(i + 1) % loop.size()].y - loop[(i + 1) % loop.size()].x * loop[i].y;
        return 0.5 * area;
    }

    // The silhouette along normal has one loop matching the expected outline
    // (corners given in 3D, projected by the frame) within 0.05 mm.
    void CheckOutline(const Mesh& mesh, const Point3& normal, const std::vector<Point3>& corners)
    {
        const SectionFrame frame = FrameForPlane({ 0, 0, 0 }, normal);
        SilhouetteInfo info;
        const SectionResult r = SilhouetteMesh(mesh, frame, 0.05, 0.01, 1.0, &info);
        REQUIRE(r.slices.size() == 1);
        std::vector<Vec2> expected;
        for (const Point3& c : corners)
            expected.push_back(frame.ToPlane(c));
        std::vector<Vec2> loop;
        double worst = 0.0;
        for (const Point3& p : r.slices[0].points)
        {
            loop.push_back(frame.ToPlane(p));
            worst = std::max(worst, DistanceToPolygon(loop.back(), expected));
        }
        MESSAGE("normal (" << normal.x << ", " << normal.y << ", " << normal.z << "): " << loop.size() << " points, "
                           << info.snapped << " snapped, worst " << worst << " mm, fit within "
                           << r.largestDeviation << ", " << r.profiles[0].segments.size() << " segments");
        CHECK(worst <= 0.05);
        CHECK(std::fabs(Area(loop)) == doctest::Approx(std::fabs(Area(expected))).epsilon(0.002));
        CHECK(r.profiles[0].closed);
        CHECK(r.profiles[0].segments.size() == expected.size());
    }
}

TEST_CASE("the silhouette of an L-bracket along each axis, with the tab a slice misses")
{
    const Mesh mesh = Bracket();
    // Along Z: the base, and the tab sticking out past it.
    CheckOutline(mesh, { 0, 0, 1 },
                 { { 0, 0, 0 }, { 60, 0, 0 }, { 60, 40, 0 }, { 5, 40, 0 }, { 5, 50, 0 }, { 0, 50, 0 } });
    // Along Y: the L.
    CheckOutline(mesh, { 0, 1, 0 },
                 { { 0, 0, 0 }, { 60, 0, 0 }, { 60, 0, 5 }, { 5, 0, 5 }, { 5, 0, 40 }, { 0, 0, 40 } });
    // Along X: the upright, with the tab.
    CheckOutline(mesh, { 1, 0, 0 },
                 { { 0, 0, 0 }, { 0, 40, 0 }, { 0, 40, 30 }, { 0, 50, 30 }, { 0, 50, 40 }, { 0, 0, 40 } });

    // A slice through the base misses the tab.
    const SectionResult slice = SectionMesh(mesh, FrameForPlane({ 0, 0, 2.5 }, { 0, 0, 1 }), 0.05, 0.01);
    double highestY = -1e300;
    for (const SlicePolyline& s : slice.slices)
        for (const Point3& p : s.points)
            highestY = std::max(highestY, p.y);
    CHECK(highestY == doctest::Approx(40.0));
}

TEST_CASE("a through-hole is kept and a pinhole filled")
{
    const Mesh mesh = Plate({ { 10, 10, 15, 15 }, { 30, 30, 30.4, 30.4 } });
    SilhouetteInfo info;
    const SectionResult r =
        SilhouetteMesh(mesh, FrameForPlane({ 0, 0, 0 }, { 0, 0, 1 }), 0.05, 0.01, 1.0, &info);
    CHECK(info.pieces == 1);
    CHECK(info.holes == 1);
    CHECK(info.filledHoles == 1);
    REQUIRE(r.slices.size() == 2);
    std::vector<Vec2> hole;
    for (const Point3& p : r.slices[1].points)
        hole.push_back({ p.x, p.y });
    CHECK(Area(hole) == doctest::Approx(-25.0).epsilon(0.01));
    for (const Vec2& p : hole)
        CHECK(DistanceToPolygon(p, { { 10, 10 }, { 15, 10 }, { 15, 15 }, { 10, 15 } }) <= 0.05);

    // With a smaller smallest hole, the pinhole is kept too.
    const SectionResult fine = SilhouetteMesh(mesh, FrameForPlane({ 0, 0, 0 }, { 0, 0, 1 }), 0.05, 0.01, 0.2, &info);
    CHECK(info.holes == 2);
    CHECK(fine.slices.size() == 3);
}

TEST_CASE("two separate blocks give two loops")
{
    Mesh mesh;
    AddBox(mesh, { 0, 0, 0 }, { 10, 10, 10 });
    AddBox(mesh, { 20, 0, 3 }, { 30, 10, 8 });
    SilhouetteInfo info;
    const SectionResult r = SilhouetteMesh(mesh, FrameForPlane({ 0, 0, 0 }, { 0, 0, 1 }), 0.05, 0.01, 1.0, &info);
    CHECK(info.pieces == 2);
    CHECK(r.slices.size() == 2);
    CHECK(r.behind == doctest::Approx(0.0));
    CHECK(r.ahead == doctest::Approx(10.0));
}

TEST_CASE("a cylinder seen end on is one circle")
{
    const Mesh mesh = Cylinder(10.0, 20.0, 96);
    const SectionResult r = SilhouetteMesh(mesh, FrameForPlane({ 0, 0, 0 }, { 0, 0, 1 }), 0.05, 0.01, 1.0);
    REQUIRE(r.profiles.size() == 1);
    REQUIRE(r.profiles[0].segments.size() == 1);
    const ProfileSegment& circle = r.profiles[0].segments[0];
    CHECK(circle.kind == ProfileSegment::Kind::Circle);
    CHECK(circle.radius == doctest::Approx(10.0).epsilon(0.002));
    CHECK(std::hypot(circle.centre.x, circle.centre.y) < 0.02);
}

// Skipped unless asked for: set SCANTOALIBRE_STL to a scan (.stl or .ply) and run
//   ScanCoreTests.exe -tc="silhouette of a real scan*" --no-skip
TEST_CASE("silhouette of a real scan" * doctest::skip())
{
    char* value = nullptr;
    size_t length = 0;
    REQUIRE(_dupenv_s(&value, &length, "SCANTOALIBRE_STL") == 0);
    REQUIRE(value != nullptr);
    const std::string path(value);
    free(value);
    Mesh mesh;
    ScanFileInfo fileInfo;
    std::string message;
    REQUIRE(ReadScanFile(path, mesh, fileInfo, message));
    const OrientedBox box = FitOrientedBox(mesh);
    for (int k = 0; k < 3; ++k)
    {
        SilhouetteInfo info;
        const auto t0 = std::chrono::steady_clock::now();
        const SectionResult r = SilhouetteMesh(mesh, FrameForPlane(box.centre, box.axes[k]), 0.05, 0.01, 1.0, &info);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        size_t segments = 0;
        for (const Profile& p : r.profiles)
            segments += p.segments.size();
        MESSAGE("box axis " << k << ": " << info.pieces << " pieces, " << info.holes << " holes (" << info.filledHoles
                            << " filled, " << info.droppedPieces << " specks dropped), cell " << info.cell << ", "
                            << info.snapped << " of " << info.points << " points snapped, " << segments
                            << " segments within " << r.largestDeviation << " mm, " << seconds << " s");
    }
}
