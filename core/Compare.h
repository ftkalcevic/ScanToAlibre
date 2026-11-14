// Compare.h : distances from points to a triangle mesh, for comparing a scan
// with the part (7 Compare) and for scoring symmetry planes.

#pragma once

#include <cstdint>
#include <vector>

#include "Mesh.h"

namespace scancore
{
    // The closest point on a triangle to p.
    Point3 ClosestPointOnTriangle(const Point3& p, const Point3& a, const Point3& b, const Point3& c);

    // A mesh's triangles in a uniform grid, for finding the nearest point on the
    // mesh to a point within a search distance. The cell is at least cell (mm),
    // grown so the grid has no more than about four million cells. Searches go
    // out ring by ring, so a cell about the usual search distance or less is best.
    class TriangleGrid
    {
    public:
        TriangleGrid(const Mesh& mesh, double cell);

        struct Nearest
        {
            bool found = false;
            Point3 point;               // on the mesh
            int triangle = -1;
            double distance = 0.0;      // from the query point
            double signedDistance = 0.0;// positive on the side the triangle faces (outside)
        };

        // The nearest point on the mesh within maxDistance of p. Where several
        // triangles are about as near (an edge or a corner), the sign comes from
        // the one p is most squarely in front of or behind.
        Nearest Find(const Point3& p, double maxDistance) const;

        const Mesh& GetMesh() const { return m_mesh; }
        const Point3& Normal(int triangle) const { return m_normals[static_cast<size_t>(triangle)]; }

    private:
        const Mesh& m_mesh;
        std::vector<Point3> m_normals;          // unit, per triangle
        Point3 m_low;
        double m_cell = 1.0;
        int m_count[3] = { 1, 1, 1 };
        std::vector<uint32_t> m_start;          // per cell, into m_items; one more than the cells
        std::vector<int> m_items;               // triangles

        int64_t CellIndex(int i, int j, int k) const
        {
            return (static_cast<int64_t>(k) * m_count[1] + j) * m_count[0] + i;
        }
    };

    // Each point's signed distance (mm) from the nearest point on the part:
    // positive outside it. NaN where nothing is within range. On all cores.
    std::vector<float> Deviations(const std::vector<Point3>& points, const Mesh& part, double range);

    // A part's facets as Alibre gives them (9 numbers a triangle, in model
    // units) as a mesh in mm: corners within 1e-6 mm joined, triangles with two
    // corners the same left out, and turned outward by the signed volume if
    // they wind inward (Alibre's winding is unproven).
    Mesh MeshFromFacets(const std::vector<double>& values, double millimetresPerUnit);

    struct DeviationStats
    {
        size_t compared = 0;            // with a value
        size_t notCompared = 0;         // NaN: nothing of the part within range
        double largestPositive = 0.0;   // 0 or more
        double largestNegative = 0.0;   // 0 or less
        double rms = 0.0;
        double within = 0.0;            // fraction of those compared within the tolerance either way
    };
    DeviationStats DeviationStatistics(const std::vector<float>& values, double tolerance);

    // Colour bands, symmetric about green: dark blue, blue, light blue (inside
    // the part, from -range to -tolerance in thirds of the range), green (within
    // the tolerance), yellow, orange, red (outside, to +range), then grey (NaN,
    // or beyond the range).
    const int kDeviationBands = 8;
    const int kDeviationGrey = 7;
    int DeviationBand(double value, double tolerance, double range);
}
