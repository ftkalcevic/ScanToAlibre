// ViewProjection.cpp : the view's projection, fitted from samples.

#include "ViewProjection.h"

#include <algorithm>
#include <cmath>

namespace scancore
{
    namespace
    {
        // Solves a x = b in place by Gaussian elimination with partial pivoting.
        bool Solve(std::vector<std::vector<double>>& a, std::vector<double>& b, std::vector<double>& x)
        {
            const size_t n = b.size();
            for (size_t col = 0; col < n; ++col)
            {
                size_t pivot = col;
                for (size_t row = col + 1; row < n; ++row)
                    if (std::fabs(a[row][col]) > std::fabs(a[pivot][col]))
                        pivot = row;
                if (std::fabs(a[pivot][col]) < 1e-12)
                    return false;
                std::swap(a[pivot], a[col]);
                std::swap(b[pivot], b[col]);
                for (size_t row = col + 1; row < n; ++row)
                {
                    const double f = a[row][col] / a[col][col];
                    for (size_t k = col; k < n; ++k)
                        a[row][k] -= f * a[col][k];
                    b[row] -= f * b[col];
                }
            }
            x.assign(n, 0.0);
            for (size_t i = n; i-- > 0;)
            {
                double sum = b[i];
                for (size_t k = i + 1; k < n; ++k)
                    sum -= a[i][k] * x[k];
                x[i] = sum / a[i][i];
            }
            return true;
        }
    }

    bool ViewProjection::Fit(const std::vector<Point3>& world, const std::vector<ScreenPoint>& screen)
    {
        const size_t n = world.size();
        if (n < 6 || screen.size() != n)
            return false;

        // Normalise both sides so the equations are well scaled.
        Box3 box;
        for (const Point3& p : world)
            box.Add(p);
        m_centre = box.Centre();
        const Point3 size = box.Size();
        m_scale = std::max({ size.x, size.y, size.z }) * 0.5;
        if (!(m_scale > 0.0))
            return false;

        double uLow = 1e300, uHigh = -1e300, vLow = 1e300, vHigh = -1e300;
        for (const ScreenPoint& s : screen)
        {
            uLow = std::min(uLow, s.u);
            uHigh = std::max(uHigh, s.u);
            vLow = std::min(vLow, s.v);
            vHigh = std::max(vHigh, s.v);
        }
        m_cu = 0.5 * (uLow + uHigh);
        m_cv = 0.5 * (vLow + vHigh);
        m_su = std::max(0.5 * (uHigh - uLow), 1e-9);
        m_sv = std::max(0.5 * (vHigh - vLow), 1e-9);

        // Unknowns m00..m23 with m23 = 1:
        //   u (m20 x + m21 y + m22 z + 1) = m00 x + m01 y + m02 z + m03, and the same for v.
        // Least squares through the normal equations.
        std::vector<std::vector<double>> ata(11, std::vector<double>(11, 0.0));
        std::vector<double> atb(11, 0.0);
        auto addRow = [&](const double row[11], double rhs) {
            for (int i = 0; i < 11; ++i)
            {
                for (int j = 0; j < 11; ++j)
                    ata[i][j] += row[i] * row[j];
                atb[i] += row[i] * rhs;
            }
        };
        for (size_t i = 0; i < n; ++i)
        {
            const Point3 p = (world[i] - m_centre) / m_scale;
            const double u = (screen[i].u - m_cu) / m_su;
            const double v = (screen[i].v - m_cv) / m_sv;
            const double rowU[11] = { p.x, p.y, p.z, 1.0, 0.0, 0.0, 0.0, 0.0, -u * p.x, -u * p.y, -u * p.z };
            const double rowV[11] = { 0.0, 0.0, 0.0, 0.0, p.x, p.y, p.z, 1.0, -v * p.x, -v * p.y, -v * p.z };
            addRow(rowU, u);
            addRow(rowV, v);
        }

        std::vector<double> x;
        if (!Solve(ata, atb, x))
            return false;
        for (int k = 0; k < 4; ++k)
        {
            m_m[0][k] = x[k];
            m_m[1][k] = x[4 + k];
        }
        m_m[2][0] = x[8];
        m_m[2][1] = x[9];
        m_m[2][2] = x[10];
        m_m[2][3] = 1.0;

        m_largestError = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const ScreenPoint s = Project(world[i]);
            m_largestError = std::max(m_largestError, std::hypot(s.u - screen[i].u, s.v - screen[i].v));
        }
        return true;
    }

    ScreenPoint ViewProjection::Project(const Point3& world) const
    {
        const Point3 p = (world - m_centre) / m_scale;
        double r[3];
        for (int i = 0; i < 3; ++i)
            r[i] = m_m[i][0] * p.x + m_m[i][1] * p.y + m_m[i][2] * p.z + m_m[i][3];
        return { r[0] / r[2] * m_su + m_cu, r[1] / r[2] * m_sv + m_cv };
    }

    Ray ViewProjection::LineThrough(double u, double v) const
    {
        const double un = (u - m_cu) / m_su;
        const double vn = (v - m_cv) / m_sv;

        // The line is where two planes meet: (row0 - u row2) . (p, 1) = 0 and
        // (row1 - v row2) . (p, 1) = 0, in normalised world coordinates.
        Point3 a, b;
        double ad, bd;
        a = { m_m[0][0] - un * m_m[2][0], m_m[0][1] - un * m_m[2][1], m_m[0][2] - un * m_m[2][2] };
        ad = m_m[0][3] - un * m_m[2][3];
        b = { m_m[1][0] - vn * m_m[2][0], m_m[1][1] - vn * m_m[2][1], m_m[1][2] - vn * m_m[2][2] };
        bd = m_m[1][3] - vn * m_m[2][3];

        // a and b are the gradients of u and v (times the same factor, 1 / row2 . p,
        // squared away in the cross product), so a x b is right x down: into the screen.
        const Point3 direction = Normalised(Cross(a, b));

        // The point on the line nearest the normalised origin: alpha a + beta b with
        // a . p = -ad and b . p = -bd.
        const double aa = Dot(a, a), ab = Dot(a, b), bb = Dot(b, b);
        const double det = aa * bb - ab * ab;
        Point3 point;
        if (std::fabs(det) > 1e-300)
        {
            const double alpha = (-ad * bb + bd * ab) / det;
            const double beta = (-bd * aa + ad * ab) / det;
            point = a * alpha + b * beta;
        }

        Ray ray;
        ray.origin = point * m_scale + m_centre;
        ray.direction = direction;
        return ray;
    }

    Point3 ViewProjection::IntoScreen(const Point3& at) const
    {
        const ScreenPoint s = Project(at);
        return LineThrough(s.u, s.v).direction;
    }

    std::vector<Point3> ProjectionSamplePoints(const Box3& box)
    {
        // A box of no thickness in some direction would put every corner in one
        // plane, so give it some.
        Point3 low = box.low, high = box.high;
        const Point3 size = box.Size();
        const double pad = std::max({ size.x, size.y, size.z, 1.0 }) * 0.25;
        if (size.x < pad) { low.x -= pad; high.x += pad; }
        if (size.y < pad) { low.y -= pad; high.y += pad; }
        if (size.z < pad) { low.z -= pad; high.z += pad; }

        std::vector<Point3> points;
        for (int i = 0; i < 8; ++i)
            points.push_back({ (i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y, (i & 4) ? high.z : low.z });
        return points;
    }
}
