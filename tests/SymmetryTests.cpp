// SymmetryTests.cpp : nearest points, symmetry planes and mid planes.

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

#include "Compare.h"
#include "Orient.h"
#include "ScanFile.h"
#include "Symmetry.h"

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

    // A lumpy closed blob with no symmetry: a sphere whose radius wanders.
    Mesh Blob()
    {
        Mesh mesh;
        const int rings = 40, segments = 80;
        auto radius = [](double theta, double phi) {
            return 20.0 * (1.0 + 0.15 * std::sin(2.0 * theta + 1.3) * std::cos(3.0 * phi + 0.7) +
                           0.12 * std::sin(3.0 * theta + 0.4) * std::sin(2.0 * phi + 2.1) +
                           0.10 * std::cos(theta + 0.9) * std::sin(phi + 0.2) +
                           0.08 * std::sin(5.0 * theta + 2.7) * std::cos(4.0 * phi + 1.1));
        };
        mesh.vertices.push_back({ 0, 0, radius(0, 0) });
        for (int i = 1; i < rings; ++i)
        {
            const double theta = kPi * i / rings;
            for (int j = 0; j < segments; ++j)
            {
                const double phi = 2.0 * kPi * j / segments;
                const double r = radius(theta, phi);
                mesh.vertices.push_back(
                    { r * std::sin(theta) * std::cos(phi), r * std::sin(theta) * std::sin(phi), r * std::cos(theta) });
            }
        }
        mesh.vertices.push_back({ 0, 0, -radius(kPi, 0) });
        const int bottom = static_cast<int>(mesh.vertices.size()) - 1;
        auto at = [&](int i, int j) { return 1 + (i - 1) * segments + (j % segments); };
        for (int j = 0; j < segments; ++j)
            mesh.triangles.push_back({ 0, at(1, j), at(1, j + 1) });
        for (int i = 1; i < rings - 1; ++i)
            for (int j = 0; j < segments; ++j)
            {
                mesh.triangles.push_back({ at(i, j), at(i + 1, j), at(i + 1, j + 1) });
                mesh.triangles.push_back({ at(i, j), at(i + 1, j + 1), at(i, j + 1) });
            }
        for (int j = 0; j < segments; ++j)
            mesh.triangles.push_back({ at(rings - 1, j), bottom, at(rings - 1, j + 1) });
        return mesh;
    }

    // Symmetric about x = 0 only: a block, a lump high on one side in Y, and one
    // low on the other side at a different X span (centred too).
    Mesh MirroredPart()
    {
        Mesh mesh;
        AddBox(mesh, { -30, -12, -5 }, { 30, 12, 5 });
        AddBox(mesh, { -8, 4, 5 }, { 8, 10, 12 });
        AddBox(mesh, { -15, -12, -9 }, { 15, -6, -5 });
        return mesh;
    }

    double AngleDegrees(const Point3& a, const Point3& b)
    {
        return std::acos(std::min(1.0, std::fabs(Dot(Normalised(a), Normalised(b))))) * 180.0 / kPi;
    }
}

TEST_CASE("the nearest point on a mesh is found within the search distance")
{
    Mesh mesh;
    AddBox(mesh, { 0, 0, 0 }, { 10, 10, 10 });
    const TriangleGrid grid(mesh, 1.0);
    TriangleGrid::Nearest n = grid.Find({ 5, 5, 12 }, 5.0);
    REQUIRE(n.found);
    CHECK(n.distance == doctest::Approx(2.0));
    CHECK(n.signedDistance == doctest::Approx(2.0));
    CHECK(n.point.z == doctest::Approx(10.0));

    n = grid.Find({ 5, 5, 9.5 }, 5.0);
    REQUIRE(n.found);
    CHECK(n.signedDistance == doctest::Approx(-0.5));

    // Outside a corner, the nearest point is the corner, and it's outside.
    n = grid.Find({ 11, 11, 11 }, 5.0);
    REQUIRE(n.found);
    CHECK(n.distance == doctest::Approx(std::sqrt(3.0)));
    CHECK(n.signedDistance > 0.0);

    CHECK_FALSE(grid.Find({ 5, 5, 16 }, 5.0).found);
    CHECK_FALSE(grid.Find({ 50, 50, 50 }, 5.0).found);
}

TEST_CASE("deviations agree with a brute force search")
{
    const Mesh mesh = Blob();
    std::mt19937 random(7);
    std::uniform_real_distribution<double> coordinate(-28.0, 28.0);
    std::vector<Point3> points(500);
    for (Point3& p : points)
        p = { coordinate(random), coordinate(random), coordinate(random) };
    const std::vector<float> values = Deviations(points, mesh, 3.0);
    for (size_t i = 0; i < points.size(); ++i)
    {
        double best = 1e300;
        for (const auto& t : mesh.triangles)
            best = std::min(best, Length(points[i] - ClosestPointOnTriangle(points[i], mesh.vertices[t[0]],
                                                                             mesh.vertices[t[1]],
                                                                             mesh.vertices[t[2]])));
        if (best <= 3.0 - 1e-6)
        {
            REQUIRE_FALSE(std::isnan(values[i]));
            CHECK(std::fabs(values[i]) == doctest::Approx(best).epsilon(1e-5));
        }
        else if (best > 3.0 + 1e-6)
        {
            CHECK(std::isnan(values[i]));
        }
    }
}

TEST_CASE("a mesh mirrored about a tilted plane is found")
{
    Mesh mesh = MirroredPart();
    // Turn and move it so the symmetry plane is tilted and off the origin.
    const Transform move = Transform::Rotation(Normalised({ 0.3, 1.0, 0.4 }), 0.6, { 0, 0, 0 })
                               .Then(Transform::Translation({ 12.5, -7.25, 40.0 }));
    TransformMesh(mesh, move);
    const Point3 trueNormal = move.ApplyDirection({ 1, 0, 0 });
    const Point3 truePoint = move.Apply({ 0, 0, 0 });

    // Start from a box a little off, so the refinement has work to do.
    OrientedBox box = FitOrientedBox(mesh);
    box.ApplyTransform(Transform::Rotation(Normalised({ 0.2, 0.7, -0.4 }), 2.0 * kPi / 180.0, box.centre)
                           .Then(Transform::Translation({ 0.4, -0.3, 0.2 })));

    const auto start = std::chrono::steady_clock::now();
    const std::vector<SymmetryCandidate> found = FindSymmetryPlanes(mesh, box, {});
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    REQUIRE_FALSE(found.empty());
    const SymmetryCandidate& best = found.front();
    MESSAGE("within " << best.score.within << ", rms " << best.score.rms << " mm, in " << seconds << " s");
    CHECK(best.score.within > 0.95);
    CHECK(AngleDegrees(best.plane.direction, trueNormal) < 0.05);
    CHECK(std::fabs(Dot(best.plane.point - truePoint, trueNormal)) < 0.01);
}

TEST_CASE("a part with no symmetry is refused")
{
    const Mesh mesh = Blob();
    const OrientedBox box = FitOrientedBox(mesh);
    const std::vector<SymmetryCandidate> found = FindSymmetryPlanes(mesh, box, {});
    REQUIRE_FALSE(found.empty());
    MESSAGE("best within " << found.front().score.within);
    CHECK(found.front().score.within < 0.3);
}

TEST_CASE("mid plane between two parallel planes")
{
    Feature a, b, plane;
    a.kind = b.kind = Feature::Kind::Plane;
    a.point = { 3, 4, 5 };
    a.direction = { 0, 0, 1 };
    b.point = { -7, 1, 25 };
    b.direction = { 0, 0, -1 };         // facing the other way, as a part's two sides do
    std::string message;
    REQUIRE(MidPlane(a, b, plane, message));
    CHECK(AngleDegrees(plane.direction, { 0, 0, 1 }) < 1e-9);
    CHECK(std::fabs(Dot(plane.point - a.point, plane.direction)) == doctest::Approx(10.0));
    CHECK(std::fabs(Dot(plane.point - b.point, plane.direction)) == doctest::Approx(10.0));

    // Two planes at an angle: the bisector, equally far from both everywhere on it.
    b.point = { 0, 0, 0 };
    b.direction = Normalised({ 0, 1, 1 });
    REQUIRE(MidPlane(a, b, plane, message));
    const Point3 u = Normalised(Cross(plane.direction, { 1, 0, 0 }));
    for (double t : { -10.0, 0.0, 15.0 })
    {
        const Point3 p = plane.point + u * t + Point3{ 1, 0, 0 } * t;
        CHECK(Dot(p - a.point, a.direction) == doctest::Approx(-Dot(p - b.point, b.direction)).epsilon(1e-9));
    }
}

TEST_CASE("mid plane between two axes, two points, and what's refused")
{
    Feature a, b, plane;
    a.kind = b.kind = Feature::Kind::Axis;
    a.point = { -10, 0, 5 };
    a.direction = { 0, 0, 1 };
    b.point = { 10, 0, -20 };
    b.direction = { 0, 0, -1 };
    std::string message;
    REQUIRE(MidPlane(a, b, plane, message));
    CHECK(AngleDegrees(plane.direction, { 1, 0, 0 }) < 1e-9);
    CHECK(Dot(plane.point, plane.direction) == doctest::Approx(0.0));

    a.kind = b.kind = Feature::Kind::Point;
    a.point = { 2, 2, 2 };
    b.point = { 2, 2, 8 };
    REQUIRE(MidPlane(a, b, plane, message));
    CHECK(AngleDegrees(plane.direction, { 0, 0, 1 }) < 1e-9);
    CHECK(plane.point.z == doctest::Approx(5.0));

    b.kind = Feature::Kind::Plane;
    CHECK_FALSE(MidPlane(a, b, plane, message));
    a.kind = Feature::Kind::Axis;
    b.kind = Feature::Kind::Plane;
    CHECK_FALSE(MidPlane(a, b, plane, message));
    a.kind = b.kind = Feature::Kind::Axis;
    a.direction = { 1, 0, 0 };
    b.direction = { 0, 1, 0 };
    a.point = b.point = { 0, 0, 0 };
    CHECK_FALSE(MidPlane(a, b, plane, message));
}

// Skipped unless asked for: set SCANTOALIBRE_STL to a scan (.stl or .ply) and run
//   ScanCoreTests.exe -tc="symmetry of a real scan*" --no-skip
TEST_CASE("symmetry of a real scan" * doctest::skip())
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
    MESSAGE("box centre (" << box.centre.x << ", " << box.centre.y << ", " << box.centre.z << "), half "
                           << box.halfSize[0] << " " << box.halfSize[1] << " " << box.halfSize[2] << "; samples "
                           << SurfaceSamples(mesh, 5).size());
    const auto t0 = std::chrono::steady_clock::now();
    SymmetryOptions options;
    if (_dupenv_s(&value, &length, "SCANTOALIBRE_ITERATIONS") == 0 && value != nullptr)
    {
        options.iterations = std::atoi(value);
        free(value);
    }
    const std::vector<SymmetryCandidate> found = FindSymmetryPlanes(mesh, box, {}, options);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    MESSAGE(mesh.triangles.size() << " triangles, " << found.size() << " planes in " << seconds << " s");
    for (const SymmetryCandidate& c : found)
        MESSAGE("normal (" << c.plane.direction.x << ", " << c.plane.direction.y << ", " << c.plane.direction.z
                           << "), point (" << c.plane.point.x << ", " << c.plane.point.y << ", " << c.plane.point.z
                           << "): within " << c.score.within << ", matched " << c.score.matched << ", rms "
                           << c.score.rms << ", trimmed " << c.score.trimmed);
}

TEST_CASE("detect finds the world plane a scan is symmetric about")
{
    // Symmetric about x = 0 only, with lumps big enough that the block's own
    // symmetry about XY and ZX doesn't pass.
    Mesh mesh;
    AddBox(mesh, { -30, -12, -5 }, { 30, 12, 5 });
    AddBox(mesh, { -15, 0, 5 }, { 15, 12, 20 });
    AddBox(mesh, { -20, -12, -12 }, { 20, -4, -5 });
    const std::array<SymmetryScore, 3> scores = ScoreWorldPlanes(mesh);
    MESSAGE("XY " << scores[0].within << ", YZ " << scores[1].within << ", ZX " << scores[2].within);
    CHECK(scores[1].within >= 0.99);
    CHECK(scores[0].within < 0.8);
    CHECK(scores[2].within < 0.8);
}
