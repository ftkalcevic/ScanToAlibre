// ReconstructTests.cpp : meshes made from point clouds with normals.

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "Ply.h"
#include "Reconstruct.h"

using namespace scancore;

namespace
{
    const double kPi = 3.14159265358979323846;

    // Points spread evenly over a sphere (a Fibonacci spiral), facing out.
    void SpherePoints(double radius, int count, std::vector<Point3>& points, std::vector<Point3>& normals,
                      double noise = 0.0)
    {
        uint32_t state = 99;
        auto random = [&state]() {
            state = state * 1664525u + 1013904223u;
            return (static_cast<double>(state >> 8) / static_cast<double>(1u << 24)) * 2.0 - 1.0;
        };
        const double golden = kPi * (3.0 - std::sqrt(5.0));
        for (int i = 0; i < count; ++i)
        {
            const double z = 1.0 - 2.0 * (i + 0.5) / count;
            const double r = std::sqrt(1.0 - z * z);
            const Point3 n{ r * std::cos(golden * i), r * std::sin(golden * i), z };
            points.push_back(n * (radius + noise * random()));
            normals.push_back(n);
        }
    }

    double Volume(const Mesh& mesh)
    {
        double volume = 0.0;
        for (const auto& t : mesh.triangles)
            volume += Dot(mesh.vertices[t[0]], Cross(mesh.vertices[t[1]], mesh.vertices[t[2]])) / 6.0;
        return volume;
    }

    // Each edge used by exactly two triangles, once each way.
    bool ClosedAndOriented(const Mesh& mesh)
    {
        std::map<std::pair<int, int>, int> directed;
        for (const auto& t : mesh.triangles)
            for (int k = 0; k < 3; ++k)
                ++directed[{ t[k], t[(k + 1) % 3] }];
        for (const auto& entry : directed)
        {
            if (entry.second != 1)
                return false;
            if (directed.find({ entry.first.second, entry.first.first }) == directed.end())
                return false;
        }
        return true;
    }
}

TEST_CASE("the spacing of evenly spread points is found")
{
    std::vector<Point3> points;
    for (int i = 0; i < 50; ++i)
        for (int j = 0; j < 40; ++j)
            points.push_back({ i * 0.5, j * 0.5, 0.0 });
    CHECK(PointSpacing(points) == doctest::Approx(0.5).epsilon(0.05));
}

TEST_CASE("a sphere's points make a closed, outward, round mesh")
{
    std::vector<Point3> points, normals;
    SpherePoints(20.0, 20000, points, normals);
    Mesh mesh;
    std::string message;
    ReconstructResult result;
    REQUIRE(ReconstructSurface(points, normals, {}, mesh, message, &result));
    CHECK(result.spacing == doctest::Approx(0.5).epsilon(0.15));
    CHECK(result.triangles > 20000);
    CHECK(ClosedAndOriented(mesh));
    CHECK(Volume(mesh) == doctest::Approx(4.0 / 3.0 * kPi * 8000.0).epsilon(0.02));
    double worst = 0.0;
    for (const Point3& p : mesh.vertices)
        worst = std::max(worst, std::fabs(Length(p) - 20.0));
    CHECK(worst < 0.3 * result.cellSize);
}

TEST_CASE("noise on the points is smoothed out of the mesh")
{
    std::vector<Point3> points, normals;
    SpherePoints(20.0, 20000, points, normals, 0.1);
    Mesh mesh;
    std::string message;
    REQUIRE(ReconstructSurface(points, normals, {}, mesh, message));
    double sum = 0.0;
    for (const Point3& p : mesh.vertices)
        sum += (Length(p) - 20.0) * (Length(p) - 20.0);
    const double rms = std::sqrt(sum / static_cast<double>(mesh.vertices.size()));
    CHECK(rms < 0.06);      // the points' own scatter is about 0.058 rms
}

TEST_CASE("a flat patch of points makes a flat mesh that stops near its edges")
{
    std::vector<Point3> points, normals;
    for (int i = 0; i <= 80; ++i)
        for (int j = 0; j <= 60; ++j)
        {
            points.push_back({ i * 0.5, j * 0.5, 0.0 });
            normals.push_back({ 0.0, 0.0, 1.0 });
        }
    Mesh mesh;
    std::string message;
    ReconstructResult result;
    REQUIRE(ReconstructSurface(points, normals, {}, mesh, message, &result));
    const Box3 box = Bounds(mesh);
    CHECK(box.low.x > -1.5 * result.cellSize);
    CHECK(box.high.x < 40.0 + 1.5 * result.cellSize);
    CHECK(box.low.y > -1.5 * result.cellSize);
    CHECK(box.high.y < 30.0 + 1.5 * result.cellSize);
    for (const Point3& p : mesh.vertices)
        CHECK(std::fabs(p.z) < 1e-6);
    CHECK(SurfaceArea(mesh) == doctest::Approx(1200.0).epsilon(0.1));
    // Facing up, the way the normals point.
    const Point3 n = Cross(mesh.vertices[mesh.triangles[0][1]] - mesh.vertices[mesh.triangles[0][0]],
                           mesh.vertices[mesh.triangles[0][2]] - mesh.vertices[mesh.triangles[0][0]]);
    CHECK(n.z > 0.0);
}

TEST_CASE("a point cloud without normals, or too small, is refused")
{
    std::vector<Point3> points(5, Point3{ 1, 2, 3 }), normals;
    Mesh mesh;
    std::string message;
    CHECK_FALSE(ReconstructSurface(points, normals, {}, mesh, message));
    CHECK_FALSE(message.empty());
}

// Skipped unless asked for: set SCANTOALIBRE_PLY to a PLY point cloud and run
//   ScanCoreTests.exe -tc="reconstruct a real cloud*" --no-skip
TEST_CASE("reconstruct a real cloud" * doctest::skip())
{
    char* value = nullptr;
    size_t length = 0;
    REQUIRE(_dupenv_s(&value, &length, "SCANTOALIBRE_PLY") == 0);
    REQUIRE(value != nullptr);
    const std::string path(value);
    free(value);

    Mesh cloud;
    std::vector<Point3> normals;
    std::string message;
    PlyReadInfo info;
    REQUIRE(ReadPly(path, cloud, message, &info, &normals));
    REQUIRE(info.hasNormals);
    const auto t0 = std::chrono::steady_clock::now();
    Mesh mesh;
    ReconstructResult result;
    ReconstructOptions options;
    char* gap = nullptr;
    if (_dupenv_s(&gap, &length, "SCANTOALIBRE_GAP") == 0 && gap != nullptr)
    {
        options.gapCells = std::atoi(gap);
        free(gap);
    }
    REQUIRE(ReconstructSurface(cloud.vertices, normals, options, mesh, message, &result));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::map<std::pair<int, int>, int> edges;
    for (const auto& t : mesh.triangles)
        for (int k = 0; k < 3; ++k)
        {
            const int a = std::min(t[k], t[(k + 1) % 3]), b = std::max(t[k], t[(k + 1) % 3]);
            ++edges[{ a, b }];
        }
    size_t open = 0, tangled = 0;
    for (const auto& e : edges)
    {
        open += (e.second == 1) ? 1 : 0;
        tangled += (e.second > 2) ? 1 : 0;
    }
    MESSAGE(result.points << " points, spacing " << result.spacing << " mm, cell " << result.cellSize << " mm, "
                          << result.gridCorners << " grid corners, " << result.triangles << " triangles, " << open
                          << " open edges, " << tangled << " edges with more than two triangles, in " << ms << " ms");
}
