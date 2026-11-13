// Orient.cpp : moving, turning and scaling a scan, and lining it up.

#include "Orient.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <sstream>
#include <unordered_map>

namespace scancore
{
    namespace
    {
        const double kPi = 3.14159265358979323846;

        Point3 TriangleNormal(const Mesh& mesh, int t)
        {
            const auto& tri = mesh.triangles[t];
            return Cross(mesh.vertices[tri[1]] - mesh.vertices[tri[0]], mesh.vertices[tri[2]] - mesh.vertices[tri[0]]);
        }

        // Area-weighted first and second moments over triangle surfaces.
        struct Moments
        {
            double area = 0.0;
            Point3 first;                    // sum of area x centroid
            double second[3][3] = {};        // sum of the integral of p p^T over each triangle
            Point3 normals;                  // sum of area-weighted normals

            void Add(const Mesh& mesh, int t)
            {
                const auto& tri = mesh.triangles[t];
                const Point3& a = mesh.vertices[tri[0]];
                const Point3& b = mesh.vertices[tri[1]];
                const Point3& c = mesh.vertices[tri[2]];
                const Point3 n = Cross(b - a, c - a);
                const double triangleArea = 0.5 * Length(n);
                if (triangleArea == 0.0)
                    return;
                const Point3 g = (a + b + c) / 3.0;
                area += triangleArea;
                first = first + g * triangleArea;
                normals = normals + n * 0.5;
                // The integral of p p^T over a triangle is A/12 (9 g g^T + a a^T + b b^T + c c^T).
                const double pg[3] = { g.x, g.y, g.z };
                const double pa[3] = { a.x, a.y, a.z }, pb[3] = { b.x, b.y, b.z }, pc[3] = { c.x, c.y, c.z };
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        second[i][j] += triangleArea / 12.0 *
                                        (9.0 * pg[i] * pg[j] + pa[i] * pa[j] + pb[i] * pb[j] + pc[i] * pc[j]);
            }

            Point3 Mean() const { return area > 0.0 ? first / area : Point3{}; }

            void Covariance(double c[3][3]) const
            {
                const Point3 m = Mean();
                const double pm[3] = { m.x, m.y, m.z };
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        c[i][j] = (area > 0.0 ? second[i][j] / area : 0.0) - pm[i] * pm[j];
            }

            // The fitted plane's normal (the least-spread direction), on the side the triangles face.
            Point3 PlaneNormal() const
            {
                double c[3][3];
                Covariance(c);
                double values[3];
                Point3 vectors[3];
                SymmetricEigen(c, values, vectors);
                Point3 n = vectors[0];
                if (Dot(n, normals) < 0.0)
                    n = n * -1.0;
                return n;
            }
        };

        uint64_t EdgeKey(int a, int b)
        {
            if (a > b)
                std::swap(a, b);
            return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
        }

        double MatrixScale(const Transform& t)
        {
            return Length(Point3{ t.m[0][0], t.m[1][0], t.m[2][0] });
        }

        Point3 Perpendicular(const Point3& d)
        {
            const Point3 other = (std::fabs(d.x) < 0.9) ? Point3{ 1, 0, 0 } : Point3{ 0, 1, 0 };
            return Normalised(Cross(d, other));
        }

        // The part of v square to unit n.
        Point3 Reject(const Point3& v, const Point3& n)
        {
            return v - n * Dot(v, n);
        }
    }

    Point3 Transform::Apply(const Point3& p) const
    {
        return { m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + t.x,
                 m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + t.y,
                 m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + t.z };
    }

    Point3 Transform::ApplyDirection(const Point3& d) const
    {
        return Normalised({ m[0][0] * d.x + m[0][1] * d.y + m[0][2] * d.z,
                            m[1][0] * d.x + m[1][1] * d.y + m[1][2] * d.z,
                            m[2][0] * d.x + m[2][1] * d.y + m[2][2] * d.z });
    }

    Transform Transform::Then(const Transform& next) const
    {
        Transform out;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                out.m[i][j] = next.m[i][0] * m[0][j] + next.m[i][1] * m[1][j] + next.m[i][2] * m[2][j];
        const Point3 moved{ next.m[0][0] * t.x + next.m[0][1] * t.y + next.m[0][2] * t.z,
                            next.m[1][0] * t.x + next.m[1][1] * t.y + next.m[1][2] * t.z,
                            next.m[2][0] * t.x + next.m[2][1] * t.y + next.m[2][2] * t.z };
        out.t = moved + next.t;
        return out;
    }

    Transform Transform::Translation(const Point3& offset)
    {
        Transform out;
        out.t = offset;
        return out;
    }

    Transform Transform::Rotation(const Point3& axisIn, double angle, const Point3& pivot)
    {
        const Point3 k = Normalised(axisIn);
        const double c = std::cos(angle), s = std::sin(angle), v = 1.0 - c;
        Transform out;
        out.m[0][0] = c + k.x * k.x * v;       out.m[0][1] = k.x * k.y * v - k.z * s; out.m[0][2] = k.x * k.z * v + k.y * s;
        out.m[1][0] = k.y * k.x * v + k.z * s; out.m[1][1] = c + k.y * k.y * v;       out.m[1][2] = k.y * k.z * v - k.x * s;
        out.m[2][0] = k.z * k.x * v - k.y * s; out.m[2][1] = k.z * k.y * v + k.x * s; out.m[2][2] = c + k.z * k.z * v;
        Transform about = out;
        about.t = Point3{};
        out.t = pivot - about.Apply(pivot);
        return out;
    }

    Transform Transform::Scaling(double factor, const Point3& pivot)
    {
        Transform out;
        for (int i = 0; i < 3; ++i)
            out.m[i][i] = factor;
        out.t = pivot - pivot * factor;
        return out;
    }

    Transform Transform::RotationBetween(const Point3& from, const Point3& to, const Point3& pivot)
    {
        const Point3 a = Normalised(from), b = Normalised(to);
        const Point3 axis = Cross(a, b);
        const double sine = Length(axis), cosine = Dot(a, b);
        if (sine < 1e-12)
        {
            if (cosine > 0.0)
                return Transform();
            return Rotation(Perpendicular(a), kPi, pivot);
        }
        return Rotation(axis, std::atan2(sine, cosine), pivot);
    }

    void TransformMesh(Mesh& mesh, const Transform& transform)
    {
        for (Point3& p : mesh.vertices)
            p = transform.Apply(p);
        // A mirror (negative scale) would turn the triangles inside out; scales
        // here are positive, so the winding stays.
    }

    void SymmetricEigen(const double input[3][3], double values[3], Point3 vectors[3])
    {
        double a[3][3], v[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                a[i][j] = input[i][j];

        for (int sweep = 0; sweep < 50; ++sweep)
        {
            const double off = std::fabs(a[0][1]) + std::fabs(a[0][2]) + std::fabs(a[1][2]);
            const double diag = std::fabs(a[0][0]) + std::fabs(a[1][1]) + std::fabs(a[2][2]);
            if (off <= 1e-15 * (diag + 1e-300))
                break;
            for (int p = 0; p < 2; ++p)
            {
                for (int q = p + 1; q < 3; ++q)
                {
                    if (std::fabs(a[p][q]) < 1e-300)
                        continue;
                    const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                    const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                    const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                    for (int k = 0; k < 3; ++k)
                    {
                        const double akp = a[k][p], akq = a[k][q];
                        a[k][p] = c * akp - s * akq;
                        a[k][q] = s * akp + c * akq;
                    }
                    for (int k = 0; k < 3; ++k)
                    {
                        const double apk = a[p][k], aqk = a[q][k];
                        a[p][k] = c * apk - s * aqk;
                        a[q][k] = s * apk + c * aqk;
                    }
                    for (int k = 0; k < 3; ++k)
                    {
                        const double vkp = v[k][p], vkq = v[k][q];
                        v[k][p] = c * vkp - s * vkq;
                        v[k][q] = s * vkp + c * vkq;
                    }
                }
            }
        }

        int order[3] = { 0, 1, 2 };
        std::sort(order, order + 3, [&a](int x, int y) { return a[x][x] < a[y][y]; });
        for (int i = 0; i < 3; ++i)
        {
            values[i] = a[order[i]][order[i]];
            vectors[i] = Normalised({ v[0][order[i]], v[1][order[i]], v[2][order[i]] });
        }
    }

    void OrientedBox::ApplyTransform(const Transform& transform)
    {
        const double scale = MatrixScale(transform);
        centre = transform.Apply(centre);
        for (int k = 0; k < 3; ++k)
        {
            axes[k] = transform.ApplyDirection(axes[k]);
            halfSize[k] *= scale;
        }
    }

    std::array<Point3, 8> OrientedBox::Corners() const
    {
        std::array<Point3, 8> corners;
        for (int i = 0; i < 8; ++i)
        {
            Point3 p = centre;
            for (int k = 0; k < 3; ++k)
                p = p + axes[k] * (((i >> k) & 1) ? halfSize[k] : -halfSize[k]);
            corners[i] = p;
        }
        return corners;
    }

    namespace
    {
        struct Point2
        {
            double x, y;
        };

        double Cross2(const Point2& o, const Point2& a, const Point2& b)
        {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        }

        // Anticlockwise, without collinear points (Andrew's monotone chain). Points
        // well inside the octagon of the eight extremes are dropped first, which
        // leaves few for a scan's many surface points.
        std::vector<Point2> ConvexHull2D(std::vector<Point2> points)
        {
            if (points.size() > 64)
            {
                Point2 extreme[8];
                double best[8];
                const double dirs[8][2] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };
                for (int k = 0; k < 8; ++k)
                    best[k] = -1e300;
                for (const Point2& p : points)
                {
                    for (int k = 0; k < 8; ++k)
                    {
                        const double d = p.x * dirs[k][0] + p.y * dirs[k][1];
                        if (d > best[k])
                        {
                            best[k] = d;
                            extreme[k] = p;
                        }
                    }
                }
                std::vector<Point2> kept;
                for (const Point2& p : points)
                {
                    bool inside = true;
                    for (int k = 0; k < 8 && inside; ++k)
                        inside = Cross2(extreme[k], extreme[(k + 1) % 8], p) > 1e-12;
                    if (!inside)
                        kept.push_back(p);
                }
                for (const Point2& p : extreme)
                    kept.push_back(p);
                points.swap(kept);
            }

            std::sort(points.begin(), points.end(),
                      [](const Point2& a, const Point2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
            points.erase(std::unique(points.begin(), points.end(),
                                     [](const Point2& a, const Point2& b) { return a.x == b.x && a.y == b.y; }),
                         points.end());
            if (points.size() < 3)
                return points;
            std::vector<Point2> hull(2 * points.size());
            size_t k = 0;
            for (size_t i = 0; i < points.size(); ++i)
            {
                while (k >= 2 && Cross2(hull[k - 2], hull[k - 1], points[i]) <= 0.0)
                    --k;
                hull[k++] = points[i];
            }
            for (size_t i = points.size() - 1, lower = k + 1; i-- > 0;)
            {
                while (k >= lower && Cross2(hull[k - 2], hull[k - 1], points[i]) <= 0.0)
                    --k;
                hull[k++] = points[i];
            }
            hull.resize(k - 1);
            return hull;
        }

        // The direction (radians from x) of the smallest-area rectangle round a
        // convex polygon: one of its sides lies along an edge of the polygon.
        double MinAreaRectangleAngle(const std::vector<Point2>& hull, double& area)
        {
            area = 1e300;
            double bestAngle = 0.0;
            const size_t n = hull.size();
            if (n < 3)
            {
                area = 0.0;
                return 0.0;
            }
            for (size_t i = 0; i < n; ++i)
            {
                const Point2& a = hull[i];
                const Point2& b = hull[(i + 1) % n];
                const double length = std::hypot(b.x - a.x, b.y - a.y);
                if (length == 0.0)
                    continue;
                const double ux = (b.x - a.x) / length, uy = (b.y - a.y) / length;
                double lowU = 1e300, highU = -1e300, lowV = 1e300, highV = -1e300;
                for (const Point2& p : hull)
                {
                    const double pu = p.x * ux + p.y * uy, pv = -p.x * uy + p.y * ux;
                    lowU = std::min(lowU, pu);
                    highU = std::max(highU, pu);
                    lowV = std::min(lowV, pv);
                    highV = std::max(highV, pv);
                }
                const double rectangle = (highU - lowU) * (highV - lowV);
                if (rectangle < area)
                {
                    area = rectangle;
                    bestAngle = std::atan2(uy, ux);
                }
            }
            return bestAngle;
        }

        double RectangleArea(const std::vector<Point2>& hull, double angle)
        {
            const double ux = std::cos(angle), uy = std::sin(angle);
            double lowU = 1e300, highU = -1e300, lowV = 1e300, highV = -1e300;
            for (const Point2& p : hull)
            {
                const double pu = p.x * ux + p.y * uy, pv = -p.x * uy + p.y * ux;
                lowU = std::min(lowU, pu);
                highU = std::max(highU, pu);
                lowV = std::min(lowV, pv);
                highV = std::max(highV, pv);
            }
            return (highU - lowU) * (highV - lowV);
        }

        // One way round for a box axis, whichever sign it came with: its largest part positive.
        Point3 Canonical(const Point3& a)
        {
            const double largest = (std::fabs(a.x) >= std::fabs(a.y) && std::fabs(a.x) >= std::fabs(a.z)) ? a.x
                                   : (std::fabs(a.y) >= std::fabs(a.z))                                   ? a.y
                                                                                                          : a.z;
            return (largest < 0.0) ? a * -1.0 : a;
        }

        // The normals of the largest flat areas, by binning triangle normals
        // (folded so n and -n count together) on a cube map, about 1.3 degrees a bin.
        std::vector<Point3> FlatAreaNormals(const Mesh& mesh, size_t count)
        {
            struct Bin
            {
                double area = 0.0;
                Point3 sum;
            };
            const int cells = 90;
            std::unordered_map<int, Bin> bins;
            for (const auto& tri : mesh.triangles)
            {
                const Point3 n2 = Cross(mesh.vertices[tri[1]] - mesh.vertices[tri[0]],
                                        mesh.vertices[tri[2]] - mesh.vertices[tri[0]]);
                const double area = 0.5 * Length(n2);
                if (area == 0.0)
                    continue;
                const Point3 n = Canonical(n2 / (2.0 * area));
                const double c[3] = { n.x, n.y, n.z };
                int axis = 0;
                for (int k = 1; k < 3; ++k)
                    if (std::fabs(c[k]) > std::fabs(c[axis]))
                        axis = k;
                const double u = c[(axis + 1) % 3] / std::fabs(c[axis]);
                const double v = c[(axis + 2) % 3] / std::fabs(c[axis]);
                const int iu = std::clamp(static_cast<int>((u + 1.0) * 0.5 * cells), 0, cells - 1);
                const int iv = std::clamp(static_cast<int>((v + 1.0) * 0.5 * cells), 0, cells - 1);
                Bin& bin = bins[(axis * cells + iu) * cells + iv];
                bin.area += area;
                bin.sum = bin.sum + n * area;
            }
            std::vector<Bin> sorted;
            for (const auto& entry : bins)
                sorted.push_back(entry.second);
            std::sort(sorted.begin(), sorted.end(), [](const Bin& a, const Bin& b) { return a.area > b.area; });
            std::vector<Point3> normals;
            for (size_t i = 0; i < sorted.size() && normals.size() < count; ++i)
                normals.push_back(Normalised(sorted[i].sum));
            return normals;
        }
    }

    OrientedBox FitOrientedBox(const Mesh& mesh)
    {
        OrientedBox box;
        if (mesh.vertices.empty())
            return box;

        // Directions to try as one of the box's axes: the largest flat areas'
        // normals first (a machined part's faces), then the world axes, then the
        // principal directions of the surface.
        std::vector<Point3> candidates = FlatAreaNormals(mesh, 6);
        candidates.push_back({ 1, 0, 0 });
        candidates.push_back({ 0, 1, 0 });
        candidates.push_back({ 0, 0, 1 });
        Moments moments;
        for (size_t t = 0; t < mesh.triangles.size(); ++t)
            moments.Add(mesh, static_cast<int>(t));
        if (moments.area > 0.0)
        {
            double c[3][3];
            moments.Covariance(c);
            double values[3];
            Point3 vectors[3];
            SymmetricEigen(c, values, vectors);
            for (const Point3& v : vectors)
                candidates.push_back(v);
        }

        // For each, the smallest rectangle round the scan seen along it (rotating
        // calipers on the outline) times the scan's length along it. A machined
        // part should sit square to its sides, so turning about an axis follows the
        // sides instead if that costs at most 5% over the smallest: the
        // area-weighted average direction of the faces square to the axis, with
        // directions a quarter turn apart counted alike (a box looks the same
        // turned 90 degrees), so a mirror pair of facets or a round area cancels
        // out and flat sides decide. An axis along a flat area's normal wins if its
        // box is within 5% of the smallest of all.
        std::vector<Point3> faceNormals(mesh.triangles.size());
        std::vector<double> faceAreas(mesh.triangles.size());
        for (size_t t = 0; t < mesh.triangles.size(); ++t)
        {
            const Point3 n = TriangleNormal(mesh, static_cast<int>(t));
            faceAreas[t] = 0.5 * Length(n);
            faceNormals[t] = Normalised(n);
        }

        const size_t flatCount = candidates.size() - 3 - (moments.area > 0.0 ? 3 : 0);
        const double kFaceAllowance = 1.05;
        struct Fit
        {
            double volume;
            Point3 axes[3];
            bool onFace;
        };
        std::vector<Fit> fits;
        std::vector<Point3> tried;
        std::vector<Point2> projected(mesh.vertices.size());
        for (size_t c = 0; c < candidates.size(); ++c)
        {
            const Point3 d = Canonical(Normalised(candidates[c]));
            if (Length(d) == 0.0)
                continue;
            bool seen = false;
            for (const Point3& t : tried)
                seen = seen || std::fabs(Dot(t, d)) > 0.99996;     // within about half a degree
            if (seen)
                continue;
            tried.push_back(d);

            const Point3 u = Perpendicular(d);
            const Point3 v = Cross(d, u);
            double low = 1e300, high = -1e300;
            for (size_t i = 0; i < mesh.vertices.size(); ++i)
            {
                const Point3& p = mesh.vertices[i];
                projected[i] = { Dot(p, u), Dot(p, v) };
                const double along = Dot(p, d);
                low = std::min(low, along);
                high = std::max(high, along);
            }
            const std::vector<Point2> hull = ConvexHull2D(projected);
            double smallest = 0.0;
            double angle = MinAreaRectangleAngle(hull, smallest);
            double area = smallest;
            double sumCos = 0.0, sumSin = 0.0, sideArea = 0.0;
            for (size_t t = 0; t < faceNormals.size(); ++t)
            {
                const Point3& n = faceNormals[t];
                if (std::fabs(Dot(n, d)) > 0.05)
                    continue;           // not a side seen along this axis
                const double phi = std::atan2(Dot(n, v), Dot(n, u));
                sumCos += faceAreas[t] * std::cos(4.0 * phi);
                sumSin += faceAreas[t] * std::sin(4.0 * phi);
                sideArea += faceAreas[t];
            }
            // Only when the sides agree on a direction at all: a round part scores
            // near 0 (Frank's popsicle dispenser, mostly round, scored 0.011).
            if (sideArea > 0.0 && std::hypot(sumCos, sumSin) > 0.005 * sideArea)
            {
                const double sideAngle = std::atan2(sumSin, sumCos) / 4.0;
                const double sideRectangle = RectangleArea(hull, sideAngle);
                if (sideRectangle <= smallest * kFaceAllowance)
                {
                    angle = sideAngle;
                    area = sideRectangle;
                }
            }
            Fit fit;
            fit.volume = area * (high - low);
            fit.axes[0] = d;
            fit.axes[1] = u * std::cos(angle) + v * std::sin(angle);
            fit.axes[2] = Cross(fit.axes[0], fit.axes[1]);
            fit.onFace = c < flatCount;
            fits.push_back(fit);
        }

        size_t chosen = 0;
        for (size_t i = 1; i < fits.size(); ++i)
            if (fits[i].volume < fits[chosen].volume * (1.0 - 1e-3))
                chosen = i;
        for (size_t i = 0; i < fits.size(); ++i)
        {
            if (fits[i].onFace && fits[i].volume <= fits[chosen].volume * kFaceAllowance)
            {
                chosen = i;
                break;
            }
        }
        Point3 bestAxes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        if (!fits.empty())
            for (int k = 0; k < 3; ++k)
                bestAxes[k] = fits[chosen].axes[k];

        // Its extent along each axis; then longest first, each pointing its
        // largest part positive, right-handed.
        double low[3] = { 1e300, 1e300, 1e300 }, high[3] = { -1e300, -1e300, -1e300 };
        for (const Point3& p : mesh.vertices)
        {
            for (int k = 0; k < 3; ++k)
            {
                const double d = Dot(p, bestAxes[k]);
                low[k] = std::min(low[k], d);
                high[k] = std::max(high[k], d);
            }
        }
        int order[3] = { 0, 1, 2 };
        std::sort(order, order + 3, [&](int a, int b) { return high[a] - low[a] > high[b] - low[b] + 1e-9; });

        box.centre = Point3{};
        for (int k = 0; k < 3; ++k)
            box.centre = box.centre + bestAxes[k] * (0.5 * (high[k] + low[k]));
        box.axes[0] = Canonical(bestAxes[order[0]]);
        box.axes[1] = Canonical(bestAxes[order[1]]);
        box.axes[2] = Normalised(Cross(box.axes[0], box.axes[1]));
        for (int k = 0; k < 3; ++k)
            box.halfSize[k] = 0.5 * (high[order[k]] - low[order[k]]);
        return box;
    }

    void Feature::ApplyTransform(const Transform& transform)
    {
        point = transform.Apply(point);
        if (kind != Kind::Point)
            direction = transform.ApplyDirection(direction);
        halfWidth *= MatrixScale(transform);
    }

    std::vector<Feature> BoxFeatures(const OrientedBox& box)
    {
        std::vector<Feature> features;
        const char letters[3] = { 'A', 'B', 'C' };
        const double largest = std::max({ box.halfSize[0], box.halfSize[1], box.halfSize[2] });
        for (int k = 0; k < 3; ++k)
        {
            Feature plane;
            plane.kind = Feature::Kind::Plane;
            plane.point = box.centre;
            plane.direction = box.axes[k];
            plane.name = std::string("Box plane ") + letters[k];
            plane.halfWidth = largest * 1.1;
            features.push_back(plane);
        }
        for (int k = 0; k < 3; ++k)
        {
            Feature axis;
            axis.kind = Feature::Kind::Axis;
            axis.point = box.centre;
            axis.direction = box.axes[k];
            axis.name = std::string("Box axis ") + letters[k];
            axis.halfWidth = box.halfSize[k] * 1.25 + largest * 0.05;
            features.push_back(axis);
        }
        Feature centre;
        centre.kind = Feature::Kind::Point;
        centre.point = box.centre;
        centre.name = "Box centre";
        features.push_back(centre);
        return features;
    }

    std::vector<std::array<int, 3>> TriangleNeighbours(const Mesh& mesh)
    {
        std::vector<std::array<int, 3>> neighbours(mesh.triangles.size(), { -1, -1, -1 });
        std::unordered_map<uint64_t, std::pair<int, int>> first;      // edge -> (triangle, side)
        first.reserve(mesh.triangles.size() * 2);
        for (size_t t = 0; t < mesh.triangles.size(); ++t)
        {
            const auto& tri = mesh.triangles[t];
            for (int k = 0; k < 3; ++k)
            {
                const uint64_t key = EdgeKey(tri[k], tri[(k + 1) % 3]);
                const auto found = first.find(key);
                if (found == first.end())
                {
                    first.emplace(key, std::make_pair(static_cast<int>(t), k));
                }
                else if (found->second.first >= 0)
                {
                    const int other = found->second.first, side = found->second.second;
                    if (neighbours[other][side] < 0)
                    {
                        neighbours[other][side] = static_cast<int>(t);
                        neighbours[t][k] = other;
                    }
                    found->second.first = -1;       // a third triangle on this edge isn't joined
                }
            }
        }
        return neighbours;
    }

    std::vector<int> GrowFlatArea(const Mesh& mesh, const std::vector<std::array<int, 3>>& neighbours, int seed,
                                  double angleDegrees, double distance)
    {
        std::vector<int> area;
        if (seed < 0 || static_cast<size_t>(seed) >= mesh.triangles.size())
            return area;

        const double cosLimit = std::cos(angleDegrees * kPi / 180.0);
        std::vector<char> visited(mesh.triangles.size(), 0);
        Moments moments;
        moments.Add(mesh, seed);
        Point3 normal = Normalised(TriangleNormal(mesh, seed));
        Point3 point = moments.Mean();
        size_t nextRefit = 8;

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
                const Point3 nn = Normalised(TriangleNormal(mesh, n));
                const auto& tri = mesh.triangles[n];
                const Point3 middle = (mesh.vertices[tri[0]] + mesh.vertices[tri[1]] + mesh.vertices[tri[2]]) / 3.0;
                if (Dot(nn, normal) < cosLimit || std::fabs(Dot(middle - point, normal)) > distance)
                    continue;
                area.push_back(n);
                queue.push_back(n);
                moments.Add(mesh, n);
                if (area.size() >= nextRefit)
                {
                    normal = moments.PlaneNormal();
                    point = moments.Mean();
                    nextRefit = area.size() * 2;
                }
            }
        }
        std::sort(area.begin(), area.end());
        return area;
    }

    bool FitPlane(const Mesh& mesh, const std::vector<int>& triangles, Feature& plane, double* rms)
    {
        Moments moments;
        for (int t : triangles)
            moments.Add(mesh, t);
        if (moments.area <= 0.0)
            return false;

        plane.kind = Feature::Kind::Plane;
        plane.point = moments.Mean();
        plane.direction = moments.PlaneNormal();

        double sum = 0.0, reach = 0.0;
        size_t count = 0;
        for (int t : triangles)
        {
            for (int v : mesh.triangles[t])
            {
                const Point3 d = mesh.vertices[v] - plane.point;
                const double h = Dot(d, plane.direction);
                sum += h * h;
                ++count;
                reach = std::max(reach, Length(Reject(d, plane.direction)));
            }
        }
        plane.halfWidth = reach * 1.1;
        if (rms != nullptr)
            *rms = count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
        return true;
    }

    namespace
    {
        // A circle through 2D points (x, y), least squares: the algebraic fit
        // (Kasa), then a few Gauss-Newton steps on the true distances.
        bool FitCircle(const std::vector<Point2>& points, Point2& centre, double& radius)
        {
            const size_t n = points.size();
            if (n < 3)
                return false;
            double mx = 0.0, my = 0.0;
            for (const Point2& p : points)
            {
                mx += p.x;
                my += p.y;
            }
            mx /= n;
            my /= n;

            // x^2 + y^2 + D x + E y + F = 0, about the mean for good scaling.
            double a[3][3] = {}, r[3] = {};
            for (const Point2& p : points)
            {
                const double x = p.x - mx, y = p.y - my;
                const double row[3] = { x, y, 1.0 };
                const double rhs = -(x * x + y * y);
                for (int i = 0; i < 3; ++i)
                {
                    for (int j = 0; j < 3; ++j)
                        a[i][j] += row[i] * row[j];
                    r[i] += row[i] * rhs;
                }
            }
            // Solve the 3 x 3 by Cramer's rule.
            auto det3 = [](const double m[3][3]) {
                return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                       m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                       m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
            };
            const double d = det3(a);
            if (std::fabs(d) < 1e-300)
                return false;
            double solution[3];
            for (int k = 0; k < 3; ++k)
            {
                double m[3][3];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        m[i][j] = (j == k) ? r[i] : a[i][j];
                solution[k] = det3(m) / d;
            }
            double cx = -solution[0] / 2.0, cy = -solution[1] / 2.0;
            double rr = cx * cx + cy * cy - solution[2];
            if (!(rr > 0.0))
                return false;
            double R = std::sqrt(rr);

            for (int iteration = 0; iteration < 20; ++iteration)
            {
                // Residuals d_i = |p - c| - R; unknowns (cx, cy, R).
                double jtj[3][3] = {}, jtr[3] = {};
                for (const Point2& p : points)
                {
                    const double x = p.x - mx - cx, y = p.y - my - cy;
                    const double length = std::hypot(x, y);
                    if (length < 1e-12)
                        continue;
                    const double j[3] = { -x / length, -y / length, -1.0 };
                    const double residual = length - R;
                    for (int i = 0; i < 3; ++i)
                    {
                        for (int k = 0; k < 3; ++k)
                            jtj[i][k] += j[i] * j[k];
                        jtr[i] += j[i] * residual;
                    }
                }
                const double dj = det3(jtj);
                if (std::fabs(dj) < 1e-300)
                    break;
                double step[3];
                for (int k = 0; k < 3; ++k)
                {
                    double m[3][3];
                    for (int i = 0; i < 3; ++i)
                        for (int jj = 0; jj < 3; ++jj)
                            m[i][jj] = (jj == k) ? -jtr[i] : jtj[i][jj];
                    step[k] = det3(m) / dj;
                }
                cx += step[0];
                cy += step[1];
                R += step[2];
                if (std::fabs(step[0]) + std::fabs(step[1]) + std::fabs(step[2]) < 1e-12 * (1.0 + R))
                    break;
            }
            centre = { cx + mx, cy + my };
            radius = std::fabs(R);
            return std::isfinite(radius) && radius > 0.0;
        }

        // The area-weighted spread of normals: the axis is the direction they're
        // least along; spread is how far round they turn (middle over largest).
        bool NormalAxis(const Mesh& mesh, const std::vector<int>& triangles, Point3& axis, double& spread)
        {
            double c[3][3] = {};
            for (int t : triangles)
            {
                const Point3 n2 = TriangleNormal(mesh, t);
                const double area = 0.5 * Length(n2);
                if (area == 0.0)
                    continue;
                const Point3 n = n2 / (2.0 * area);
                const double v[3] = { n.x, n.y, n.z };
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        c[i][j] += area * v[i] * v[j];
            }
            double values[3];
            Point3 vectors[3];
            SymmetricEigen(c, values, vectors);
            if (!(values[2] > 0.0))
                return false;
            axis = Canonical(vectors[0]);
            spread = values[1] / values[2];
            return true;
        }

        // A cylinder model while growing: axis line and radius.
        struct CylinderModel
        {
            bool valid = false;
            Point3 axis, centre;
            double radius = 0.0;
        };

        CylinderModel FitModel(const Mesh& mesh, const std::vector<int>& triangles)
        {
            CylinderModel model;
            CylinderFit fit;
            if (triangles.size() >= 12 && FitCylinder(mesh, triangles, fit))
            {
                model.valid = true;
                model.axis = fit.axis.direction;
                model.centre = fit.axis.point;
                model.radius = fit.radius;
            }
            return model;
        }
    }

    bool FitCylinder(const Mesh& mesh, const std::vector<int>& triangles, CylinderFit& fit)
    {
        Point3 axis;
        double spread = 0.0;
        // The normals must turn at least about 25 degrees round the axis.
        if (!NormalAxis(mesh, triangles, axis, spread) || spread < 0.015)
            return false;

        const Point3 u = Perpendicular(axis);
        const Point3 v = Cross(axis, u);
        std::vector<int> corners;
        for (int t : triangles)
            for (int k : mesh.triangles[t])
                corners.push_back(k);
        std::sort(corners.begin(), corners.end());
        corners.erase(std::unique(corners.begin(), corners.end()), corners.end());

        std::vector<Point2> projected;
        double low = 1e300, high = -1e300;
        for (int k : corners)
        {
            const Point3& p = mesh.vertices[k];
            projected.push_back({ Dot(p, u), Dot(p, v) });
            low = std::min(low, Dot(p, axis));
            high = std::max(high, Dot(p, axis));
        }
        Point2 centre2;
        double radius = 0.0;
        if (!FitCircle(projected, centre2, radius))
            return false;

        const Point3 onAxis = u * centre2.x + v * centre2.y;
        fit.axis.kind = Feature::Kind::Axis;
        fit.axis.direction = axis;
        fit.axis.point = onAxis + axis * (0.5 * (low + high));
        fit.axis.halfWidth = 0.5 * (high - low) * 1.25 + radius * 0.25;
        fit.radius = radius;

        double sum = 0.0;
        for (const Point2& p : projected)
        {
            const double d = std::hypot(p.x - centre2.x, p.y - centre2.y) - radius;
            sum += d * d;
        }
        fit.rms = std::sqrt(sum / static_cast<double>(projected.size()));

        // A hole's triangles face the axis.
        double facing = 0.0;
        for (int t : triangles)
        {
            const Point3 n2 = TriangleNormal(mesh, t);
            const auto& tri = mesh.triangles[t];
            const Point3 middle = (mesh.vertices[tri[0]] + mesh.vertices[tri[1]] + mesh.vertices[tri[2]]) / 3.0;
            facing += Dot(n2, Reject(middle - fit.axis.point, axis));
        }
        fit.hole = facing < 0.0;
        return true;
    }

    std::vector<int> GrowCylinder(const Mesh& mesh, const std::vector<std::array<int, 3>>& neighbours, int seed,
                                  double angleDegrees, double distance)
    {
        std::vector<int> area;
        if (seed < 0 || static_cast<size_t>(seed) >= mesh.triangles.size())
            return area;

        const double cosSmooth = std::cos(angleDegrees * kPi / 180.0);
        const double cosRadial = std::cos(2.0 * angleDegrees * kPi / 180.0);
        std::vector<char> visited(mesh.triangles.size(), 0);
        std::vector<Point3> normals(mesh.triangles.size());
        auto normalOf = [&](int t) -> const Point3& {
            if (Length(normals[t]) == 0.0)
                normals[t] = Normalised(TriangleNormal(mesh, t));
            return normals[t];
        };

        CylinderModel model;
        size_t nextFit = 12;
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
                const Point3& nn = normalOf(n);
                if (Dot(nn, normalOf(t)) < cosSmooth)
                    continue;           // a crease: not visited, so another way round may still reach it
                if (model.valid)
                {
                    const auto& tri = mesh.triangles[n];
                    const Point3 middle =
                        (mesh.vertices[tri[0]] + mesh.vertices[tri[1]] + mesh.vertices[tri[2]]) / 3.0;
                    const Point3 out = Reject(middle - model.centre, model.axis);
                    const double r = Length(out);
                    if (std::fabs(r - model.radius) > distance || r == 0.0 ||
                        std::fabs(Dot(nn, out / r)) < cosRadial)
                        continue;
                }
                visited[n] = 1;
                area.push_back(n);
                queue.push_back(n);
                if (area.size() >= nextFit)
                {
                    const CylinderModel next = FitModel(mesh, area);
                    if (next.valid)
                        model = next;
                    nextFit = area.size() * 2;
                }
            }
        }
        std::sort(area.begin(), area.end());
        return area;
    }

    namespace
    {
        // Removes direction c from an orthonormal set, keeping it orthonormal.
        void RemoveDirection(std::vector<Point3>& slides, const Point3& c)
        {
            std::vector<Point3> kept;
            for (const Point3& b : slides)
            {
                Point3 r = Reject(b, c);
                for (const Point3& k : kept)
                    r = Reject(r, k);
                if (Length(r) > 1e-6)
                    kept.push_back(Normalised(r));
            }
            slides.swap(kept);
        }

        // The smallest slide within the free directions that meets each
        // condition dot(rows[i], v) = rhs[i] as nearly as it can.
        Point3 SolveSlide(const std::vector<Point3>& slides, const std::vector<Point3>& rows,
                          const std::vector<double>& rhs)
        {
            const size_t m = slides.size();
            if (m == 0 || rows.empty())
                return {};
            // (C B)^T (C B) y = (C B)^T r, with a touch of damping for the minimum norm.
            double a[3][3] = {}, r[3] = {};
            for (size_t i = 0; i < rows.size(); ++i)
            {
                double cb[3] = {};
                for (size_t j = 0; j < m; ++j)
                    cb[j] = Dot(rows[i], slides[j]);
                for (size_t j = 0; j < m; ++j)
                {
                    for (size_t k = 0; k < m; ++k)
                        a[j][k] += cb[j] * cb[k];
                    r[j] += cb[j] * rhs[i];
                }
            }
            for (size_t j = 0; j < m; ++j)
                a[j][j] += 1e-12;
            // Gaussian elimination on m <= 3.
            double y[3] = {};
            int order[3] = { 0, 1, 2 };
            for (size_t col = 0; col < m; ++col)
            {
                size_t pivot = col;
                for (size_t row = col + 1; row < m; ++row)
                    if (std::fabs(a[row][col]) > std::fabs(a[pivot][col]))
                        pivot = row;
                for (size_t k = 0; k < m; ++k)
                    std::swap(a[col][k], a[pivot][k]);
                std::swap(r[col], r[pivot]);
                if (std::fabs(a[col][col]) < 1e-300)
                    continue;
                for (size_t row = col + 1; row < m; ++row)
                {
                    const double f = a[row][col] / a[col][col];
                    for (size_t k = col; k < m; ++k)
                        a[row][k] -= f * a[col][k];
                    r[row] -= f * r[col];
                }
            }
            (void)order;
            for (size_t i = m; i-- > 0;)
            {
                double sum = r[i];
                for (size_t k = i + 1; k < m; ++k)
                    sum -= a[i][k] * y[k];
                y[i] = (std::fabs(a[i][i]) > 1e-300) ? sum / a[i][i] : 0.0;
            }
            Point3 v;
            for (size_t j = 0; j < m; ++j)
                v = v + slides[j] * y[j];
            return v;
        }

        // The signed turn about unit k taking a onto b, both laid square to k.
        double TurnAbout(const Point3& k, const Point3& a, const Point3& b)
        {
            const Point3 ap = Normalised(Reject(a, k)), bp = Normalised(Reject(b, k));
            return std::atan2(Dot(k, Cross(ap, bp)), Dot(ap, bp));
        }
    }

    bool AlignStep(const Feature& scan, const Feature& target, bool flip, AxisToPlane axisToPlane, AlignState& state,
                   Transform& move, std::string& message)
    {
        move = Transform();
        message.clear();
        if (state.Done())
        {
            message = "The scan is fully aligned. Start over to align again.";
            return false;
        }

        using Kind = Feature::Kind;
        const bool planePlane = scan.kind == Kind::Plane && target.kind == Kind::Plane;
        const bool axisAxis = scan.kind == Kind::Axis && target.kind == Kind::Axis;
        const bool axisPlane = scan.kind == Kind::Axis && target.kind == Kind::Plane;
        const bool pointPoint = scan.kind == Kind::Point && target.kind == Kind::Point;
        if (!planePlane && !axisAxis && !axisPlane && !pointPoint)
        {
            message = "Align a plane with a plane, an axis with an axis or a plane, or a point with a point.";
            return false;
        }
        const bool inPlane = axisPlane && axisToPlane == AxisToPlane::InPlane;

        // Where the scan's direction must end up. For a plane, an axis, or an
        // axis square to a plane: along the target's direction, the nearer way
        // round unless flipped. For an axis lying in a plane: the nearest
        // direction in the plane.
        const bool directed = !pointPoint;
        const Point3 t = Normalised(target.direction);
        Point3 s = Normalised(scan.direction);
        Transform turn;
        bool turned = false;
        if (directed)
        {
            Point3 goal;
            if (inPlane)
            {
                goal = Reject(s, t);
                goal = (Length(goal) > 1e-6) ? Normalised(goal) : Perpendicular(t);
                if (flip)
                    goal = goal * -1.0;
            }
            else
            {
                goal = (Dot(s, t) >= 0.0) ? t : t * -1.0;
                if (flip)
                    goal = goal * -1.0;
            }

            if (state.turn == AlignState::Turn::Free)
            {
                turn = Transform::RotationBetween(s, goal, scan.point);
                turned = true;
            }
            else if (state.turn == AlignState::Turn::About)
            {
                const Point3& k = state.turnAxis;
                const bool sAlong = Length(Reject(s, k)) < 0.02, gAlong = Length(Reject(goal, k)) < 0.02;
                if (sAlong != gAlong || std::fabs(std::fabs(Dot(s, k)) - std::fabs(Dot(goal, k))) > 0.02)
                {
                    message = "That would need turning the scan a way earlier steps have fixed. Choose geometry "
                              "square to what's lined up already, or start over.";
                    return false;
                }
                if (!sAlong)
                {
                    // About k, the nearer way unless flipped (for a plane or an axis onto an axis).
                    double angle = TurnAbout(k, s, goal);
                    if (!inPlane)
                    {
                        const double other = TurnAbout(k, s, goal * -1.0);
                        const bool firstNearer = std::fabs(angle) <= std::fabs(other);
                        angle = (firstNearer != flip) ? angle : other;
                    }
                    turn = Transform::Rotation(k, angle, state.turnPivot);
                    turned = true;
                }
            }
            else
            {
                const double off = std::fabs(Dot(s, goal));
                if (off < 0.9999)
                {
                    message = "That would need turning the scan, which earlier steps have fixed.";
                    return false;
                }
            }
        }

        // The conditions on where it ends up, as dot(row, slide) = value.
        const Point3 p = turn.Apply(scan.point);
        std::vector<Point3> rows;
        std::vector<double> values;
        if (planePlane || inPlane)
        {
            rows.push_back(t);
            values.push_back(Dot(target.point - p, t));
        }
        else if (axisAxis)
        {
            const Point3 e1 = Perpendicular(t), e2 = Cross(t, e1);
            for (const Point3& e : { e1, e2 })
            {
                rows.push_back(e);
                values.push_back(Dot(target.point - p, e));
            }
        }
        else if (pointPoint)
        {
            for (const Point3& e : { Point3{ 1, 0, 0 }, Point3{ 0, 1, 0 }, Point3{ 0, 0, 1 } })
            {
                rows.push_back(e);
                values.push_back(Dot(target.point - p, e));
            }
        }
        const Point3 slide = SolveSlide(state.slides, rows, values);
        bool reached = true;
        for (size_t i = 0; i < rows.size(); ++i)
            reached = reached && std::fabs(Dot(rows[i], slide) - values[i]) <= 0.005;
        // A step that turns may leave its position short (earlier steps hold it,
        // such as a hole already on an axis), and a point slides as near as the
        // free directions allow (a box's centre onto the origin after its face is
        // on XY centres it in X and Y); a plane or axis that only slides must reach.
        if (!reached && !turned && !pointPoint)
        {
            message = "That would need sliding the scan a way earlier steps have fixed. Choose geometry the scan "
                      "can still slide onto, or start over.";
            return false;
        }

        // What this step fixes.
        AlignState next = state;
        if (directed)
        {
            if (state.turn == AlignState::Turn::Free)
            {
                next.turn = AlignState::Turn::About;
                next.turnAxis = t;      // a plane's normal, an axis, or the plane an axis lies in
                next.turnPivot = target.point;
            }
            else if (state.turn == AlignState::Turn::About && turned)
            {
                next.turn = AlignState::Turn::None;
            }
        }
        for (const Point3& row : rows)
            RemoveDirection(next.slides, row);
        if (next.turn == AlignState::Turn::About && !rows.empty())
            next.turnPivot = target.point;      // on what this step lined up, so turning keeps it

        const bool fixedSomething = next.turn != state.turn || next.slides.size() != state.slides.size();
        if (!fixedSomething)
        {
            message = "That lines up nothing new: earlier steps already hold it. Choose other geometry.";
            return false;
        }
        next.step = state.step + 1;
        state = next;
        move = turn.Then(Transform::Translation(slide));
        if (!reached)
            message = pointPoint ? "Moved as near as the directions still free allow."
                                 : "Turned to line up; earlier steps hold its position, so it isn't moved onto the "
                                   "target.";
        return true;
    }

    std::string FreedomText(const AlignState& state)
    {
        std::ostringstream text;
        text.precision(3);
        if (state.Done())
            return "nothing: fully aligned";
        bool any = false;
        if (state.turn == AlignState::Turn::Free)
        {
            text << "turning any way";
            any = true;
        }
        else if (state.turn == AlignState::Turn::About)
        {
            const Point3& k = state.turnAxis;
            text << "turning about (" << (std::fabs(k.x) < 5e-4 ? 0.0 : k.x) << ", "
                 << (std::fabs(k.y) < 5e-4 ? 0.0 : k.y) << ", " << (std::fabs(k.z) < 5e-4 ? 0.0 : k.z) << ")";
            any = true;
        }
        if (!state.slides.empty())
        {
            text << (any ? "; " : "") << "sliding in " << state.slides.size()
                 << (state.slides.size() == 1 ? " direction" : " directions");
        }
        return text.str();
    }
}
