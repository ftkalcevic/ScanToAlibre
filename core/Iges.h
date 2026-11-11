// Iges.h : writes surfaces as IGES 5.3 files, for Alibre to insert.
//
// Only what the add-on needs: one rational B-spline surface (entity 128) in mm.
// Lines are 80 columns: 72 of data, the section letter, and a 7-digit sequence.

#pragma once

#include <string>

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
}
