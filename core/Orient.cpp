// Orient.cpp : moving, turning and scaling a scan, and lining it up.

#include "Orient.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
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

    OrientedBox FitOrientedBox(const Mesh& mesh)
    {
        OrientedBox box;
        if (mesh.vertices.empty())
            return box;

        Moments moments;
        for (size_t t = 0; t < mesh.triangles.size(); ++t)
            moments.Add(mesh, static_cast<int>(t));

        Point3 axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        if (moments.area > 0.0)
        {
            double c[3][3];
            moments.Covariance(c);
            double values[3];
            Point3 vectors[3];
            SymmetricEigen(c, values, vectors);
            axes[0] = vectors[2];       // largest spread first
            axes[1] = vectors[1];
            // The same box whichever way the eigenvectors come out: each of the first
            // two points the way its largest part is positive.
            for (int k = 0; k < 2; ++k)
            {
                const Point3& a = axes[k];
                const double largest = (std::fabs(a.x) >= std::fabs(a.y) && std::fabs(a.x) >= std::fabs(a.z)) ? a.x
                                       : (std::fabs(a.y) >= std::fabs(a.z))                                   ? a.y
                                                                                                              : a.z;
                if (largest < 0.0)
                    axes[k] = a * -1.0;
            }
            axes[2] = Normalised(Cross(axes[0], axes[1]));
        }

        double low[3] = { 1e300, 1e300, 1e300 }, high[3] = { -1e300, -1e300, -1e300 };
        for (const Point3& p : mesh.vertices)
        {
            for (int k = 0; k < 3; ++k)
            {
                const double d = Dot(p, axes[k]);
                low[k] = std::min(low[k], d);
                high[k] = std::max(high[k], d);
            }
        }
        box.centre = Point3{};
        for (int k = 0; k < 3; ++k)
        {
            box.axes[k] = axes[k];
            box.halfSize[k] = 0.5 * (high[k] - low[k]);
            box.centre = box.centre + axes[k] * (0.5 * (high[k] + low[k]));
        }
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

    bool AlignStep(const Feature& scan, const Feature& target, bool flip, AlignState& state, Transform& move,
                   std::string& message)
    {
        move = Transform();
        message.clear();

        if (state.step >= 3)
        {
            message = "All three steps are done. Start over to align again.";
            return false;
        }
        if (scan.kind != target.kind)
        {
            message = "Align a plane with a plane, an axis with an axis, or a point with a point.";
            return false;
        }

        // A point moves only, within whatever's still free.
        if (scan.kind == Feature::Kind::Point)
        {
            Point3 d = target.point - scan.point;
            if (state.step == 1)
                d = state.firstWasPlane ? Reject(d, state.turnAxis) : state.turnAxis * Dot(d, state.turnAxis);
            else if (state.step == 2)
                d = state.slide * Dot(d, state.slide);
            move = Transform::Translation(d);
            if (state.step == 2)
                state.step = 3;
            return true;
        }

        const Point3 t = Normalised(target.direction);
        Point3 s = Normalised(scan.direction);

        if (state.step == 0)
        {
            // The nearer way round, unless flipped.
            Point3 goal = (Dot(s, t) >= 0.0) ? t : t * -1.0;
            if (flip)
                goal = goal * -1.0;
            const Transform turn = Transform::RotationBetween(s, goal, scan.point);
            const Point3 p = turn.Apply(scan.point);
            Point3 shift;
            if (scan.kind == Feature::Kind::Plane)
                shift = t * Dot(target.point - p, t);
            else
                shift = Reject(target.point - p, t);
            move = turn.Then(Transform::Translation(shift));
            state.step = 1;
            state.firstWasPlane = (scan.kind == Feature::Kind::Plane);
            state.turnAxis = t;
            state.turnPivot = target.point;
            return true;
        }

        if (state.step == 1)
        {
            const Point3 n = state.turnAxis;
            Point3 sp = Reject(s, n), tp = Reject(t, n);
            if (Length(sp) < 0.1 || Length(tp) < 0.1)
            {
                message = "That's parallel to what step 1 aligned, so it can't turn the scan. Choose one that "
                          "runs across it.";
                return false;
            }
            sp = Normalised(sp);
            tp = Normalised(tp);
            // Turn about n by the nearer of the two ways onto tp, or the other if flipped.
            double angle = std::atan2(Dot(n, Cross(sp, tp)), Dot(sp, tp));
            const bool nearer = std::fabs(angle) <= kPi / 2.0;
            if (nearer == flip)
                angle = (angle > 0.0) ? angle - kPi : angle + kPi;
            const Transform turn = Transform::Rotation(n, angle, state.turnPivot);
            const Point3 p = turn.Apply(scan.point);

            Point3 shift;
            if (state.firstWasPlane)
            {
                if (scan.kind == Feature::Kind::Plane)
                {
                    const Point3 u = Reject(t, n);
                    const double along = Dot(target.point - p, t);
                    shift = u * (along / Dot(u, u));
                    state.slide = Normalised(Cross(n, t));
                }
                else
                {
                    const Point3 w = Reject(target.point - p, t);
                    shift = Reject(w, n);
                    state.slide = Normalised(Reject(t, n));
                }
                state.step = 2;
            }
            else
            {
                // After an axis, only sliding along it is left, and this step uses it.
                if (scan.kind == Feature::Kind::Plane)
                {
                    const double denominator = Dot(n, t);
                    if (std::fabs(denominator) > 0.1)
                        shift = n * (Dot(target.point - p, t) / denominator);
                }
                else
                {
                    shift = n * Dot(target.point - p, n);
                }
                state.step = 3;
            }
            move = turn.Then(Transform::Translation(shift));
            return true;
        }

        // Step 3: slide only.
        if (scan.kind != Feature::Kind::Plane)
        {
            message = "The last step needs a plane onto a plane, or a point onto a point.";
            return false;
        }
        const double denominator = Dot(state.slide, t);
        if (std::fabs(denominator) < 0.1)
        {
            message = "Sliding along what's left can't reach that plane. Choose one square to the first two.";
            return false;
        }
        move = Transform::Translation(state.slide * (Dot(target.point - scan.point, t) / denominator));
        state.step = 3;
        return true;
    }
}
