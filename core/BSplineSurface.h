// BSplineSurface.h : a tensor-product B-spline (optionally rational) surface.

#pragma once

#include <string>
#include <vector>

#include "Geometry.h"

namespace scancore
{
    // Control points are stored u fastest: points[u + v * countU].
    // Knot vectors have count + degree + 1 values. No weights means polynomial.
    struct BSplineSurface
    {
        int degreeU = 3;
        int degreeV = 3;
        int countU = 0;
        int countV = 0;
        std::vector<double> knotsU;
        std::vector<double> knotsV;
        std::vector<Point3> points;
        std::vector<double> weights;
    };

    // On failure, message says what's wrong.
    bool ValidateSurface(const BSplineSurface& surface, std::string& message);

    // A clamped knot vector, uniform from 0 to 1 inside.
    std::vector<double> ClampedUniformKnots(int count, int degree);

    // The point at (u, v), by de Boor's algorithm. The surface must be valid.
    Point3 Evaluate(const BSplineSurface& surface, double u, double v);

    // A gentle bicubic hump over X 0 to 100 and Y 0 to 80 (mm), between Z 20
    // and about 30, for trying surface insertion in Alibre.
    BSplineSurface TestHumpSurface();
}
