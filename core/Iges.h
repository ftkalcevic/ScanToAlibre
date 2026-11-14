// Iges.h : writes surfaces as IGES 5.3 files, for Alibre to insert.
//
// Only what the add-on needs: one rational B-spline surface (entity 128) in mm,
// optionally trimmed by one outer boundary (entities 126, 142 and 144).
// Lines are 80 columns: 72 of data, the section letter, and a 7-digit sequence.

#pragma once

#include <array>
#include <string>
#include <vector>

#include "BSplineSurface.h"

namespace scancore
{
    struct IgesHeader
    {
        std::string fileName = "surface.igs";
        std::string author;
        std::string organisation;
        // YYYYMMDD.HHNNSS
        std::string timestamp = "20260101.000000";
    };

    // A real number with a decimal point, as IGES wants, to 15 significant digits.
    std::string IgesReal(double value);

    // The surface must be valid (ValidateSurface).
    std::string WriteIgesSurface(const BSplineSurface& surface, const IgesHeader& header);

    // The surface trimmed by a closed outline, given in its parameters (u, v)
    // and in space (the same points on the surface), anticlockwise and not
    // repeating the first point: entity 128, two 126 polylines, 142 and 144.
    std::string WriteIgesTrimmedSurface(const BSplineSurface& surface,
                                        const std::vector<std::array<double, 2>>& outlineUV,
                                        const std::vector<Point3>& outline, const IgesHeader& header);
}
