// CompareTests.cpp : the scan's distance from the part, its statistics and colour bands.

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <limits>
#include <random>

#include "Compare.h"

using namespace scancore;

namespace
{
    // A box's facets as Alibre gives them: 9 numbers a triangle, each face two
    // triangles, outward unless inward is set, in units of unit mm.
    std::vector<double> BoxFacets(double half, bool inward, double unit)
    {
        Point3 c[8];
        for (int i = 0; i < 8; ++i)
            c[i] = { (i & 1) ? half : -half, (i & 2) ? half : -half, (i & 4) ? half : -half };
        const int quads[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 },
                                  { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        std::vector<double> values;
        auto add = [&](int a, int b, int d) {
            for (int k : { a, inward ? d : b, inward ? b : d })
            {
                values.push_back(c[k].x / unit);
                values.push_back(c[k].y / unit);
                values.push_back(c[k].z / unit);
            }
        };
        for (const auto& q : quads)
        {
            add(q[0], q[1], q[2]);
            add(q[0], q[2], q[3]);
        }
        return values;
    }

    // Points on a cube's faces (half 10), kept clear of the edges by margin.
    std::vector<Point3> FacePoints(double half, double margin)
    {
        std::vector<Point3> points;
        for (int axis = 0; axis < 3; ++axis)
            for (double side : { -half, half })
                for (double a = -half + margin; a <= half - margin + 1e-9; a += 1.0)
                    for (double b = -half + margin; b <= half - margin + 1e-9; b += 1.0)
                    {
                        double p[3];
                        p[axis] = side;
                        p[(axis + 1) % 3] = a;
                        p[(axis + 2) % 3] = b;
                        points.push_back({ p[0], p[1], p[2] });
                    }
        return points;
    }
}

TEST_CASE("a box scan against a smaller and a larger box part")
{
    const std::vector<Point3> scan = FacePoints(10.0, 0.5);
    for (bool inward : { false, true })
    {
        // Facets in cm, as a new Alibre part's are.
        const Mesh smaller = MeshFromFacets(BoxFacets(9.8, inward, 10.0), 10.0);
        CHECK(smaller.vertices.size() == 8);
        CHECK(smaller.triangles.size() == 12);
        const std::vector<float> outside = Deviations(scan, smaller, 1.0);
        const Mesh larger = MeshFromFacets(BoxFacets(10.2, inward, 10.0), 10.0);
        const std::vector<float> inside = Deviations(scan, larger, 1.0);
        for (size_t i = 0; i < scan.size(); ++i)
        {
            CHECK(outside[i] == doctest::Approx(0.2).epsilon(1e-4));
            CHECK(inside[i] == doctest::Approx(-0.2).epsilon(1e-4));
        }
    }
}

TEST_CASE("points beyond the range aren't compared")
{
    const Mesh part = MeshFromFacets(BoxFacets(10.0, false, 1.0), 1.0);
    const std::vector<float> values = Deviations({ { 0, 0, 10.5 }, { 0, 0, 12.0 }, { 0, 0, 30.0 }, { 0, 0, 9.0 } },
                                                 part, 1.0);
    CHECK(values[0] == doctest::Approx(0.5));
    CHECK(std::isnan(values[1]));
    CHECK(std::isnan(values[2]));
    CHECK(values[3] == doctest::Approx(-1.0));
}

TEST_CASE("deviation bands")
{
    const double tol = 0.1, range = 0.9;
    CHECK(DeviationBand(-0.9, tol, range) == 0);
    CHECK(DeviationBand(-0.61, tol, range) == 0);
    CHECK(DeviationBand(-0.59, tol, range) == 1);
    CHECK(DeviationBand(-0.31, tol, range) == 1);
    CHECK(DeviationBand(-0.29, tol, range) == 2);
    CHECK(DeviationBand(-0.11, tol, range) == 2);
    CHECK(DeviationBand(-0.1, tol, range) == 3);
    CHECK(DeviationBand(0.0, tol, range) == 3);
    CHECK(DeviationBand(0.1, tol, range) == 3);
    CHECK(DeviationBand(0.11, tol, range) == 4);
    CHECK(DeviationBand(0.29, tol, range) == 4);
    CHECK(DeviationBand(0.31, tol, range) == 5);
    CHECK(DeviationBand(0.59, tol, range) == 5);
    CHECK(DeviationBand(0.61, tol, range) == 6);
    CHECK(DeviationBand(0.9, tol, range) == 6);
    CHECK(DeviationBand(0.91, tol, range) == kDeviationGrey);
    CHECK(DeviationBand(-0.91, tol, range) == kDeviationGrey);
    CHECK(DeviationBand(std::numeric_limits<double>::quiet_NaN(), tol, range) == kDeviationGrey);
}

TEST_CASE("deviation statistics")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const DeviationStats s = DeviationStatistics({ 0.05f, -0.3f, 0.4f, nan, 0.0f, nan }, 0.1);
    CHECK(s.compared == 4);
    CHECK(s.notCompared == 2);
    CHECK(s.largestPositive == doctest::Approx(0.4));
    CHECK(s.largestNegative == doctest::Approx(-0.3));
    CHECK(s.rms == doctest::Approx(std::sqrt((0.0025 + 0.09 + 0.16) / 4.0)).epsilon(1e-6));
    CHECK(s.within == doctest::Approx(0.5));

    const DeviationStats none = DeviationStatistics({ nan }, 0.1);
    CHECK(none.compared == 0);
    CHECK(none.rms == 0.0);
}

// Skipped unless asked for:
//   ScanCoreTests.exe -tc="compare timing*" --no-skip
TEST_CASE("compare timing" * doctest::skip())
{
    // A part of about 50,000 triangles: a sphere, radius 40.
    const double kPi = 3.14159265358979323846;
    const int rings = 125, segments = 200;
    std::vector<double> facets;
    auto at = [&](int i, int j) {
        const double theta = kPi * i / rings, phi = 2.0 * kPi * j / segments;
        return Point3{ 40.0 * std::sin(theta) * std::cos(phi), 40.0 * std::sin(theta) * std::sin(phi),
                       40.0 * std::cos(theta) };
    };
    auto add = [&](const Point3& a, const Point3& b, const Point3& c) {
        for (const Point3& p : { a, b, c })
            facets.insert(facets.end(), { p.x, p.y, p.z });
    };
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < segments; ++j)
        {
            add(at(i, j), at(i + 1, j), at(i + 1, j + 1));
            add(at(i, j), at(i + 1, j + 1), at(i, j + 1));
        }
    const Mesh part = MeshFromFacets(facets, 1.0);
    std::mt19937 random(3);
    std::normal_distribution<double> gauss(0.0, 1.0);
    std::uniform_real_distribution<double> noise(-0.3, 0.3);
    std::vector<Point3> points(1000000);
    for (Point3& p : points)
    {
        const Point3 d = Normalised({ gauss(random), gauss(random), gauss(random) });
        p = d * (40.0 + noise(random));
    }
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<float> values = Deviations(points, part, 1.0);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const DeviationStats s = DeviationStatistics(values, 0.1);
    MESSAGE(part.triangles.size() << " triangles, " << points.size() << " points in " << seconds << " s; rms "
                                  << s.rms << ", " << 100.0 * s.within << "% within 0.1");
}
