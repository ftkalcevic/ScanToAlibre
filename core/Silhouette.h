// Silhouette.h : the scan's outline seen along a plane's normal (6 Silhouette).
//
// A slice at one height misses tabs and flanges at other heights; the
// silhouette catches them. Every separate piece and every through-hole is
// kept. The result is Section's own, so the fitting, design intent, drawing and
// sketch writing work on it unchanged.

#pragma once

#include "Section.h"

namespace scancore
{
    struct SilhouetteInfo
    {
        double cell = 0.0;              // the grid's cell, mm
        size_t pieces = 0;              // outer loops
        size_t holes = 0;               // through-holes kept
        size_t filledHoles = 0;         // holes smaller than smallestHole, filled
        size_t droppedPieces = 0;       // pieces smaller than smallestHole, left out
        size_t snapped = 0, points = 0; // loop points moved onto the mesh's outline edges, of all
    };

    // The scan projected along the frame's normal onto its plane:
    // 1. every triangle drawn on a grid (cells the larger of the tolerance and
    //    a 4000th of the extent), gaps of a cell closed;
    // 2. empty regions the outside can't reach are holes; those smaller in area
    //    than a circle of smallestHole (mm across) are filled, and pieces that
    //    small are left out;
    // 3. every boundary loop traced (pieces anticlockwise, holes clockwise),
    //    each point moved onto the nearest outline edge of the mesh within 1.5
    //    cells (edges where the triangles either side face differently along
    //    the normal, and open edges), the rest smoothed;
    // 4. each loop fitted with lines and arcs (FitProfile) and simplified for
    //    the reference, as Section does. The slices are the loops on the plane.
    SectionResult SilhouetteMesh(const Mesh& mesh, const SectionFrame& frame, double tolerance,
                                 double referenceTolerance, double smallestHole, SilhouetteInfo* info = nullptr,
                                 bool arcs = true);
}
