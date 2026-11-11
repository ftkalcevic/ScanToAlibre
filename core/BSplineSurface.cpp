// BSplineSurface.cpp : a tensor-product B-spline (optionally rational) surface.

#include "BSplineSurface.h"

#include <algorithm>
#include <cmath>

namespace scancore
{
    namespace
    {
        // The knot span holding t, for clamped knots: degree <= span < count.
        int FindSpan(const std::vector<double>& knots, int count, int degree, double t)
        {
            if (t >= knots[count])
                return count - 1;
            if (t <= knots[degree])
                return degree;

            int low = degree;
            int high = count;
            while (high - low > 1)
            {
                const int middle = (low + high) / 2;
                if (t < knots[middle])
                    high = middle;
                else
                    low = middle;
            }
            return low;
        }

        // Basis function values N[span - degree .. span] at t (Piegl and Tiller A2.2).
        std::vector<double> Basis(const std::vector<double>& knots, int span, int degree, double t)
        {
            std::vector<double> n(degree + 1, 0.0);
            std::vector<double> left(degree + 1, 0.0);
            std::vector<double> right(degree + 1, 0.0);
            n[0] = 1.0;
            for (int j = 1; j <= degree; ++j)
            {
                left[j] = t - knots[span + 1 - j];
                right[j] = knots[span + j] - t;
                double saved = 0.0;
                for (int r = 0; r < j; ++r)
                {
                    const double denominator = right[r + 1] + left[j - r];
                    const double term = (denominator != 0.0) ? n[r] / denominator : 0.0;
                    n[r] = saved + right[r + 1] * term;
                    saved = left[j - r] * term;
                }
                n[j] = saved;
            }
            return n;
        }

        bool CheckKnots(const std::vector<double>& knots, int count, int degree, const char* name,
                        std::string& message)
        {
            if (knots.size() != static_cast<size_t>(count + degree + 1))
            {
                message = std::string(name) + " knot vector has " + std::to_string(knots.size()) +
                          " values; it needs " + std::to_string(count + degree + 1) + ".";
                return false;
            }
            for (size_t i = 1; i < knots.size(); ++i)
            {
                if (!(knots[i] >= knots[i - 1]))
                {
                    message = std::string(name) + " knots must not decrease.";
                    return false;
                }
            }
            if (!(knots[count] > knots[degree]))
            {
                message = std::string(name) + " knots have an empty range.";
                return false;
            }
            return true;
        }
    }

    bool ValidateSurface(const BSplineSurface& surface, std::string& message)
    {
        if (surface.degreeU < 1 || surface.degreeV < 1)
        {
            message = "Degrees must be at least 1.";
            return false;
        }
        if (surface.countU <= surface.degreeU || surface.countV <= surface.degreeV)
        {
            message = "Each direction needs more control points than its degree.";
            return false;
        }
        if (!CheckKnots(surface.knotsU, surface.countU, surface.degreeU, "The U", message) ||
            !CheckKnots(surface.knotsV, surface.countV, surface.degreeV, "The V", message))
            return false;

        const size_t count = static_cast<size_t>(surface.countU) * surface.countV;
        if (surface.points.size() != count)
        {
            message = "There are " + std::to_string(surface.points.size()) + " control points; " +
                      std::to_string(count) + " are needed.";
            return false;
        }
        if (!surface.weights.empty())
        {
            if (surface.weights.size() != count)
            {
                message = "There must be one weight for each control point.";
                return false;
            }
            for (double w : surface.weights)
            {
                if (!(w > 0.0))
                {
                    message = "Weights must be greater than 0.";
                    return false;
                }
            }
        }
        for (const Point3& p : surface.points)
        {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            {
                message = "A control point isn't a number.";
                return false;
            }
        }
        return true;
    }

    std::vector<double> ClampedUniformKnots(int count, int degree)
    {
        std::vector<double> knots;
        const int inner = count - degree;      // spans
        for (int i = 0; i <= degree; ++i)
            knots.push_back(0.0);
        for (int i = 1; i < inner; ++i)
            knots.push_back(static_cast<double>(i) / inner);
        for (int i = 0; i <= degree; ++i)
            knots.push_back(1.0);
        return knots;
    }

    Point3 Evaluate(const BSplineSurface& surface, double u, double v)
    {
        const int spanU = FindSpan(surface.knotsU, surface.countU, surface.degreeU, u);
        const int spanV = FindSpan(surface.knotsV, surface.countV, surface.degreeV, v);
        const std::vector<double> nu = Basis(surface.knotsU, spanU, surface.degreeU, u);
        const std::vector<double> nv = Basis(surface.knotsV, spanV, surface.degreeV, v);

        double x = 0.0, y = 0.0, z = 0.0, w = 0.0;
        for (int j = 0; j <= surface.degreeV; ++j)
        {
            for (int i = 0; i <= surface.degreeU; ++i)
            {
                const int index = (spanU - surface.degreeU + i) + (spanV - surface.degreeV + j) * surface.countU;
                const double weight = surface.weights.empty() ? 1.0 : surface.weights[index];
                const double b = nu[i] * nv[j] * weight;
                x += b * surface.points[index].x;
                y += b * surface.points[index].y;
                z += b * surface.points[index].z;
                w += b;
            }
        }
        return { x / w, y / w, z / w };
    }

    BSplineSurface TestHumpSurface()
    {
        BSplineSurface surface;
        surface.degreeU = 3;
        surface.degreeV = 3;
        surface.countU = 6;
        surface.countV = 5;
        surface.knotsU = ClampedUniformKnots(surface.countU, surface.degreeU);
        surface.knotsV = ClampedUniformKnots(surface.countV, surface.degreeV);

        for (int j = 0; j < surface.countV; ++j)
        {
            for (int i = 0; i < surface.countU; ++i)
            {
                const double x = 100.0 * i / (surface.countU - 1);
                const double y = 80.0 * j / (surface.countV - 1);
                // Raised in the middle, a little more towards +X.
                const double s = std::sin(3.14159265358979 * i / (surface.countU - 1));
                const double t = std::sin(3.14159265358979 * j / (surface.countV - 1));
                const double z = 20.0 + 12.0 * s * t + 1.5 * i / (surface.countU - 1);
                surface.points.push_back({ x, y, z });
            }
        }
        return surface;
    }
}
