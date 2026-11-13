// Reconstruct.h : makes a triangle mesh from a point cloud with normals.
//
// A signed distance is worked out at the corners of a grid near the points
// (Hoppe et al. 1992: each nearby point's height above its own tangent plane,
// averaged with Gaussian weights, so noise smooths out); it's zero on the
// surface, positive on the side the normals face. The surface is then drawn
// through the grid by surface nets: a vertex in each cell the surface crosses,
// at the average of where it crosses the cell's edges, and a quad (two
// triangles) for each grid edge it crosses. The grid reaches two cells from the
// points, so gaps between a scanner's lines of points close, but a surface
// vertex must be within 1.5 cells of a point, so no
// skirt runs past the scan's own edges or into holes; wider gaps stay open.
//
// Walls thinner than about two cells come out poorly (the two sides' points
// cancel); a finer cell helps.

#pragma once

#include <string>
#include <vector>

#include "Mesh.h"

namespace scancore
{
    struct ReconstructOptions
    {
        double cellSize = 0.0;          // mm; 0: from the points' spacing
        double smoothing = 1.0;         // the Gaussian's width, in cells
        size_t maxPointCells = 1000000; // the cell grows until the points fill at most this many cells
        int gapCells = 2;               // surface is made up to this many cells from the nearest point, so gaps
                                        // between the scanner's lines of points close but wider holes stay open
    };

    struct ReconstructResult
    {
        double spacing = 0.0;           // mm, from PointSpacing
        double cellSize = 0.0;          // mm, as used
        size_t points = 0;
        size_t gridCorners = 0;         // where the signed distance was worked out
        size_t triangles = 0;
    };

    // normals are unit vectors pointing out of the surface, one for each point.
    bool ReconstructSurface(const std::vector<Point3>& points, const std::vector<Point3>& normals,
                            const ReconstructOptions& options, Mesh& mesh, std::string& message,
                            ReconstructResult* result = nullptr);

    // The points' spacing: the side of the square each covers on the surface,
    // from a sample (the median, from the distance to the 12th nearest).
    double PointSpacing(const std::vector<Point3>& points);
}
