// Smooth.h : smooths a scan's surface noise.
//
// Laplacian: each vertex moves towards the middle of its neighbours. Simple and
//   strong, but it shrinks the shape and rounds its edges.
// Taubin (1995): a Laplacian step forwards then a slightly larger one back,
//   which smooths without shrinking.
// Bilateral (normal filtering: Sun et al. 2007, Zheng et al. 2011): each
//   triangle's normal is averaged with its neighbours', ignoring those that face
//   a different way, then the vertices are moved to fit the filtered normals.
//   Flat faces become flat while sharp edges and corners stay, and curved
//   areas hardly shrink. Suits machined parts.

#pragma once

#include <cstddef>

#include "Mesh.h"

namespace scancore
{
    enum class SmoothMethod
    {
        Laplacian,
        Taubin,
        Bilateral,
    };

    struct SmoothOptions
    {
        SmoothMethod method = SmoothMethod::Taubin;
        int passes = 5;
        double strength = 0.5;          // 0 to 1: how far each pass moves
        bool keepOpenEdges = true;      // vertices on open edges stay put
    };

    struct SmoothResult
    {
        double largestMove = 0.0;       // mm, of any vertex over all passes
        double averageMove = 0.0;       // mm
    };

    // Same vertices and triangles, moved.
    Mesh Smooth(const Mesh& mesh, const SmoothOptions& options, SmoothResult* result = nullptr);

    // The vertices on open edges (edges with one triangle).
    std::vector<bool> OpenEdgeVertices(const Mesh& mesh);

    // Each vertex's neighbours along edges, sorted.
    std::vector<std::vector<int>> VertexNeighbours(const Mesh& mesh);
}
