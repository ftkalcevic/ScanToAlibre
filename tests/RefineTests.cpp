// RefineTests.cpp : decimating and smoothing.

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Decimate.h"
#include "Mesh.h"
#include "ScanFile.h"
#include "Smooth.h"

using namespace scancore;

namespace
{
    // An icosphere: an icosahedron split `levels` times, on a sphere about the origin.
    Mesh Icosphere(double radius, int levels)
    {
        const double t = (1.0 + std::sqrt(5.0)) / 2.0;
        Mesh mesh;
        mesh.vertices = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t },
                          { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
        mesh.triangles = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
                           { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
                           { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
                           { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
        for (int level = 0; level < levels; ++level)
        {
            std::map<std::pair<int, int>, int> middles;
            auto middle = [&](int a, int b) {
                const auto key = std::make_pair(std::min(a, b), std::max(a, b));
                const auto found = middles.find(key);
                if (found != middles.end())
                    return found->second;
                const int index = static_cast<int>(mesh.vertices.size());
                mesh.vertices.push_back((mesh.vertices[a] + mesh.vertices[b]) * 0.5);
                middles[key] = index;
                return index;
            };
            std::vector<std::array<int, 3>> next;
            for (const auto& tri : mesh.triangles)
            {
                const int ab = middle(tri[0], tri[1]), bc = middle(tri[1], tri[2]), ca = middle(tri[2], tri[0]);
                next.push_back({ tri[0], ab, ca });
                next.push_back({ tri[1], bc, ab });
                next.push_back({ tri[2], ca, bc });
                next.push_back({ ab, bc, ca });
            }
            mesh.triangles = next;
        }
        for (Point3& p : mesh.vertices)
            p = Normalised(p) * radius;
        return mesh;
    }

    // A flat grid in z = 0, nx by ny squares of the given size, open all round.
    Mesh Grid(int nx, int ny, double size)
    {
        Mesh mesh;
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i)
                mesh.vertices.push_back({ i * size, j * size, 0.0 });
        auto at = [nx](int i, int j) { return j * (nx + 1) + i; };
        for (int j = 0; j < ny; ++j)
        {
            for (int i = 0; i < nx; ++i)
            {
                mesh.triangles.push_back({ at(i, j), at(i + 1, j), at(i + 1, j + 1) });
                mesh.triangles.push_back({ at(i, j), at(i + 1, j + 1), at(i, j + 1) });
            }
        }
        return mesh;
    }

    // A closed cube from 0 to size, each face split into n by n squares.
    Mesh Cube(int n, double size)
    {
        Mesh mesh;
        std::map<std::tuple<int, int, int>, int> index;
        auto vertex = [&](int x, int y, int z) {
            const auto key = std::make_tuple(x, y, z);
            const auto found = index.find(key);
            if (found != index.end())
                return found->second;
            const int i = static_cast<int>(mesh.vertices.size());
            mesh.vertices.push_back({ size * x / n, size * y / n, size * z / n });
            index[key] = i;
            return i;
        };
        // Each face: an origin, two directions in grid steps, outward by u x v.
        struct Face { int o[3]; int u[3]; int v[3]; };
        const Face faces[6] = {
            { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 } },      // z = 0, normal -Z
            { { 0, 0, n }, { 1, 0, 0 }, { 0, 1, 0 } },      // z = n, +Z
            { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },      // y = 0, -Y
            { { 0, n, 0 }, { 0, 0, 1 }, { 1, 0, 0 } },      // y = n, +Y
            { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },      // x = 0, -X
            { { n, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },      // x = n, +X
        };
        for (const Face& f : faces)
        {
            for (int a = 0; a < n; ++a)
            {
                for (int b = 0; b < n; ++b)
                {
                    auto corner = [&](int da, int db) {
                        return vertex(f.o[0] + (a + da) * f.u[0] + (b + db) * f.v[0],
                                      f.o[1] + (a + da) * f.u[1] + (b + db) * f.v[1],
                                      f.o[2] + (a + da) * f.u[2] + (b + db) * f.v[2]);
                    };
                    const int p00 = corner(0, 0), p10 = corner(1, 0), p11 = corner(1, 1), p01 = corner(0, 1);
                    mesh.triangles.push_back({ p00, p10, p11 });
                    mesh.triangles.push_back({ p00, p11, p01 });
                }
            }
        }
        return mesh;
    }

    // Repeatable noise in -1 to 1.
    double Noise(uint32_t& state)
    {
        state = state * 1664525u + 1013904223u;
        return (static_cast<double>(state >> 8) / static_cast<double>(1u << 24)) * 2.0 - 1.0;
    }

    void AddNormalNoise(Mesh& mesh, double amplitude)
    {
        const std::vector<Point3> normals = VertexNormals(mesh);
        uint32_t state = 12345;
        for (size_t v = 0; v < mesh.vertices.size(); ++v)
            mesh.vertices[v] = mesh.vertices[v] + normals[v] * (amplitude * Noise(state));
    }

    double Volume(const Mesh& mesh)
    {
        double volume = 0.0;
        for (const auto& t : mesh.triangles)
            volume += Dot(mesh.vertices[t[0]], Cross(mesh.vertices[t[1]], mesh.vertices[t[2]])) / 6.0;
        return volume;
    }

    // Each edge used by exactly two triangles, once each way: closed and consistently oriented.
    bool ClosedAndOriented(const Mesh& mesh)
    {
        std::map<std::pair<int, int>, int> directed;
        for (const auto& t : mesh.triangles)
        {
            if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2])
                return false;
            for (int k = 0; k < 3; ++k)
                ++directed[{ t[k], t[(k + 1) % 3] }];
        }
        for (const auto& entry : directed)
        {
            if (entry.second != 1)
                return false;
            if (directed.find({ entry.first.second, entry.first.first }) == directed.end())
                return false;
        }
        return true;
    }

    double MeanRadius(const Mesh& mesh)
    {
        double sum = 0.0;
        for (const Point3& p : mesh.vertices)
            sum += Length(p);
        return sum / static_cast<double>(mesh.vertices.size());
    }

    double RadiusSpread(const Mesh& mesh)
    {
        const double mean = MeanRadius(mesh);
        double sum = 0.0;
        for (const Point3& p : mesh.vertices)
            sum += (Length(p) - mean) * (Length(p) - mean);
        return std::sqrt(sum / static_cast<double>(mesh.vertices.size()));
    }

    double NearestTo(const Mesh& mesh, const Point3& target)
    {
        double best = 1e300;
        for (const Point3& p : mesh.vertices)
            best = std::min(best, Length(p - target));
        return best;
    }
}

TEST_CASE("decimating a sphere keeps it closed, oriented and round")
{
    const Mesh sphere = Icosphere(20.0, 5);     // 20,480 triangles
    REQUIRE(sphere.triangles.size() == 20480);
    REQUIRE(ClosedAndOriented(sphere));

    DecimateOptions options;
    options.targetTriangles = 2000;
    DecimateResult result;
    const Mesh out = Decimate(sphere, options, &result);
    CHECK(out.triangles.size() <= 2000);
    CHECK(out.triangles.size() >= 1800);
    CHECK(result.trianglesBefore == 20480);
    CHECK(result.trianglesAfter == out.triangles.size());
    CHECK(ClosedAndOriented(out));
    for (const Point3& p : out.vertices)
        CHECK(std::fabs(Length(p) - 20.0) < 0.2);
    CHECK(Volume(out) == doctest::Approx(Volume(sphere)).epsilon(0.02));
}

TEST_CASE("decimating stops at the largest change asked for")
{
    const Mesh sphere = Icosphere(20.0, 4);
    DecimateOptions options;
    options.targetTriangles = 100;
    options.maxError = 0.25;
    DecimateResult result;
    const Mesh out = Decimate(sphere, options, &result);
    CHECK(result.stoppedByError);
    CHECK(out.triangles.size() > 100);
    CHECK(out.triangles.size() < sphere.triangles.size());
    for (const Point3& p : out.vertices)
        CHECK(std::fabs(Length(p) - 20.0) < 0.25);
}

TEST_CASE("decimating a flat grid with open edges kept keeps its outline")
{
    const Mesh grid = Grid(60, 40, 1.0);
    DecimateOptions options;
    options.targetTriangles = 200;
    options.keepOpenEdges = true;
    const Mesh out = Decimate(grid, options);
    CHECK(out.triangles.size() <= 200);
    CHECK(out.triangles.size() > 0);
    const Box3 box = Bounds(out);
    CHECK(box.low.x == doctest::Approx(0.0));
    CHECK(box.low.y == doctest::Approx(0.0));
    CHECK(box.high.x == doctest::Approx(60.0));
    CHECK(box.high.y == doctest::Approx(40.0));
    CHECK(SurfaceArea(out) == doctest::Approx(2400.0).epsilon(1e-6));
    for (const Point3& p : out.vertices)
        CHECK(std::fabs(p.z) < 1e-9);
}

TEST_CASE("decimating a cube keeps its corners")
{
    const Mesh cube = Cube(16, 30.0);
    REQUIRE(ClosedAndOriented(cube));
    REQUIRE(Volume(cube) == doctest::Approx(27000.0));

    DecimateOptions options;
    options.targetTriangles = 100;
    const Mesh out = Decimate(cube, options);
    CHECK(out.triangles.size() <= 100);
    CHECK(ClosedAndOriented(out));
    CHECK(Volume(out) == doctest::Approx(27000.0).epsilon(1e-6));
    for (int i = 0; i < 8; ++i)
    {
        const Point3 corner{ (i & 1) * 30.0, ((i >> 1) & 1) * 30.0, ((i >> 2) & 1) * 30.0 };
        CHECK(NearestTo(out, corner) < 1e-6);
    }
}

TEST_CASE("a mesh already small enough isn't decimated")
{
    const Mesh sphere = Icosphere(10.0, 2);
    DecimateOptions options;
    options.targetTriangles = 100000;
    DecimateResult result;
    const Mesh out = Decimate(sphere, options, &result);
    CHECK(out.triangles == sphere.triangles);
    CHECK(result.collapses == 0);
}

TEST_CASE("every smoothing method takes noise off a sphere, and Taubin shrinks it least")
{
    Mesh noisy = Icosphere(20.0, 5);
    AddNormalNoise(noisy, 0.2);
    const double spreadBefore = RadiusSpread(noisy);
    REQUIRE(spreadBefore > 0.08);

    double shrink[3] = {};
    const SmoothMethod methods[3] = { SmoothMethod::Laplacian, SmoothMethod::Taubin, SmoothMethod::Bilateral };
    for (int m = 0; m < 3; ++m)
    {
        SmoothOptions options;
        options.method = methods[m];
        options.passes = 5;
        options.strength = 0.5;
        SmoothResult result;
        const Mesh out = Smooth(noisy, options, &result);
        CAPTURE(m);
        CHECK(out.triangles == noisy.triangles);
        CHECK(RadiusSpread(out) < 0.5 * spreadBefore);
        CHECK(result.largestMove > 0.0);
        shrink[m] = 20.0 - MeanRadius(out);
    }
    CHECK(std::fabs(shrink[1]) < std::fabs(shrink[0]));
    CHECK(std::fabs(shrink[2]) < std::fabs(shrink[0]));
}

TEST_CASE("bilateral smoothing keeps a noisy cube's corners sharper than Laplacian")
{
    Mesh noisy = Cube(16, 30.0);
    AddNormalNoise(noisy, 0.15);

    SmoothOptions options;
    options.passes = 5;
    options.strength = 0.5;
    options.method = SmoothMethod::Laplacian;
    const Mesh laplacian = Smooth(noisy, options);
    options.method = SmoothMethod::Bilateral;
    const Mesh bilateral = Smooth(noisy, options);

    double laplacianCorners = 0.0, bilateralCorners = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        const Point3 corner{ (i & 1) * 30.0, ((i >> 1) & 1) * 30.0, ((i >> 2) & 1) * 30.0 };
        laplacianCorners += NearestTo(laplacian, corner);
        bilateralCorners += NearestTo(bilateral, corner);
    }
    CHECK(bilateralCorners < 0.5 * laplacianCorners);

    // And the faces are flatter than they were: a face's middle vertices close to its plane.
    double before = 0.0, after = 0.0;
    int count = 0;
    for (size_t v = 0; v < noisy.vertices.size(); ++v)
    {
        const Point3 original = noisy.vertices[v];
        // Vertices on the top face away from its edges.
        if (bilateral.vertices[v].z > 25.0 && original.x > 3.0 && original.x < 27.0 && original.y > 3.0 &&
            original.y < 27.0)
        {
            before += std::fabs(original.z - 30.0);
            after += std::fabs(bilateral.vertices[v].z - 30.0);
            ++count;
        }
    }
    REQUIRE(count > 50);
    CHECK(after < 0.6 * before);
}

TEST_CASE("smoothing with open edges kept leaves them where they are")
{
    Mesh grid = Grid(30, 20, 1.0);
    uint32_t state = 7;
    for (Point3& p : grid.vertices)
        p.z = 0.1 * Noise(state);
    const std::vector<bool> open = OpenEdgeVertices(grid);

    SmoothOptions options;
    options.method = SmoothMethod::Laplacian;
    options.passes = 10;
    options.keepOpenEdges = true;
    const Mesh out = Smooth(grid, options);
    int openCount = 0;
    for (size_t v = 0; v < grid.vertices.size(); ++v)
    {
        if (!open[v])
            continue;
        ++openCount;
        CHECK(Length(out.vertices[v] - grid.vertices[v]) == 0.0);
    }
    CHECK(openCount == 2 * (30 + 20));
}

// Skipped unless asked for: set SCANTOALIBRE_PLY (or SCANTOALIBRE_STL) to a scan and run
//   ScanCoreTests.exe -tc="refine timing*" --no-skip
TEST_CASE("refine timing" * doctest::skip())
{
    char* value = nullptr;
    size_t length = 0;
    _dupenv_s(&value, &length, "SCANTOALIBRE_PLY");
    if (value == nullptr)
        _dupenv_s(&value, &length, "SCANTOALIBRE_STL");
    REQUIRE(value != nullptr);
    const std::string path(value);
    free(value);

    Mesh mesh;
    ScanFileInfo info;
    std::string message;
    REQUIRE(ReadScanFile(path, mesh, info, message));
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };

    for (size_t target : { mesh.triangles.size() / 2, mesh.triangles.size() / 10 })
    {
        DecimateOptions options;
        options.targetTriangles = target;
        DecimateResult result;
        const auto t0 = std::chrono::steady_clock::now();
        const Mesh out = Decimate(mesh, options, &result);
        const auto t1 = std::chrono::steady_clock::now();
        MESSAGE("decimate " << mesh.triangles.size() << " to " << out.triangles.size() << " in " << ms(t0, t1)
                            << " ms, " << result.refused << " refused, largest error " << result.largestError);
    }
    for (SmoothMethod method : { SmoothMethod::Laplacian, SmoothMethod::Taubin, SmoothMethod::Bilateral })
    {
        SmoothOptions options;
        options.method = method;
        SmoothResult result;
        const auto t0 = std::chrono::steady_clock::now();
        Smooth(mesh, options, &result);
        const auto t1 = std::chrono::steady_clock::now();
        MESSAGE("smooth method " << static_cast<int>(method) << ", 5 passes, in " << ms(t0, t1)
                                 << " ms, largest move " << result.largestMove << ", average "
                                 << result.averageMove);
    }
}
