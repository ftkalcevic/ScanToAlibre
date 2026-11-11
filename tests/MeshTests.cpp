// MeshTests.cpp : reading STL, simplifying, picking and the view projection.

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Mesh.h"
#include "ViewProjection.h"

using namespace scancore;

namespace
{
    // A unit cube's 12 triangles, outward facing, as corner coordinates.
    std::vector<std::array<Point3, 3>> CubeTriangles(double size)
    {
        auto c = [size](int i) { return Point3{ (i & 1) * size, ((i >> 1) & 1) * size, ((i >> 2) & 1) * size }; };
        const int faces[6][4] = {
            { 0, 2, 3, 1 },     // -Z
            { 4, 5, 7, 6 },     // +Z
            { 0, 1, 5, 4 },     // -Y
            { 2, 6, 7, 3 },     // +Y
            { 0, 4, 6, 2 },     // -X
            { 1, 3, 7, 5 },     // +X
        };
        std::vector<std::array<Point3, 3>> out;
        for (const auto& f : faces)
        {
            out.push_back({ c(f[0]), c(f[1]), c(f[2]) });
            out.push_back({ c(f[0]), c(f[2]), c(f[3]) });
        }
        return out;
    }

    std::string BinaryStl(const std::vector<std::array<Point3, 3>>& triangles, const std::string& header)
    {
        std::string bytes(80, ' ');
        std::memcpy(&bytes[0], header.data(), std::min<size_t>(header.size(), 80));
        const uint32_t count = static_cast<uint32_t>(triangles.size());
        bytes.append(reinterpret_cast<const char*>(&count), 4);
        for (const auto& t : triangles)
        {
            float values[12] = {};
            for (int k = 0; k < 3; ++k)
            {
                values[3 + 3 * k] = static_cast<float>(t[k].x);
                values[4 + 3 * k] = static_cast<float>(t[k].y);
                values[5 + 3 * k] = static_cast<float>(t[k].z);
            }
            bytes.append(reinterpret_cast<const char*>(values), sizeof(values));
            bytes.append(2, '\0');
        }
        return bytes;
    }

    std::string AsciiStl(const std::vector<std::array<Point3, 3>>& triangles)
    {
        std::string text = "solid cube\n";
        for (const auto& t : triangles)
        {
            text += "  facet normal 0 0 0\n    outer loop\n";
            for (const Point3& p : t)
                text += "      vertex " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z) + "\n";
            text += "    endloop\n  endfacet\n";
        }
        return text + "endsolid cube\n";
    }

    // A UV sphere of the given radius about the origin.
    Mesh Sphere(double radius, int rings, int segments)
    {
        Mesh mesh;
        const double pi = 3.14159265358979323846;
        mesh.vertices.push_back({ 0, 0, radius });
        for (int r = 1; r < rings; ++r)
        {
            const double a = pi * r / rings;
            for (int s = 0; s < segments; ++s)
            {
                const double b = 2 * pi * s / segments;
                mesh.vertices.push_back({ radius * std::sin(a) * std::cos(b), radius * std::sin(a) * std::sin(b),
                                          radius * std::cos(a) });
            }
        }
        mesh.vertices.push_back({ 0, 0, -radius });
        const int bottom = static_cast<int>(mesh.vertices.size()) - 1;
        auto at = [segments](int ring, int s) { return 1 + (ring - 1) * segments + (s % segments); };
        for (int s = 0; s < segments; ++s)
            mesh.triangles.push_back({ 0, at(1, s), at(1, s + 1) });
        for (int r = 1; r < rings - 1; ++r)
        {
            for (int s = 0; s < segments; ++s)
            {
                mesh.triangles.push_back({ at(r, s), at(r + 1, s), at(r + 1, s + 1) });
                mesh.triangles.push_back({ at(r, s), at(r + 1, s + 1), at(r, s + 1) });
            }
        }
        for (int s = 0; s < segments; ++s)
            mesh.triangles.push_back({ bottom, at(rings - 1, s + 1), at(rings - 1, s) });
        return mesh;
    }

    double DistanceToLine(const Point3& p, const Ray& line)
    {
        return Length(Cross(p - line.origin, line.direction));
    }
}

TEST_CASE("binary STL is read, corners joined, even when its header starts with solid")
{
    const std::string bytes = BinaryStl(CubeTriangles(10.0), "solid but really binary");
    Mesh mesh;
    std::string message;
    StlReadInfo info;
    REQUIRE(ParseStl(bytes, mesh, message, &info));
    CHECK(info.binary);
    CHECK(info.fileTriangles == 12);
    CHECK(mesh.triangles.size() == 12);
    CHECK(mesh.vertices.size() == 8);
    const Box3 box = Bounds(mesh);
    CHECK(box.low.x == 0.0);
    CHECK(box.high.z == 10.0);
    CHECK(SurfaceArea(mesh) == doctest::Approx(600.0));
}

TEST_CASE("a mesh written as binary STL reads back the same, even with a header starting solid")
{
    Mesh mesh;
    std::string message;
    REQUIRE(ParseStl(BinaryStl(CubeTriangles(10.0), ""), mesh, message));
    const std::string bytes = WriteBinaryStl(mesh, "solid ScanToAlibre");
    CHECK(bytes.size() == 84 + 50 * 12);
    Mesh back;
    StlReadInfo info;
    REQUIRE(ParseStl(bytes, back, message, &info));
    CHECK(info.binary);
    CHECK(back.vertices.size() == 8);
    CHECK(back.triangles == mesh.triangles);
    CHECK(SurfaceArea(back) == doctest::Approx(600.0));
}

TEST_CASE("ASCII STL is read the same as binary")
{
    Mesh mesh;
    std::string message;
    StlReadInfo info;
    REQUIRE(ParseStl(AsciiStl(CubeTriangles(10.0)), mesh, message, &info));
    CHECK_FALSE(info.binary);
    CHECK(mesh.triangles.size() == 12);
    CHECK(mesh.vertices.size() == 8);
}

TEST_CASE("files that aren't STL, or have no triangles, are refused")
{
    Mesh mesh;
    std::string message;
    CHECK_FALSE(ParseStl("hello world, this is not an STL file at all", mesh, message));
    CHECK_FALSE(message.empty());
    CHECK_FALSE(ParseStl("solid empty\nendsolid empty\n", mesh, message));
    CHECK_FALSE(ParseStl("solid bad\nfacet normal 0 0 1\nouter loop\nvertex 1 2\nendloop\nendfacet\n", mesh, message));
}

TEST_CASE("degenerate triangles are left out")
{
    auto triangles = CubeTriangles(10.0);
    triangles.push_back({ Point3{ 1, 1, 1 }, Point3{ 1, 1, 1 }, Point3{ 2, 2, 2 } });
    Mesh mesh;
    std::string message;
    StlReadInfo info;
    REQUIRE(ParseStl(BinaryStl(triangles, ""), mesh, message, &info));
    CHECK(info.degenerate == 1);
    CHECK(mesh.triangles.size() == 12);
}

TEST_CASE("vertex normals of a sphere point outwards")
{
    const Mesh sphere = Sphere(20.0, 30, 60);
    const std::vector<Point3> normals = VertexNormals(sphere);
    for (size_t i = 0; i < sphere.vertices.size(); ++i)
        CHECK(Dot(normals[i], Normalised(sphere.vertices[i])) > 0.99);
}

TEST_CASE("simplifying keeps the shape within about a cell and under the triangle limit")
{
    const Mesh sphere = Sphere(20.0, 200, 400);    // about 160,000 triangles
    REQUIRE(sphere.triangles.size() > 150000);
    const Mesh simple = SimplifyForDisplay(sphere, 20000);
    CHECK(simple.triangles.size() <= 20000);
    CHECK(simple.triangles.size() > 5000);
    for (const Point3& p : simple.vertices)
        CHECK(std::fabs(Length(p) - 20.0) < 1.5);

    // Small enough already: unchanged.
    const Mesh same = SimplifyForDisplay(sphere, 1000000);
    CHECK(same.triangles.size() == sphere.triangles.size());
}

TEST_CASE("a line through a cube hits its nearest face")
{
    Mesh mesh;
    std::string message;
    REQUIRE(ParseStl(BinaryStl(CubeTriangles(10.0), ""), mesh, message));

    // Looking down -Z from far above, and with the origin inside the cube.
    for (double z0 : { 100.0, 5.0, -100.0 })
    {
        const RayHit hit = IntersectLine(mesh, { { 3.0, 4.0, z0 }, { 0.0, 0.0, -1.0 } });
        REQUIRE(hit.hit);
        CHECK(hit.point.z == doctest::Approx(10.0));
        CHECK(hit.point.x == doctest::Approx(3.0));
    }

    const RayHit miss = IntersectLine(mesh, { { 30.0, 4.0, 100.0 }, { 0.0, 0.0, -1.0 } });
    CHECK_FALSE(miss.hit);
}

TEST_CASE("an orthographic view is fitted exactly and a click becomes the line along the view")
{
    // Screen u right, v down, 4 pixels a millimetre, looking along a tilted direction.
    const Point3 right = Normalised({ 1.0, 0.3, 0.0 });
    const Point3 forward = Normalised({ 0.2, -0.5, -1.0 });
    const Point3 up = Normalised(Cross(right, forward));
    const Point3 r2 = Normalised(Cross(forward, up));
    auto project = [&](const Point3& p) { return ScreenPoint{ 640.0 + 4.0 * Dot(p, r2), 400.0 - 4.0 * Dot(p, up) }; };

    Box3 box;
    box.Add({ 8, -32, -248 });
    box.Add({ 46, -8, -181 });
    const std::vector<Point3> world = ProjectionSamplePoints(box);
    std::vector<ScreenPoint> screen;
    for (const Point3& p : world)
        screen.push_back(project(p));

    ViewProjection view;
    REQUIRE(view.Fit(world, screen));
    CHECK(view.LargestError() < 1e-6);

    const Point3 target{ 20.0, -15.0, -200.0 };
    const ScreenPoint s = project(target);
    const Ray line = view.LineThrough(s.u, s.v);
    CHECK(DistanceToLine(target, line) < 1e-6);
    CHECK(Dot(line.direction, forward) == doctest::Approx(1.0));
    CHECK(Dot(view.IntoScreen(target), forward) == doctest::Approx(1.0));
}

TEST_CASE("a perspective view is fitted exactly and a click's line passes through the eye")
{
    const Point3 eye{ 50.0, -100.0, 120.0 };
    const Point3 forward = Normalised(Point3{ 25.0, -20.0, -210.0 } - eye);
    const Point3 right = Normalised(Cross(forward, { 0, 0, 1 }));
    const Point3 up = Cross(right, forward);
    auto project = [&](const Point3& p) {
        const Point3 d = p - eye;
        const double depth = Dot(d, forward);
        return ScreenPoint{ 640.0 + 900.0 * Dot(d, right) / depth, 400.0 - 900.0 * Dot(d, up) / depth };
    };

    Box3 box;
    box.Add({ 8, -32, -248 });
    box.Add({ 46, -8, -181 });
    const std::vector<Point3> world = ProjectionSamplePoints(box);
    std::vector<ScreenPoint> screen;
    for (const Point3& p : world)
        screen.push_back(project(p));

    ViewProjection view;
    REQUIRE(view.Fit(world, screen));
    CHECK(view.LargestError() < 1e-4);

    const Point3 target{ 30.0, -12.0, -190.0 };
    const ScreenPoint s = project(target);
    const Ray line = view.LineThrough(s.u, s.v);
    CHECK(DistanceToLine(target, line) < 1e-4);
    CHECK(DistanceToLine(eye, line) < 1e-3);
    CHECK(Dot(line.direction, Normalised(target - eye)) == doctest::Approx(1.0));
    CHECK(Dot(view.IntoScreen(target), Normalised(target - eye)) == doctest::Approx(1.0));
}

// Skipped unless asked for: set SCANTOALIBRE_STL to a scan and run
//   ScanCoreTests.exe -tc="real scan timing*" --no-skip
TEST_CASE("real scan timing" * doctest::skip())
{
    char* value = nullptr;
    size_t length = 0;
    REQUIRE(_dupenv_s(&value, &length, "SCANTOALIBRE_STL") == 0);
    REQUIRE(value != nullptr);
    const std::string path(value);
    free(value);

    auto now = [] { return std::chrono::steady_clock::now(); };
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };

    const auto t0 = now();
    Mesh mesh;
    std::string message;
    StlReadInfo info;
    REQUIRE(ReadStl(path, mesh, message, &info));
    const auto t1 = now();
    const std::vector<Point3> normals = VertexNormals(mesh);
    const auto t2 = now();
    const Mesh simple = SimplifyForDisplay(mesh, 300000);
    const auto t3 = now();
    const Box3 box = Bounds(mesh);
    const RayHit hit = IntersectLine(mesh, { box.Centre(), { 0.0, 0.0, -1.0 } });
    const auto t4 = now();

    MESSAGE("triangles " << mesh.triangles.size() << ", vertices " << mesh.vertices.size() << ", degenerate "
                         << info.degenerate);
    MESSAGE("read " << ms(t0, t1) << " ms, normals " << ms(t1, t2) << " ms, simplify to "
                    << simple.triangles.size() << " in " << ms(t2, t3) << " ms, pick " << ms(t3, t4) << " ms, hit "
                    << hit.hit);
}

TEST_CASE("a click on a cube seen from above picks its top, not its bottom")
{
    Mesh mesh;
    std::string message;
    REQUIRE(ParseStl(BinaryStl(CubeTriangles(10.0), ""), mesh, message));

    // Looking down -Z: screen right is +X, screen down is -Y.
    auto project = [](const Point3& p) { return ScreenPoint{ 500.0 + 20.0 * p.x, 500.0 - 20.0 * p.y }; };
    const std::vector<Point3> world = ProjectionSamplePoints(Bounds(mesh));
    std::vector<ScreenPoint> screen;
    for (const Point3& p : world)
        screen.push_back(project(p));
    ViewProjection view;
    REQUIRE(view.Fit(world, screen));

    const ScreenPoint s = project({ 3.0, 4.0, 0.0 });
    const RayHit hit = IntersectLine(mesh, view.LineThrough(s.u, s.v));
    REQUIRE(hit.hit);
    CHECK(hit.point.z == doctest::Approx(10.0));

    // Looking up +Z from below, with screen right still +X: screen down is +Y.
    auto below = [](const Point3& p) { return ScreenPoint{ 500.0 + 20.0 * p.x, 500.0 + 20.0 * p.y }; };
    screen.clear();
    for (const Point3& p : world)
        screen.push_back(below(p));
    REQUIRE(view.Fit(world, screen));
    const ScreenPoint t = below({ 3.0, 4.0, 0.0 });
    const RayHit under = IntersectLine(mesh, view.LineThrough(t.u, t.v));
    REQUIRE(under.hit);
    CHECK(under.point.z == doctest::Approx(0.0));
}
