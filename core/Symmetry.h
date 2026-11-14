// Symmetry.h : finding a scan's plane of symmetry, and the plane halfway
// between two of its features (Orient's Find Symmetry and Mid Plane).

#pragma once

#include <array>
#include <string>
#include <vector>

#include "Orient.h"

namespace scancore
{
    class TriangleGrid;

    // How well a plane mirrors the scan onto itself, from points spread over its
    // surface by area: each point is mirrored and measured to the nearest point
    // on the scan.
    struct SymmetryScore
    {
        double rms = 0.0;               // mm, over the matched points
        double within = 0.0;            // fraction of all the points within the tolerance
        double matched = 0.0;           // fraction with something within the search distance
        // The rms with each distance capped at the tolerance, and the unmatched
        // counted at it: what candidates are ranked by, lowest first. Counting
        // within alone ranked planes tilted 2 degrees first on flat-sided parts,
        // as mirrored flat faces still land near the faces.
        double trimmed = 0.0;
    };

    struct SymmetryCandidate
    {
        Feature plane;                  // unit normal; the point is the box's centre moved onto the plane
        SymmetryScore score;
    };

    struct SymmetryOptions
    {
        size_t samples = 20000;
        double tolerance = 0.2;         // mm, for within
        double search = 0.0;            // mm; 0 for 3% of the box's diagonal, at least 1
        size_t refine = 4;              // how many of the best starting planes are refined
        int iterations = 30;
    };

    // Points on the surface, spread by area (the same for the same mesh).
    std::vector<Point3> SurfaceSamples(const Mesh& mesh, size_t count);

    SymmetryScore ScoreMirror(const TriangleGrid& grid, const std::vector<Point3>& samples, const Feature& plane,
                              double search, double tolerance);

    // Starting planes: the box's three centre planes, the planes through the
    // scan's centroid square to its principal directions, and planes through
    // the centroid and the box's centre square to each fitted plane's normal,
    // along each fitted axis, and containing each fitted axis square to the box's
    // directions. The best few are refined by mirror ICP: each point is paired
    // with the nearest point on the scan to its mirror image, and the plane is
    // moved to bring the mirrored points onto the scan's surface there
    // (point-to-plane, Gauss-Newton), until it moves less than 0.001 mm and
    // 0.001 degrees: each start for 8 steps, then the best of them for the rest
    // of the iterations. Pairs further than three times the rms, or the
    // tolerance if that's more, are left out.
    // Returns the refined planes, best (lowest trimmed rms) first, those that settled
    // on the same plane once.
    std::vector<SymmetryCandidate> FindSymmetryPlanes(const Mesh& mesh, const OrientedBox& box,
                                                      const std::vector<Feature>& fitted,
                                                      const SymmetryOptions& options = SymmetryOptions());

    // How well each world plane mirrors the scan, as it lies: XY (z = 0), YZ
    // (x = 0) and ZX (y = 0), not refined (for 5 Surface's Detect, after the scan
    // is aligned).
    std::array<SymmetryScore, 3> ScoreWorldPlanes(const Mesh& mesh,
                                                  const SymmetryOptions& options = SymmetryOptions());

    // The plane halfway between two planes (bisecting them, their normals
    // flipped to agree), two axes (square to the line joining them, through its
    // middle) or two points (likewise). Anything else, or two that coincide, is
    // refused with a message.
    bool MidPlane(const Feature& a, const Feature& b, Feature& plane, std::string& message);
}
