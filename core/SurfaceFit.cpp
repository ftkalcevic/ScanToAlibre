// SurfaceFit.cpp : selecting areas of the scan and fitting a height-field
// B-spline surface to them.

#include "SurfaceFit.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <numeric>

#include "Orient.h"

namespace scancore
{
    namespace
    {
        const double kPi = 3.14159265358979323846;
        const int kDegree = 3;

        Point3 TriangleCross(const Mesh& mesh, int t)
        {
            const auto& tri = mesh.triangles[t];
            const Point3& a = mesh.vertices[tri[0]];
            return Cross(mesh.vertices[tri[1]] - a, mesh.vertices[tri[2]] - a);
        }

        // The knot span holding t, for clamped knots: degree <= span < count.
        int FindSpan(const std::vector<double>& knots, int count, double t)
        {
            if (t >= knots[count])
                return count - 1;
            if (t <= knots[kDegree])
                return kDegree;
            int low = kDegree, high = count;
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

        // The cubic basis functions N[span-3 .. span] at t and their first and
        // second derivatives (Piegl and Tiller A2.3): ders[k][j].
        void BasisDerivatives(const std::vector<double>& knots, int span, double t, double ders[3][4])
        {
            const int p = kDegree;
            double ndu[4][4] = {};
            double left[4] = {}, right[4] = {};
            ndu[0][0] = 1.0;
            for (int j = 1; j <= p; ++j)
            {
                left[j] = t - knots[span + 1 - j];
                right[j] = knots[span + j] - t;
                double saved = 0.0;
                for (int r = 0; r < j; ++r)
                {
                    ndu[j][r] = right[r + 1] + left[j - r];
                    const double temp = (ndu[j][r] != 0.0) ? ndu[r][j - 1] / ndu[j][r] : 0.0;
                    ndu[r][j] = saved + right[r + 1] * temp;
                    saved = left[j - r] * temp;
                }
                ndu[j][j] = saved;
            }
            for (int j = 0; j <= p; ++j)
                ders[0][j] = ndu[j][p];

            double a[2][4] = {};
            for (int r = 0; r <= p; ++r)
            {
                int s1 = 0, s2 = 1;
                a[0][0] = 1.0;
                for (int k = 1; k <= 2; ++k)
                {
                    double d = 0.0;
                    const int rk = r - k, pk = p - k;
                    if (r >= k)
                    {
                        a[s2][0] = (ndu[pk + 1][rk] != 0.0) ? a[s1][0] / ndu[pk + 1][rk] : 0.0;
                        d = a[s2][0] * ndu[rk][pk];
                    }
                    const int j1 = (rk >= -1) ? 1 : -rk;
                    const int j2 = (r - 1 <= pk) ? k - 1 : p - r;
                    for (int j = j1; j <= j2; ++j)
                    {
                        a[s2][j] = (ndu[pk + 1][rk + j] != 0.0) ? (a[s1][j] - a[s1][j - 1]) / ndu[pk + 1][rk + j] : 0.0;
                        d += a[s2][j] * ndu[rk + j][pk];
                    }
                    if (r <= pk)
                    {
                        a[s2][k] = (ndu[pk + 1][r] != 0.0) ? -a[s1][k - 1] / ndu[pk + 1][r] : 0.0;
                        d += a[s2][k] * ndu[r][pk];
                    }
                    ders[k][r] = d;
                    std::swap(s1, s2);
                }
            }
            double factor = p;
            for (int k = 1; k <= 2; ++k)
            {
                for (int j = 0; j <= p; ++j)
                    ders[k][j] *= factor;
                factor *= (p - k);
            }
        }

        // gram[d][i][k - i + 3] = the integral over 0..1 of N_i^(d) N_k^(d), for |i - k| <= 3.
        std::vector<std::array<std::array<double, 7>, 3>> GramMatrices(const std::vector<double>& knots, int count)
        {
            std::vector<std::array<std::array<double, 7>, 3>> gram(count);
            for (auto& g : gram)
                for (auto& row : g)
                    row.fill(0.0);
            static const double kNodes[4] = { -0.861136311594053, -0.339981043584856, 0.339981043584856,
                                              0.861136311594053 };
            static const double kWeights[4] = { 0.347854845137454, 0.652145154862546, 0.652145154862546,
                                                0.347854845137454 };
            for (int span = kDegree; span < count; ++span)
            {
                const double a = knots[span], b = knots[span + 1];
                if (!(b > a))
                    continue;
                for (int q = 0; q < 4; ++q)
                {
                    const double t = 0.5 * (a + b) + 0.5 * (b - a) * kNodes[q];
                    const double w = 0.5 * (b - a) * kWeights[q];
                    double ders[3][4];
                    BasisDerivatives(knots, span, t, ders);
                    for (int d = 0; d < 3; ++d)
                        for (int x = 0; x < 4; ++x)
                            for (int y = 0; y < 4; ++y)
                            {
                                const int i = span - kDegree + x, k = span - kDegree + y;
                                gram[i][d][k - i + 3] += w * ders[d][x] * ders[d][y];
                            }
                }
            }
            return gram;
        }

        // A symmetric positive definite band matrix: row r holds columns r - band .. r.
        struct BandMatrix
        {
            int size = 0, band = 0;
            std::vector<double> values;

            BandMatrix(int n, int b) : size(n), band(b), values(static_cast<size_t>(n) * (b + 1), 0.0) {}
            double& At(int r, int c) { return values[static_cast<size_t>(r) * (band + 1) + (c - r + band)]; }

            // Adds to (r, c) and (c, r), stored once.
            void Add(int r, int c, double value)
            {
                if (c > r)
                    std::swap(r, c);
                At(r, c) += value;
            }

            // Cholesky in place, then solves for b. False if not positive definite.
            bool Solve(std::vector<double>& b)
            {
                for (int j = 0; j < size; ++j)
                {
                    const int low = std::max(0, j - band);
                    double sum = At(j, j);
                    for (int k = low; k < j; ++k)
                        sum -= At(j, k) * At(j, k);
                    if (!(sum > 0.0))
                        return false;
                    const double diagonal = std::sqrt(sum);
                    At(j, j) = diagonal;
                    const int last = std::min(size - 1, j + band);
                    for (int i = j + 1; i <= last; ++i)
                    {
                        const int lowI = std::max(0, i - band);
                        double s = At(i, j);
                        for (int k = std::max(low, lowI); k < j; ++k)
                            s -= At(i, k) * At(j, k);
                        At(i, j) = s / diagonal;
                    }
                }
                for (int i = 0; i < size; ++i)
                {
                    double s = b[i];
                    for (int k = std::max(0, i - band); k < i; ++k)
                        s -= At(i, k) * b[k];
                    b[i] = s / At(i, i);
                }
                for (int i = size - 1; i >= 0; --i)
                {
                    double s = b[i];
                    const int last = std::min(size - 1, i + band);
                    for (int k = i + 1; k <= last; ++k)
                        s -= At(k, i) * b[k];
                    b[i] = s / At(i, i);
                }
                return true;
            }
        };

        struct Sample
        {
            double s, t, h, weight;
        };

        struct Grid
        {
            int countU = 0, countV = 0;
            std::vector<double> knotsU, knotsV;
            std::vector<double> heights;            // u fastest
        };

        // Solves the heights for a grid of countU x countV control points.
        bool SolveGrid(const std::vector<Sample>& samples, double s0, double s1, double t0, double t1, double alpha,
                       Grid& grid)
        {
            const int nu = grid.countU, nv = grid.countV, n = nu * nv;
            grid.knotsU = ClampedUniformKnots(nu, kDegree);
            grid.knotsV = ClampedUniformKnots(nv, kDegree);
            const double ls = s1 - s0, lt = t1 - t0;
            BandMatrix matrix(n, kDegree * nu + kDegree);
            std::vector<double> rhs(n, 0.0);

            for (const Sample& sample : samples)
            {
                const double u = (sample.s - s0) / ls, v = (sample.t - t0) / lt;
                const int su = FindSpan(grid.knotsU, nu, u), sv = FindSpan(grid.knotsV, nv, v);
                double du[3][4], dv[3][4];
                BasisDerivatives(grid.knotsU, su, u, du);
                BasisDerivatives(grid.knotsV, sv, v, dv);
                int index[16];
                double basis[16];
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x)
                    {
                        index[x + 4 * y] = (su - kDegree + x) + (sv - kDegree + y) * nu;
                        basis[x + 4 * y] = du[0][x] * dv[0][y];
                    }
                for (int a = 0; a < 16; ++a)
                {
                    rhs[index[a]] += sample.weight * basis[a] * sample.h;
                    // Each pair once: (a, b) and (b, a) are the same stored entry.
                    for (int b = 0; b <= a; ++b)
                        matrix.Add(index[a], index[b], sample.weight * basis[a] * basis[b]);
                }
            }

            // Bending: the integral of h_ss^2 + 2 h_st^2 + h_tt^2 over the domain.
            const double spacing = std::sqrt((ls / (nu - kDegree)) * (lt / (nv - kDegree)));
            const double lambda = alpha * std::pow(spacing, 4.0);
            const auto gu = GramMatrices(grid.knotsU, nu);
            const auto gv = GramMatrices(grid.knotsV, nv);
            const double area = ls * lt;
            const double c20 = lambda * area / std::pow(ls, 4.0);
            const double c11 = lambda * area * 2.0 / (ls * ls * lt * lt);
            const double c02 = lambda * area / std::pow(lt, 4.0);
            for (int j = 0; j < nv; ++j)
                for (int i = 0; i < nu; ++i)
                {
                    const int r = i + j * nu;
                    for (int l = std::max(0, j - kDegree); l <= std::min(nv - 1, j + kDegree); ++l)
                        for (int k = std::max(0, i - kDegree); k <= std::min(nu - 1, i + kDegree); ++k)
                        {
                            const int c = k + l * nu;
                            if (c > r)
                                continue;
                            const double value = c20 * gu[i][2][k - i + 3] * gv[j][0][l - j + 3] +
                                                 c11 * gu[i][1][k - i + 3] * gv[j][1][l - j + 3] +
                                                 c02 * gu[i][0][k - i + 3] * gv[j][2][l - j + 3];
                            matrix.Add(r, c, value);
                        }
                }

            // A little ridge, so a control point with no data near it and a flat
            // bending penalty still has an answer.
            double trace = 0.0;
            for (int r = 0; r < n; ++r)
                trace += matrix.At(r, r);
            const double ridge = 1e-10 * trace / n;
            for (int r = 0; r < n; ++r)
                matrix.At(r, r) += ridge;

            if (!matrix.Solve(rhs))
                return false;
            grid.heights = std::move(rhs);
            return true;
        }

        double GridHeight(const Grid& grid, double u, double v)
        {
            const int su = FindSpan(grid.knotsU, grid.countU, u), sv = FindSpan(grid.knotsV, grid.countV, v);
            double du[3][4], dv[3][4];
            BasisDerivatives(grid.knotsU, su, u, du);
            BasisDerivatives(grid.knotsV, sv, v, dv);
            double h = 0.0;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    h += du[0][x] * dv[0][y] * grid.heights[(su - kDegree + x) + (sv - kDegree + y) * grid.countU];
            return h;
        }

        double Greville(const std::vector<double>& knots, int i)
        {
            return (knots[i + 1] + knots[i + 2] + knots[i + 3]) / 3.0;
        }

        // Distance from p to the segment ab, in 2D.
        double SegmentDistance(const std::array<double, 2>& p, const std::array<double, 2>& a,
                               const std::array<double, 2>& b)
        {
            const double dx = b[0] - a[0], dy = b[1] - a[1];
            const double length2 = dx * dx + dy * dy;
            double f = (length2 > 0.0) ? ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / length2 : 0.0;
            f = std::clamp(f, 0.0, 1.0);
            const double ex = a[0] + f * dx - p[0], ey = a[1] + f * dy - p[1];
            return std::sqrt(ex * ex + ey * ey);
        }

        void DouglasPeucker(const std::vector<std::array<double, 2>>& points, size_t first, size_t last,
                            double tolerance, std::vector<char>& keep)
        {
            if (last <= first + 1)
                return;
            double worst = -1.0;
            size_t at = first;
            for (size_t i = first + 1; i < last; ++i)
            {
                const double d = SegmentDistance(points[i], points[first], points[last]);
                if (d > worst)
                {
                    worst = d;
                    at = i;
                }
            }
            if (worst > tolerance)
            {
                keep[at] = 1;
                DouglasPeucker(points, first, at, tolerance, keep);
                DouglasPeucker(points, at, last, tolerance, keep);
            }
        }

        double SignedArea(const std::vector<std::array<double, 2>>& loop)
        {
            double area = 0.0;
            for (size_t i = 0; i < loop.size(); ++i)
            {
                const auto& a = loop[i];
                const auto& b = loop[(i + 1) % loop.size()];
                area += a[0] * b[1] - b[0] * a[1];
            }
            return 0.5 * area;
        }
    }

    std::vector<int> GrowSmoothArea(const Mesh& mesh, const std::vector<std::array<int, 3>>& neighbours, int seed,
                                    double angleDegrees)
    {
        std::vector<int> area;
        if (seed < 0 || static_cast<size_t>(seed) >= mesh.triangles.size())
            return area;
        const double cosLimit = std::cos(angleDegrees * kPi / 180.0);
        std::vector<char> visited(mesh.triangles.size(), 0);
        std::deque<int> queue{ seed };
        visited[seed] = 1;
        area.push_back(seed);
        while (!queue.empty())
        {
            const int t = queue.front();
            queue.pop_front();
            const Point3 normal = Normalised(TriangleCross(mesh, t));
            for (int n : neighbours[t])
            {
                if (n < 0 || visited[n])
                    continue;
                if (Dot(Normalised(TriangleCross(mesh, n)), normal) < cosLimit)
                    continue;
                visited[n] = 1;
                area.push_back(n);
                queue.push_back(n);
            }
        }
        std::sort(area.begin(), area.end());
        return area;
    }

    std::vector<int> BrushArea(const Mesh& mesh, const std::vector<std::array<int, 3>>& neighbours, int seed,
                               const Point3& centre, double radius)
    {
        std::vector<int> area;
        if (seed < 0 || static_cast<size_t>(seed) >= mesh.triangles.size())
            return area;
        const double r2 = radius * radius;
        auto inside = [&](int t) {
            for (int k = 0; k < 3; ++k)
            {
                const Point3 d = mesh.vertices[mesh.triangles[t][k]] - centre;
                if (Dot(d, d) <= r2)
                    return true;
            }
            return false;
        };
        std::vector<char> visited(mesh.triangles.size(), 0);
        std::deque<int> queue{ seed };
        visited[seed] = 1;
        area.push_back(seed);
        while (!queue.empty())
        {
            const int t = queue.front();
            queue.pop_front();
            for (int n : neighbours[t])
            {
                if (n < 0 || visited[n])
                    continue;
                visited[n] = 1;
                if (!inside(n))
                    continue;
                area.push_back(n);
                queue.push_back(n);
            }
        }
        std::sort(area.begin(), area.end());
        return area;
    }

    bool SurfaceBaseFrame(const Mesh& mesh, const std::vector<int>& triangles, const SurfaceFitOptions& options,
                          SurfaceFrame& frame, std::string& message)
    {
        const Point3 X{ 1, 0, 0 }, Y{ 0, 1, 0 }, Z{ 0, 0, 1 };
        std::vector<Point3> mirrors;
        if (options.symmetryYZ)
            mirrors.push_back(X);
        if (options.symmetryZX)
            mirrors.push_back(Y);
        if (options.symmetryXY)
            mirrors.push_back(Z);
        if (mirrors.size() > 2)
        {
            message = "Only two symmetry planes can be used at once: no direction lies in all three, so there's no "
                      "base plane square to them all.";
            return false;
        }

        // The selection's area-weighted normal and middle.
        Point3 normalSum, middle;
        double area = 0.0;
        for (int t : triangles)
        {
            const Point3 c = TriangleCross(mesh, t);
            const double a = 0.5 * Length(c);
            normalSum = normalSum + c * 0.5;
            const auto& tri = mesh.triangles[t];
            middle = middle + (mesh.vertices[tri[0]] + mesh.vertices[tri[1]] + mesh.vertices[tri[2]]) * (a / 3.0);
            area += a;
        }
        if (!(area > 0.0))
        {
            message = "Select some of the scan first.";
            return false;
        }
        middle = middle / area;

        Point3 n;
        if (options.base == SurfaceBase::Auto)
        {
            if (mirrors.size() == 2)
            {
                n = Cross(mirrors[0], mirrors[1]);
            }
            else
            {
                Feature plane;
                if (!FitPlane(mesh, triangles, plane))
                {
                    message = "No plane could be fitted to the selection.";
                    return false;
                }
                n = plane.direction;
                if (mirrors.size() == 1)
                {
                    n = n - mirrors[0] * Dot(n, mirrors[0]);
                    if (Length(n) < 1e-6)
                    {
                        message = "The selection faces along the symmetry plane's normal, so no base plane square "
                                  "to the symmetry plane suits it. Choose a base plane.";
                        return false;
                    }
                }
                n = Normalised(n);
            }
        }
        else
        {
            n = (options.base == SurfaceBase::XY) ? Z : (options.base == SurfaceBase::YZ) ? X : Y;
            for (const Point3& m : mirrors)
            {
                if (std::fabs(Dot(m, n)) > 0.5)
                {
                    message = "A symmetry plane can't be the base plane too: the surface's heights are measured "
                              "square to the base plane, so they can't mirror across it.";
                    return false;
                }
            }
        }
        if (Dot(n, normalSum) < 0.0)
            n = n * -1.0;

        Point3 u;
        if (!mirrors.empty())
        {
            u = mirrors[0];
        }
        else if (options.base == SurfaceBase::XY)
        {
            u = X;
        }
        else if (options.base == SurfaceBase::YZ)
        {
            u = Y;
        }
        else if (options.base == SurfaceBase::ZX)
        {
            u = Z;
        }
        else
        {
            // The world axis lying flattest in the plane.
            const Point3 axes[3] = { X, Y, Z };
            double best = 2.0;
            for (const Point3& axis : axes)
            {
                const double d = std::fabs(Dot(axis, n));
                if (d < best)
                {
                    best = d;
                    u = axis;
                }
            }
            u = Normalised(u - n * Dot(u, n));
        }
        frame.n = n;
        frame.u = u;
        frame.v = Cross(n, u);

        // The origin in every symmetry plane, so the domain can be made symmetric.
        Point3 origin = middle;
        for (const Point3& m : mirrors)
            origin = origin - m * Dot(origin, m);
        frame.origin = origin;
        return true;
    }

    double SurfaceHeightAt(const SurfaceFitResult& result, double s, double t)
    {
        const double u = std::clamp((s - result.s0) / (result.s1 - result.s0), 0.0, 1.0);
        const double v = std::clamp((t - result.t0) / (result.t1 - result.t0), 0.0, 1.0);
        return Dot(Evaluate(result.surface, u, v) - result.frame.origin, result.frame.n);
    }

    SurfaceFitResult FitScanSurface(const Mesh& mesh, const std::vector<int>& triangles,
                                    const SurfaceFitOptions& options)
    {
        const auto start = std::chrono::steady_clock::now();
        SurfaceFitResult result;
        if (triangles.empty())
        {
            result.message = "Select some of the scan first.";
            return result;
        }
        SurfaceFrame& f = result.frame;
        if (!SurfaceBaseFrame(mesh, triangles, options, f, result.message))
            return result;

        // A height field: refuse a selection that folds over the base plane.
        const double cos85 = std::cos(85.0 * kPi / 180.0);
        double total = 0.0, steep = 0.0;
        for (int t : triangles)
        {
            const Point3 c = TriangleCross(mesh, t);
            const double a = 0.5 * Length(c);
            total += a;
            if (Dot(Normalised(c), f.n) < cos85)
                steep += a;
        }
        if (steep > 0.02 * total)
        {
            char text[256];
            sprintf_s(text, "%.0f%% of the selection faces more than 85 degrees away from the base plane's normal, "
                            "so it isn't a single height over that plane. Choose another base plane, or select less.",
                      100.0 * steep / total);
            result.message = text;
            return result;
        }

        // The selected triangles' corners, each weighted by a third of its triangles' area.
        std::map<int, double> cornerWeights;
        for (int t : triangles)
        {
            const double a = 0.5 * Length(TriangleCross(mesh, t));
            for (int k = 0; k < 3; ++k)
                cornerWeights[mesh.triangles[t][k]] += a / 3.0;
        }
        const size_t stride = std::max<size_t>(1, (cornerWeights.size() + options.maxSamples - 1) / options.maxSamples);

        std::vector<Point3> mirrors;
        if (options.symmetryYZ)
            mirrors.push_back({ 1, 0, 0 });
        if (options.symmetryZX)
            mirrors.push_back({ 0, 1, 0 });
        if (options.symmetryXY)
            mirrors.push_back({ 0, 0, 1 });
        auto mirrorsOf = [&](const Point3& p) {
            std::vector<Point3> copies{ p };
            for (const Point3& m : mirrors)
            {
                const size_t count = copies.size();
                for (size_t i = 0; i < count; ++i)
                    copies.push_back(copies[i] - m * (2.0 * Dot(copies[i], m)));
            }
            return copies;
        };
        auto project = [&](const Point3& p) {
            const Point3 d = p - f.origin;
            return std::array<double, 3>{ Dot(d, f.u), Dot(d, f.v), Dot(d, f.n) };
        };

        std::vector<Sample> samples;
        size_t k = 0;
        for (const auto& [vertex, weight] : cornerWeights)
        {
            if (k++ % stride != 0)
                continue;
            for (const Point3& p : mirrorsOf(mesh.vertices[vertex]))
            {
                const auto q = project(p);
                samples.push_back({ q[0], q[1], q[2], weight * stride });
            }
        }
        result.samples = samples.size();

        // The domain: the samples' rectangle (and the scan's box's, for Box), 2%
        // more and the offset, symmetric about each mirror.
        double s0 = 1e300, s1 = -1e300, t0 = 1e300, t1 = -1e300;
        auto include = [&](double s, double t) {
            s0 = std::min(s0, s);
            s1 = std::max(s1, s);
            t0 = std::min(t0, t);
            t1 = std::max(t1, t);
        };
        for (const Sample& s : samples)
            include(s.s, s.t);
        // Every vertex of the scan projected, with its mirrors (vertex v's
        // mirror m is at v * mirrorCount + m), for the Projection and
        // Selection outlines.
        const size_t mirrorCount = size_t(1) << mirrors.size();
        std::vector<std::array<double, 2>> flat;
        const bool trim = options.perimeter == SurfacePerimeter::Projection ||
                          options.perimeter == SurfacePerimeter::Selection;
        if (trim)
        {
            flat.reserve(mesh.vertices.size() * mirrorCount);
            for (const Point3& vertex : mesh.vertices)
                for (const Point3& p : mirrorsOf(vertex))
                {
                    const auto q = project(p);
                    flat.push_back({ q[0], q[1] });
                }
        }
        if (options.perimeter == SurfacePerimeter::Projection)
            for (const auto& q : flat)
                include(q[0], q[1]);
        if (options.perimeter == SurfacePerimeter::Box)
        {
            if (options.boxCorners.empty())
            {
                result.message = "There's no box round the scan to use.";
                return result;
            }
            for (const Point3& corner : options.boxCorners)
                for (const Point3& p : mirrorsOf(corner))
                {
                    const auto q = project(p);
                    include(q[0], q[1]);
                }
        }
        for (const Point3& m : mirrors)
        {
            if (std::fabs(Dot(m, f.u)) > 0.5)
            {
                const double reach = std::max(std::fabs(s0), std::fabs(s1));
                s0 = -reach;
                s1 = reach;
            }
            if (std::fabs(Dot(m, f.v)) > 0.5)
            {
                const double reach = std::max(std::fabs(t0), std::fabs(t1));
                t0 = -reach;
                t1 = reach;
            }
        }
        const double size = std::max({ s1 - s0, t1 - t0, 1e-6 });
        const double offset = std::max(0.0, options.offset);
        const double marginS = 0.02 * std::max(s1 - s0, 0.05 * size) + offset;
        const double marginT = 0.02 * std::max(t1 - t0, 0.05 * size) + offset;
        s0 -= marginS;
        s1 += marginS;
        t0 -= marginT;
        t1 += marginT;
        if (s1 - s0 < 0.05 * size)
        {
            const double grow = 0.025 * size;
            s0 -= grow;
            s1 += grow;
        }
        if (t1 - t0 < 0.05 * size)
        {
            const double grow = 0.025 * size;
            t0 -= grow;
            t1 += grow;
        }
        result.s0 = s0;
        result.s1 = s1;
        result.t0 = t0;
        result.t1 = t1;

        // Fit with more control points until 99% of the samples are within the tolerance.
        const double alpha = 1e-4 * std::pow(10.0, 4.0 * std::clamp(options.smoothing, 0.0, 1.0));
        const int maxSpans = std::max(1, options.maxControlPoints - kDegree);
        const double longest = std::max(s1 - s0, t1 - t0);
        const int levels[] = { 1, 2, 3, 4, 6, 9, 13, 19, 28, 37, 50, 70, 100 };
        Grid best;
        std::vector<double> residuals(samples.size());
        for (int level : levels)
        {
            Grid grid;
            const int spansU = std::clamp(static_cast<int>(std::lround(level * (s1 - s0) / longest)), 1, maxSpans);
            const int spansV = std::clamp(static_cast<int>(std::lround(level * (t1 - t0) / longest)), 1, maxSpans);
            grid.countU = spansU + kDegree;
            grid.countV = spansV + kDegree;
            if (!best.heights.empty() && grid.countU == best.countU && grid.countV == best.countV)
                break;
            if (!SolveGrid(samples, s0, s1, t0, t1, alpha, grid))
            {
                if (best.heights.empty())
                {
                    result.message = "The fit couldn't be solved (too few points selected?).";
                    return result;
                }
                break;
            }
            best = std::move(grid);

            double sum = 0.0;
            result.largest = 0.0;
            for (size_t i = 0; i < samples.size(); ++i)
            {
                const Sample& s = samples[i];
                const double r = GridHeight(best, (s.s - s0) / (s1 - s0), (s.t - t0) / (t1 - t0)) - s.h;
                residuals[i] = std::fabs(r);
                sum += r * r;
                result.largest = std::max(result.largest, std::fabs(r));
            }
            result.rms = std::sqrt(sum / std::max<size_t>(1, samples.size()));
            std::vector<double> sorted = residuals;
            const size_t at = std::min(sorted.size() - 1, static_cast<size_t>(0.99 * sorted.size()));
            std::nth_element(sorted.begin(), sorted.begin() + at, sorted.end());
            result.within99 = sorted[at];
            if (result.within99 <= options.tolerance || (spansU >= maxSpans && spansV >= maxSpans))
                break;
        }

        // The surface: control points over the domain at the Greville points, lifted by their heights.
        BSplineSurface& surface = result.surface;
        surface.degreeU = surface.degreeV = kDegree;
        surface.countU = best.countU;
        surface.countV = best.countV;
        surface.knotsU = best.knotsU;
        surface.knotsV = best.knotsV;
        surface.points.resize(static_cast<size_t>(best.countU) * best.countV);
        for (int j = 0; j < best.countV; ++j)
            for (int i = 0; i < best.countU; ++i)
            {
                const double s = s0 + (s1 - s0) * Greville(best.knotsU, i);
                const double t = t0 + (t1 - t0) * Greville(best.knotsV, j);
                surface.points[i + j * best.countU] =
                    f.origin + f.u * s + f.v * t + f.n * best.heights[i + j * best.countU];
            }

        // The trim: the outline of the whole scan, or of the selection, with
        // their mirrors, on the base plane.
        if (trim)
        {
            const bool whole = options.perimeter == SurfacePerimeter::Projection;
            const size_t count = (whole ? mesh.triangles.size() : triangles.size()) * mirrorCount;
            auto triangle = [&](size_t k, ProjectedTriangle& tri) {
                const size_t number = k / mirrorCount, m = k % mirrorCount;
                const auto& corners = mesh.triangles[whole ? number : static_cast<size_t>(triangles[number])];
                for (int c = 0; c < 3; ++c)
                    tri[c] = flat[static_cast<size_t>(corners[c]) * mirrorCount + m];
            };
            const double cell = std::max(s1 - s0, t1 - t0) / 200.0;
            const double simplify = std::max(options.tolerance, 0.5 * cell);
            const auto outline = ProjectionOutline(count, triangle, s0, s1, t0, t1, cell, simplify, offset);
            if (outline.size() < 3)
            {
                result.message = whole ? "The scan's outline couldn't be found." : "The selection's outline couldn't be found.";
                return result;
            }
            for (const auto& p : outline)
            {
                const double u = std::clamp((p[0] - s0) / (s1 - s0), 0.0, 1.0);
                const double v = std::clamp((p[1] - t0) / (t1 - t0), 0.0, 1.0);
                result.outlineUV.push_back({ u, v });
                result.outline.push_back(Evaluate(surface, u, v));
            }
        }

        result.ok = true;
        result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return result;
    }

    std::vector<std::array<double, 2>> ProjectionOutline(
        const std::vector<std::array<std::array<double, 2>, 3>>& triangles, double s0, double s1, double t0, double t1,
        double cell, double simplify, double offset)
    {
        return ProjectionOutline(
            triangles.size(), [&](size_t k, ProjectedTriangle& tri) { tri = triangles[k]; }, s0, s1, t0, t1, cell,
            simplify, offset);
    }

    std::vector<std::array<double, 2>> ProjectionOutline(size_t count,
                                                          const std::function<void(size_t, ProjectedTriangle&)>& triangle,
                                                          double s0, double s1, double t0, double t1, double cell,
                                                          double simplify, double offset)
    {
        // A grid with two empty cells round it.
        const int pad = 2;
        const int nx = static_cast<int>(std::ceil((s1 - s0) / cell)) + 2 * pad;
        const int ny = static_cast<int>(std::ceil((t1 - t0) / cell)) + 2 * pad;
        const double x0 = s0 - pad * cell, y0 = t0 - pad * cell;
        std::vector<char> filled(static_cast<size_t>(nx) * ny, 0);
        auto at = [&](int i, int j) -> char& { return filled[static_cast<size_t>(i) + static_cast<size_t>(j) * nx]; };
        auto cellOf = [&](double x, double y, int& i, int& j) {
            i = std::clamp(static_cast<int>(std::floor((x - x0) / cell)), 0, nx - 1);
            j = std::clamp(static_cast<int>(std::floor((y - y0) / cell)), 0, ny - 1);
        };

        ProjectedTriangle tri;
        for (size_t k = 0; k < count; ++k)
        {
            triangle(k, tri);
            double lowX = 1e300, highX = -1e300, lowY = 1e300, highY = -1e300;
            for (const auto& p : tri)
            {
                lowX = std::min(lowX, p[0]);
                highX = std::max(highX, p[0]);
                lowY = std::min(lowY, p[1]);
                highY = std::max(highY, p[1]);
                int i, j;
                cellOf(p[0], p[1], i, j);
                at(i, j) = 1;
            }
            int i0, j0, i1, j1;
            cellOf(lowX, lowY, i0, j0);
            cellOf(highX, highY, i1, j1);
            const double d = (tri[1][0] - tri[0][0]) * (tri[2][1] - tri[0][1]) -
                             (tri[2][0] - tri[0][0]) * (tri[1][1] - tri[0][1]);
            if (d == 0.0)
                continue;
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i)
                {
                    const double x = x0 + (i + 0.5) * cell, y = y0 + (j + 0.5) * cell;
                    const double a = ((tri[1][0] - x) * (tri[2][1] - y) - (tri[2][0] - x) * (tri[1][1] - y)) / d;
                    const double b = ((tri[2][0] - x) * (tri[0][1] - y) - (tri[0][0] - x) * (tri[2][1] - y)) / d;
                    const double c = 1.0 - a - b;
                    if (a >= 0.0 && b >= 0.0 && c >= 0.0)
                        at(i, j) = 1;
                }
        }

        // Close gaps of a cell: grow by one, then shrink by one.
        auto grow = [&](char value) {
            std::vector<char> next = filled;
            for (int j = 1; j < ny - 1; ++j)
                for (int i = 1; i < nx - 1; ++i)
                    if (at(i, j) != value)
                        for (int dj = -1; dj <= 1; ++dj)
                            for (int di = -1; di <= 1; ++di)
                                if (at(i + di, j + dj) == value)
                                    next[static_cast<size_t>(i) + static_cast<size_t>(j) * nx] = value;
            filled.swap(next);
        };
        grow(1);
        grow(0);

        // Fill holes: whatever the outside can't reach is inside.
        std::vector<char> outside(filled.size(), 0);
        {
            std::deque<std::pair<int, int>> queue{ { 0, 0 } };
            outside[0] = 1;
            while (!queue.empty())
            {
                const auto [i, j] = queue.front();
                queue.pop_front();
                const int di[4] = { 1, -1, 0, 0 }, dj[4] = { 0, 0, 1, -1 };
                for (int k = 0; k < 4; ++k)
                {
                    const int a = i + di[k], b = j + dj[k];
                    if (a < 0 || b < 0 || a >= nx || b >= ny)
                        continue;
                    const size_t index = static_cast<size_t>(a) + static_cast<size_t>(b) * nx;
                    if (outside[index] || filled[index])
                        continue;
                    outside[index] = 1;
                    queue.push_back({ a, b });
                }
            }
        }
        for (size_t i = 0; i < filled.size(); ++i)
            filled[i] = outside[i] ? 0 : 1;

        // Grow by a disc of the offset round every cell on the edge.
        const double reach = offset / cell;
        if (reach > 0.5)
        {
            const int r = static_cast<int>(std::ceil(reach));
            std::vector<char> grown = filled;
            for (int j = 1; j < ny - 1; ++j)
                for (int i = 1; i < nx - 1; ++i)
                {
                    if (!at(i, j) || (at(i - 1, j) && at(i + 1, j) && at(i, j - 1) && at(i, j + 1)))
                        continue;
                    for (int dj = -r; dj <= r; ++dj)
                        for (int di = -r; di <= r; ++di)
                        {
                            const int a = i + di, b = j + dj;
                            if (a < 0 || b < 0 || a >= nx || b >= ny || di * di + dj * dj > reach * reach)
                                continue;
                            grown[static_cast<size_t>(a) + static_cast<size_t>(b) * nx] = 1;
                        }
                }
            filled.swap(grown);
        }

        // The largest piece.
        std::vector<int> label(filled.size(), -1);
        int bestLabel = -1;
        size_t bestCount = 0;
        int labels = 0;
        for (size_t start = 0; start < filled.size(); ++start)
        {
            if (!filled[start] || label[start] >= 0)
                continue;
            const int current = labels++;
            size_t count = 0;
            std::deque<size_t> queue{ start };
            label[start] = current;
            while (!queue.empty())
            {
                const size_t index = queue.front();
                queue.pop_front();
                ++count;
                const int i = static_cast<int>(index % nx), j = static_cast<int>(index / nx);
                const int di[4] = { 1, -1, 0, 0 }, dj[4] = { 0, 0, 1, -1 };
                for (int k = 0; k < 4; ++k)
                {
                    const int a = i + di[k], b = j + dj[k];
                    if (a < 0 || b < 0 || a >= nx || b >= ny)
                        continue;
                    const size_t next = static_cast<size_t>(a) + static_cast<size_t>(b) * nx;
                    if (!filled[next] || label[next] >= 0)
                        continue;
                    label[next] = current;
                    queue.push_back(next);
                }
            }
            if (count > bestCount)
            {
                bestCount = count;
                bestLabel = current;
            }
        }
        if (bestLabel < 0)
            return {};
        auto in = [&](int i, int j) {
            return i >= 0 && j >= 0 && i < nx && j < ny &&
                   label[static_cast<size_t>(i) + static_cast<size_t>(j) * nx] == bestLabel;
        };

        // Its boundary edges, the piece on their left, chained into loops.
        const int cornersX = nx + 1;
        auto corner = [&](int i, int j) { return i + j * cornersX; };
        std::multimap<int, int> edges;     // from corner, to corner
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
            {
                if (!in(i, j))
                    continue;
                if (!in(i, j - 1))
                    edges.insert({ corner(i, j), corner(i + 1, j) });
                if (!in(i + 1, j))
                    edges.insert({ corner(i + 1, j), corner(i + 1, j + 1) });
                if (!in(i, j + 1))
                    edges.insert({ corner(i + 1, j + 1), corner(i, j + 1) });
                if (!in(i - 1, j))
                    edges.insert({ corner(i, j + 1), corner(i, j) });
            }
        std::vector<std::array<double, 2>> best;
        double bestArea = 0.0;
        while (!edges.empty())
        {
            auto it = edges.begin();
            const int first = it->first;
            int to = it->second;
            edges.erase(it);
            std::vector<int> loop{ first };
            while (to != first)
            {
                loop.push_back(to);
                auto range = edges.equal_range(to);
                if (range.first == range.second)
                    break;
                to = range.first->second;
                edges.erase(range.first);
            }
            std::vector<std::array<double, 2>> points;
            for (int c : loop)
                points.push_back({ x0 + (c % cornersX) * cell, y0 + (c / cornersX) * cell });
            const double area = SignedArea(points);
            if (area > bestArea)
            {
                bestArea = area;
                best = std::move(points);
            }
        }
        if (best.size() < 3)
            return {};

        // Smooth the staircase, then simplify.
        for (int pass = 0; pass < 3; ++pass)
        {
            std::vector<std::array<double, 2>> next(best.size());
            for (size_t i = 0; i < best.size(); ++i)
            {
                const auto& a = best[(i + best.size() - 1) % best.size()];
                const auto& b = best[i];
                const auto& c = best[(i + 1) % best.size()];
                next[i] = { 0.25 * a[0] + 0.5 * b[0] + 0.25 * c[0], 0.25 * a[1] + 0.5 * b[1] + 0.25 * c[1] };
            }
            best.swap(next);
        }
        size_t far = 0;
        double farDistance = -1.0;
        for (size_t i = 0; i < best.size(); ++i)
        {
            const double dx = best[i][0] - best[0][0], dy = best[i][1] - best[0][1];
            if (dx * dx + dy * dy > farDistance)
            {
                farDistance = dx * dx + dy * dy;
                far = i;
            }
        }
        std::vector<std::array<double, 2>> closed = best;
        closed.push_back(best[0]);
        std::vector<char> keep(closed.size(), 0);
        keep[0] = keep[far] = keep[closed.size() - 1] = 1;
        DouglasPeucker(closed, 0, far, simplify, keep);
        DouglasPeucker(closed, far, closed.size() - 1, simplify, keep);
        std::vector<std::array<double, 2>> simplified;
        for (size_t i = 0; i + 1 < closed.size(); ++i)
            if (keep[i])
                simplified.push_back(closed[i]);
        return simplified;
    }
}
