// Decimate.h : reduces a mesh's triangles by quadric edge collapse
// (Garland and Heckbert, 1997).
//
// Each vertex carries the sum of the squared distances to its triangles' planes
// (a quadric). Edges are collapsed cheapest first, each to the point that
// minimises the joined quadric, so flat areas lose triangles first and edges
// and curves keep theirs. A collapse is refused if it would fold a triangle
// over or join the surface to itself (the link condition).

#pragma once

#include <cstddef>

#include "Mesh.h"

namespace scancore
{
    struct DecimateOptions
    {
        size_t targetTriangles = 0;     // stop at or below this many
        double maxError = 0.0;          // mm; stop before a collapse moves the surface further. 0: no limit
        bool keepOpenEdges = true;      // open (boundary) edges stay where they are
    };

    struct DecimateResult
    {
        size_t trianglesBefore = 0;
        size_t trianglesAfter = 0;
        size_t collapses = 0;
        size_t refused = 0;             // collapses refused by the fold and link checks
        double largestError = 0.0;      // mm, the error estimate of the last collapse made
        bool stoppedByError = false;    // stopped by maxError before reaching the target
    };

    Mesh Decimate(const Mesh& mesh, const DecimateOptions& options, DecimateResult* result = nullptr);
}
