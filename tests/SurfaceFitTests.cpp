// SurfaceFitTests.cpp : 5 Surface: selecting areas and fitting height-field surfaces.

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "Iges.h"
#include "Orient.h"
#include "SurfaceFit.h"

using namespace scancore;

namespace
{
    // A grid of (nx + 1) x (ny + 1) points over x0..x1, y0..y1 with z = height(x, y),
    // two triangles a square, facing +Z.
    Mesh HeightMesh(double x0, double x1, double y0, double y1, int nx, int ny,
                    const std::function<double(double, double)>& height)
    {
        Mesh mesh;
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i)
            {
                const double x = x0 + (x1 - x0) * i / nx, y = y0 + (y1 - y0) * j / ny;
                mesh.vertices.push_back({ x, y, height(x, y) });
            }
        auto at = [&](int i, int j) { return i + j * (nx + 1); };
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
            {
                mesh.triangles.push_back({ at(i, j), at(i + 1, j), at(i + 1, j + 1) });
                mesh.triangles.push_back({ at(i, j), at(i + 1, j + 1), at(i, j + 1) });
            }
        return mesh;
    }

    std::vector<int> All(const Mesh& mesh)
    {
        std::vector<int> all(mesh.triangles.size());
        std::iota(all.begin(), all.end(), 0);
        return all;
    }

    // A repeatable number from -1 to 1.
    double Noise(unsigned& state)
    {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    }

    std::vector<std::string> Lines(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
        }
        return lines;
    }
}

TEST_CASE("a smooth height field is fitted back closely")
{
    auto height = [](double x, double y) { return 5.0 * std::sin(x / 30.0) * std::cos(y / 25.0); };
    const Mesh mesh = HeightMesh(0, 100, 0, 80, 100, 80, height);
    SurfaceFitOptions options;
    options.base = SurfaceBase::XY;
    options.perimeter = SurfacePerimeter::Rectangle;
    options.tolerance = 0.002;
    options.smoothing = 0.0;
    const SurfaceFitResult result = FitScanSurface(mesh, All(mesh), options);
    REQUIRE(result.ok);
    std::string message;
    CHECK(ValidateSurface(result.surface, message));
    CHECK(result.within99 <= 0.002);
    CHECK(result.largest < 0.01);
    CHECK(result.outline.empty());

    // The surface is in place: its height at a few points matches.
    for (double x : { 20.0, 50.0, 85.0 })
        for (double y : { 10.0, 40.0, 70.0 })
        {
            const Point3 d = Point3{ x, y, 0 } - result.frame.origin;
            CHECK(SurfaceHeightAt(result, Dot(d, result.frame.u), Dot(d, result.frame.v)) + result.frame.origin.z ==
                  doctest::Approx(height(x, y)).epsilon(0.002));
        }
}

TEST_CASE("noise on a height field shows in the RMS, and smoothing stops it being followed")
{
    unsigned state = 12345;
    Mesh mesh = HeightMesh(0, 60, 0, 60, 120, 120, [](double x, double y) { return 0.002 * (x * x - y * y); });
    for (Point3& p : mesh.vertices)
        p.z += 0.02 * std::sqrt(3.0) * Noise(state);    // uniform, standard deviation 0.02
    SurfaceFitOptions options;
    options.base = SurfaceBase::XY;
    options.perimeter = SurfacePerimeter::Rectangle;
    options.tolerance = 0.05;
    const SurfaceFitResult result = FitScanSurface(mesh, All(mesh), options);
    REQUIRE(result.ok);
    CHECK(result.rms > 0.015);
    CHECK(result.rms < 0.03);
    CHECK(result.within99 <= 0.05);
    CHECK(result.surface.countU < 20);
}

TEST_CASE("a half dome with YZ symmetry fits a symmetric whole dome")
{
    const Mesh mesh = HeightMesh(0, 30, -30, 30, 30, 60, [](double x, double y) { return 20.0 - (x * x + y * y) / 60.0; });
    SurfaceFitOptions options;
    options.base = SurfaceBase::XY;
    options.symmetryYZ = true;
    options.perimeter = SurfacePerimeter::Rectangle;
    const SurfaceFitResult result = FitScanSurface(mesh, All(mesh), options);
    REQUIRE(result.ok);
    CHECK(result.s0 == doctest::Approx(-result.s1));
    CHECK(std::fabs(result.frame.u.x) == doctest::Approx(1.0));
    for (double s : { 3.0, 11.0, 25.0 })
        for (double t : { -20.0, 0.0, 17.0 })
            CHECK(std::fabs(SurfaceHeightAt(result, s, t) - SurfaceHeightAt(result, -s, t)) < 1e-9);
    // Its middle is the dome's top.
    const Point3 d = Point3{ 0, 0, 0 } - result.frame.origin;
    CHECK(SurfaceHeightAt(result, Dot(d, result.frame.u), Dot(d, result.frame.v)) + result.frame.origin.z ==
          doctest::Approx(20.0).epsilon(0.01));
}

TEST_CASE("a symmetry plane can't also be the base plane, nor can there be three")
{
    const Mesh mesh = HeightMesh(0, 10, 0, 10, 4, 4, [](double, double) { return 0.0; });
    SurfaceFitOptions options;
    options.base = SurfaceBase::XY;
    options.symmetryXY = true;
    SurfaceFitResult result = FitScanSurface(mesh, All(mesh), options);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.message.empty());

    options.base = SurfaceBase::Auto;
    options.symmetryYZ = options.symmetryZX = true;
    result = FitScanSurface(mesh, All(mesh), options);
    CHECK_FALSE(result.ok);
}

TEST_CASE("Auto with two symmetry planes uses their common direction as the normal")
{
    const Mesh mesh = HeightMesh(-20, 20, -20, 20, 20, 20, [](double x, double y) { return 10.0 - 0.01 * (x * x + y * y); });
    SurfaceFitOptions options;
    options.base = SurfaceBase::Auto;
    options.symmetryYZ = options.symmetryZX = true;
    options.perimeter = SurfacePerimeter::Rectangle;
    const SurfaceFitResult result = FitScanSurface(mesh, All(mesh), options);
    REQUIRE(result.ok);
    CHECK(result.frame.n.z == doctest::Approx(1.0));
    CHECK(SurfaceHeightAt(result, 7, 4) == doctest::Approx(SurfaceHeightAt(result, -7, -4)).epsilon(1e-9));
}

TEST_CASE("a selection folding over the base plane is refused")
{
    // A tube round Z: its sides face sideways.
    Mesh mesh;
    const int n = 24;
    for (int k = 0; k < 2; ++k)
        for (int i = 0; i < n; ++i)
        {
            const double a = 2.0 * 3.14159265358979 * i / n;
            mesh.vertices.push_back({ 10 * std::cos(a), 10 * std::sin(a), 10.0 * k });
        }
    for (int i = 0; i < n; ++i)
    {
        const int a = i, b = (i + 1) % n;
        mesh.triangles.push_back({ a, b, b + n });
        mesh.triangles.push_back({ a, b + n, a + n });
    }
    SurfaceFitOptions options;
    options.base = SurfaceBase::XY;
    const SurfaceFitResult result = FitScanSurface(mesh, All(mesh), options);
    CHECK_FALSE(result.ok);
    CHECK(result.message.find("85 degrees") != std::string::npos);
}

TEST_CASE("a disc's outline is a near circle and the trimmed surface is written")
{
    const Mesh grid = HeightMesh(-25, 25, -25, 25, 100, 100, [](double x, double y) { return 0.01 * x * y; });
    std::vector<int> disc;
    for (size_t t = 0; t < grid.triangles.size(); ++t)
    {
        const auto& tri = grid.triangles[t];
        const Point3 middle = (grid.vertices[tri[0]] + grid.vertices[tri[1]] + grid.vertices[tri[2]]) / 3.0;
        if (middle.x * middle.x + middle.y * middle.y < 20.0 * 20.0)
            disc.push_back(static_cast<int>(t));
    }
    SurfaceFitOptions options;
    options.base = SurfaceBase::XY;
    options.perimeter = SurfacePerimeter::Selection;
    const SurfaceFitResult result = FitScanSurface(grid, disc, options);
    REQUIRE(result.ok);
    REQUIRE(result.outline.size() >= 12);
    REQUIRE(result.outline.size() == result.outlineUV.size());
    double area = 0.0;
    for (size_t i = 0; i < result.outline.size(); ++i)
    {
        const Point3& p = result.outline[i];
        CHECK(std::sqrt(p.x * p.x + p.y * p.y) == doctest::Approx(20.0).epsilon(0.03));
        // On the surface, which is within about the tolerance of the scan at its rim.
        CHECK(std::fabs(p.z - 0.01 * p.x * p.y) < 2.0 * options.tolerance);
        const auto& a = result.outlineUV[i];
        const auto& b = result.outlineUV[(i + 1) % result.outlineUV.size()];
        area += a[0] * b[1] - b[0] * a[1];
    }
    CHECK(area > 0.0);      // anticlockwise

    IgesHeader header;
    const std::vector<std::string> lines =
        Lines(WriteIgesTrimmedSurface(result.surface, result.outlineUV, result.outline, header));
    std::vector<std::string> types;
    int parameterLines = 0;
    for (const std::string& line : lines)
    {
        REQUIRE(line.size() == 80);
        if (line[72] == 'D')
            types.push_back(line.substr(0, 8));
        if (line[72] == 'P')
            ++parameterLines;
    }
    REQUIRE(types.size() == 10);
    CHECK(types[0] == "     128");
    CHECK(types[2] == "     126");
    CHECK(types[4] == "     126");
    CHECK(types[6] == "     142");
    CHECK(types[8] == "     144");
    const std::string& terminate = lines.back();
    CHECK(terminate[72] == 'T');
    CHECK(std::stoi(terminate.substr(17, 7)) == 10);
    CHECK(std::stoi(terminate.substr(25, 7)) == parameterLines);
}

TEST_CASE("the offset makes every perimeter bigger, and Box covers the scan's box")
{
    const Mesh grid = HeightMesh(-25, 25, -25, 25, 100, 100, [](double x, double y) { return 0.002 * (x * x + y * y); });
    std::vector<int> disc;
    for (size_t t = 0; t < grid.triangles.size(); ++t)
    {
        const auto& tri = grid.triangles[t];
        const Point3 middle = (grid.vertices[tri[0]] + grid.vertices[tri[1]] + grid.vertices[tri[2]]) / 3.0;
        if (middle.x * middle.x + middle.y * middle.y < 15.0 * 15.0)
            disc.push_back(static_cast<int>(t));
    }
    SurfaceFitOptions options;
    options.base = SurfaceBase::XY;

    // Rectangle: the disc's square, 2% more, then the offset.
    options.perimeter = SurfacePerimeter::Rectangle;
    const SurfaceFitResult plain = FitScanSurface(grid, disc, options);
    options.offset = 4.0;
    const SurfaceFitResult offset = FitScanSurface(grid, disc, options);
    REQUIRE(plain.ok);
    REQUIRE(offset.ok);
    CHECK(offset.s1 - offset.s0 == doctest::Approx(plain.s1 - plain.s0 + 8.0));
    CHECK(offset.t1 - offset.t0 == doctest::Approx(plain.t1 - plain.t0 + 8.0));

    // Selection: the circle of radius 15 grows to about 19.
    options.perimeter = SurfacePerimeter::Selection;
    const SurfaceFitResult grown = FitScanSurface(grid, disc, options);
    REQUIRE(grown.ok);
    REQUIRE(grown.outline.size() >= 12);
    for (const Point3& p : grown.outline)
        CHECK(std::sqrt(p.x * p.x + p.y * p.y) == doctest::Approx(19.0).epsilon(0.04));

    // Box: the rectangle round a box from -40 to 30 in X and -10 to 50 in Y, and the offset.
    options.perimeter = SurfacePerimeter::Box;
    options.offset = 1.0;
    for (int i = 0; i < 8; ++i)
        options.boxCorners.push_back({ (i & 1) ? 30.0 : -40.0, (i & 2) ? 50.0 : -10.0, (i & 4) ? 5.0 : -5.0 });
    const SurfaceFitResult box = FitScanSurface(grid, disc, options);
    REQUIRE(box.ok);
    CHECK(box.outline.empty());
    CHECK(box.s0 + box.frame.origin.x < -41.0);
    CHECK(box.s1 + box.frame.origin.x > 31.0);
    CHECK(box.t0 + box.frame.origin.y < -11.0);
    CHECK(box.t1 + box.frame.origin.y > 51.0);
    CHECK(box.s1 + box.frame.origin.x < 34.0);

    options.boxCorners.clear();
    CHECK_FALSE(FitScanSurface(grid, disc, options).ok);
}

TEST_CASE("Projection trims to the whole scan's outline, though only the selection is fitted")
{
    // A 50 x 50 square of scan, with a disc in its middle selected.
    const Mesh grid = HeightMesh(-25, 25, -25, 25, 100, 100, [](double x, double y) { return 0.002 * (x * x + y * y); });
    std::vector<int> disc;
    for (size_t t = 0; t < grid.triangles.size(); ++t)
    {
        const auto& tri = grid.triangles[t];
        const Point3 middle = (grid.vertices[tri[0]] + grid.vertices[tri[1]] + grid.vertices[tri[2]]) / 3.0;
        if (middle.x * middle.x + middle.y * middle.y < 10.0 * 10.0)
            disc.push_back(static_cast<int>(t));
    }
    SurfaceFitOptions options;
    options.base = SurfaceBase::XY;
    options.perimeter = SurfacePerimeter::Projection;
    options.offset = 2.0;
    const SurfaceFitResult result = FitScanSurface(grid, disc, options);
    REQUIRE(result.ok);
    REQUIRE(result.outline.size() >= 4);

    // The outline is the scan's square grown by 2 (rounded corners), not the disc.
    double area = 0.0, farthest = 0.0;
    for (size_t i = 0; i < result.outline.size(); ++i)
    {
        const Point3& a = result.outline[i];
        const Point3& b = result.outline[(i + 1) % result.outline.size()];
        area += a.x * b.y - b.x * a.y;
        farthest = std::max({ farthest, std::fabs(a.x), std::fabs(a.y) });
    }
    area *= 0.5;
    const double expected = 54.0 * 54.0 - (4.0 - 3.14159265) * 2.0 * 2.0;
    CHECK(area == doctest::Approx(expected).epsilon(0.03));
    CHECK(farthest == doctest::Approx(27.0).epsilon(0.02));
    CHECK(result.s1 - result.s0 > 54.0);
    // The fit's numbers are the selection's alone.
    CHECK(result.samples < 2000);
}

TEST_CASE("Smooth Area stops at a box's edge, and Brush takes a disc")
{
    // A box with each face split into a 4 x 4 grid of squares.
    Mesh box;
    auto face = [&](Point3 origin, Point3 a, Point3 b) {
        const int base = static_cast<int>(box.vertices.size());
        for (int j = 0; j <= 4; ++j)
            for (int i = 0; i <= 4; ++i)
                box.vertices.push_back(origin + a * (i / 4.0) + b * (j / 4.0));
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i)
            {
                const int p = base + i + j * 5;
                box.triangles.push_back({ p, p + 1, p + 6 });
                box.triangles.push_back({ p, p + 6, p + 5 });
            }
    };
    face({ 0, 0, 10 }, { 10, 0, 0 }, { 0, 10, 0 });       // top
    face({ 0, 0, 0 }, { 0, 10, 0 }, { 10, 0, 0 });        // bottom
    face({ 0, 0, 0 }, { 10, 0, 0 }, { 0, 0, 10 });        // front
    face({ 0, 10, 0 }, { 0, 0, 10 }, { 10, 0, 0 });       // back
    face({ 0, 0, 0 }, { 0, 0, 10 }, { 0, 10, 0 });        // left
    face({ 10, 0, 0 }, { 0, 10, 0 }, { 0, 0, 10 });       // right
    // Join the corners that are the same point.
    Mesh joined;
    std::map<std::tuple<long, long, long>, int> seen;
    std::vector<int> remap;
    for (const Point3& p : box.vertices)
    {
        const auto key = std::make_tuple(std::lround(p.x * 100), std::lround(p.y * 100), std::lround(p.z * 100));
        auto it = seen.find(key);
        if (it == seen.end())
        {
            it = seen.insert({ key, static_cast<int>(joined.vertices.size()) }).first;
            joined.vertices.push_back(p);
        }
        remap.push_back(it->second);
    }
    for (const auto& tri : box.triangles)
        joined.triangles.push_back({ remap[tri[0]], remap[tri[1]], remap[tri[2]] });

    const auto neighbours = TriangleNeighbours(joined);
    const std::vector<int> top = GrowSmoothArea(joined, neighbours, 0, 15.0);
    CHECK(top.size() == 32);
    const std::vector<int> all = GrowSmoothArea(joined, neighbours, 0, 95.0);
    CHECK(all.size() == joined.triangles.size());

    // Brush on a flat grid: everything near the centre, nothing far from it.
    const Mesh flat = HeightMesh(-10, 10, -10, 10, 40, 40, [](double, double) { return 0.0; });
    const auto flatNeighbours = TriangleNeighbours(flat);
    int seed = -1;
    for (size_t t = 0; t < flat.triangles.size() && seed < 0; ++t)
        for (int k = 0; k < 3; ++k)
            if (Length(flat.vertices[flat.triangles[t][k]]) < 1e-9)
                seed = static_cast<int>(t);
    REQUIRE(seed >= 0);
    const std::vector<int> brushed = BrushArea(flat, flatNeighbours, seed, { 0, 0, 0 }, 4.0);
    size_t expected = 0;
    for (size_t t = 0; t < flat.triangles.size(); ++t)
    {
        const auto& tri = flat.triangles[t];
        const Point3 middle = (flat.vertices[tri[0]] + flat.vertices[tri[1]] + flat.vertices[tri[2]]) / 3.0;
        const bool selected = std::binary_search(brushed.begin(), brushed.end(), static_cast<int>(t));
        if (Length(middle) < 3.5)
        {
            CHECK(selected);
            ++expected;
        }
        if (Length(middle) > 4.6)
            CHECK_FALSE(selected);
    }
    CHECK(brushed.size() >= expected);
}

TEST_CASE("surface fit timing" * doctest::skip())
{
    const Mesh mesh = HeightMesh(0, 150, 0, 120, 300, 240, [](double x, double y) {
        return 4.0 * std::sin(x / 7.0) * std::cos(y / 9.0) + 0.001 * x * y;
    });
    SurfaceFitOptions options;
    options.base = SurfaceBase::XY;
    options.perimeter = SurfacePerimeter::Selection;
    options.tolerance = 0.0001;     // so it goes to the most control points
    const auto start = std::chrono::steady_clock::now();
    const SurfaceFitResult result = FitScanSurface(mesh, All(mesh), options);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(result.ok);
    std::printf("surface fit: %zu samples, %d x %d control points, largest %.4f, 99%% %.4f, RMS %.4f mm, %.2f s\n",
                result.samples, result.surface.countU, result.surface.countV, result.largest, result.within99,
                result.rms, seconds);
}
