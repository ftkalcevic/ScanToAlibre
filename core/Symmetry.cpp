// Symmetry.cpp : symmetry planes by mirror ICP, and mid planes.

#include "Symmetry.h"

#include "Compare.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <execution>
#include <numeric>
#include <random>

namespace scancore
{
    namespace
    {
        const double kPi = 3.14159265358979323846;

        // SCANTOALIBRE_SYMMETRY_DEBUG=1 prints each refinement step.
        bool Debugging()
        {
            static const bool on = [] {
                char* value = nullptr;
                size_t length = 0;
                const bool set = _dupenv_s(&value, &length, "SCANTOALIBRE_SYMMETRY_DEBUG") == 0 && value != nullptr &&
                                 value[0] == '1';
                free(value);
                return set;
            }();
            return on;
        }

        Point3 Mirror(const Point3& p, const Point3& normal, const Point3& through)
        {
            return p - normal * (2.0 * Dot(p - through, normal));
        }

        // Two unit directions square to n and to each other.
        void Perpendiculars(const Point3& n, Point3& u, Point3& v)
        {
            const Point3 other = (std::fabs(n.x) < 0.6) ? Point3{ 1, 0, 0 }
                                 : (std::fabs(n.y) < 0.6) ? Point3{ 0, 1, 0 }
                                                          : Point3{ 0, 0, 1 };
            u = Normalised(Cross(n, other));
            v = Cross(n, u);
        }

        Feature MakePlane(const Point3& normal, const Point3& through, const Point3& centre, double halfWidth)
        {
            Feature plane;
            plane.kind = Feature::Kind::Plane;
            plane.direction = Normalised(normal);
            plane.point = centre - plane.direction * Dot(centre - through, plane.direction);
            plane.halfWidth = halfWidth;
            return plane;
        }

        // The same plane, within an angle (radians) and a distance (mm, at the box's centre).
        bool SamePlane(const Feature& a, const Feature& b, double angle, double distance)
        {
            const double cosine = std::fabs(Dot(a.direction, b.direction));
            if (cosine < std::cos(angle))
                return false;
            return std::fabs(Dot(b.point - a.point, a.direction)) <= distance;
        }

        // Mirror ICP, point to plane. The plane is n . (x - centre) = e.
        Feature Refine(const TriangleGrid& grid, const std::vector<Point3>& samples, const Feature& start,
                       const Point3& centre, const SymmetryOptions& options, double search, int iterations)
        {
            Point3 n = Normalised(start.direction);
            double e = Dot(start.point - centre, n);
            double gate = search;

            struct Row
            {
                bool used = false;
                double j[3] = { 0, 0, 0 };
                double r = 0.0;
                double distance = 0.0;
            };
            std::vector<Row> rows(samples.size());
            std::vector<size_t> order(samples.size());
            std::iota(order.begin(), order.end(), size_t{ 0 });

            for (int iteration = 0; iteration < iterations; ++iteration)
            {
                Point3 u, v;
                Perpendiculars(n, u, v);
                const Point3 through = centre + n * e;
                std::for_each(std::execution::par, order.begin(), order.end(), [&](size_t i) {
                    Row& row = rows[i];
                    row.used = false;
                    const Point3& p = samples[i];
                    const Point3 m = Mirror(p, n, through);
                    const TriangleGrid::Nearest q = grid.Find(m, gate);
                    if (!q.found)
                        return;
                    const Point3& nq = grid.Normal(q.triangle);
                    const Point3 local = p - centre;
                    const double s = Dot(n, local) - e;
                    // M(p) = p - 2 (n . (p - centre) - e) n, n turned by a u + b v, e moved by c.
                    const Point3 da = (n * Dot(u, local) + u * s) * -2.0;
                    const Point3 db = (n * Dot(v, local) + v * s) * -2.0;
                    const Point3 dc = n * 2.0;
                    row.used = true;
                    row.j[0] = Dot(nq, da);
                    row.j[1] = Dot(nq, db);
                    row.j[2] = Dot(nq, dc);
                    row.r = Dot(nq, m - q.point);
                    row.distance = q.distance;
                });

                double a[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
                double g[3] = { 0, 0, 0 };
                double sumSquared = 0.0;
                size_t used = 0;
                for (const Row& row : rows)
                {
                    if (!row.used)
                        continue;
                    ++used;
                    sumSquared += row.distance * row.distance;
                    for (int r = 0; r < 3; ++r)
                    {
                        g[r] -= row.j[r] * row.r;
                        for (int c = 0; c < 3; ++c)
                            a[r][c] += row.j[r] * row.j[c];
                    }
                }
                if (used < 10)
                    break;
                const double rms = std::sqrt(sumSquared / static_cast<double>(used));

                // Solve with a little damping, in case some turn isn't held (a round part).
                const double trace = a[0][0] + a[1][1] + a[2][2];
                for (int k = 0; k < 3; ++k)
                    a[k][k] += 1e-9 * trace + 1e-12;
                double m[3][4];
                for (int r = 0; r < 3; ++r)
                {
                    for (int c = 0; c < 3; ++c)
                        m[r][c] = a[r][c];
                    m[r][3] = g[r];
                }
                bool solved = true;
                for (int col = 0; col < 3 && solved; ++col)
                {
                    int pivot = col;
                    for (int r = col + 1; r < 3; ++r)
                        if (std::fabs(m[r][col]) > std::fabs(m[pivot][col]))
                            pivot = r;
                    if (!(std::fabs(m[pivot][col]) > 1e-300))
                    {
                        solved = false;
                        break;
                    }
                    if (pivot != col)
                        for (int c = 0; c < 4; ++c)
                            std::swap(m[col][c], m[pivot][c]);
                    for (int r = 0; r < 3; ++r)
                    {
                        if (r == col)
                            continue;
                        const double f = m[r][col] / m[col][col];
                        for (int c = col; c < 4; ++c)
                            m[r][c] -= f * m[col][c];
                    }
                }
                if (!solved)
                    break;
                double step[3] = { m[0][3] / m[0][0], m[1][3] / m[1][1], m[2][3] / m[2][2] };

                // At most 5 degrees and the search distance a step.
                const double turn = std::hypot(step[0], step[1]);
                const double maxTurn = 5.0 * kPi / 180.0;
                if (turn > maxTurn)
                {
                    step[0] *= maxTurn / turn;
                    step[1] *= maxTurn / turn;
                }
                step[2] = std::clamp(step[2], -search, search);

                n = Normalised(n + u * step[0] + v * step[1]);
                e += step[2];
                // Never under the tolerance: a shrinking gate drops pairs, so the
                // rms falls, so the gate shrinks again, without end.
                gate = std::min(search, std::max(3.0 * rms, options.tolerance));
                if (Debugging())
                    std::printf("symmetry %d: %zu pairs, rms %.5f, turn %.6f degrees, move %.6f mm\n", iteration, used,
                                rms, std::atan(std::hypot(step[0], step[1])) * 180.0 / kPi, step[2]);
                if (std::atan(std::hypot(step[0], step[1])) < 0.001 * kPi / 180.0 && std::fabs(step[2]) < 0.001)
                    break;
            }
            return MakePlane(n, centre + n * e, centre, start.halfWidth);
        }
    }

    std::vector<Point3> SurfaceSamples(const Mesh& mesh, size_t count)
    {
        std::vector<Point3> samples;
        if (mesh.triangles.empty() || count == 0)
            return samples;
        std::vector<double> cumulative(mesh.triangles.size());
        double total = 0.0;
        for (size_t t = 0; t < mesh.triangles.size(); ++t)
        {
            const auto& tri = mesh.triangles[t];
            const Point3& a = mesh.vertices[static_cast<size_t>(tri[0])];
            const Point3& b = mesh.vertices[static_cast<size_t>(tri[1])];
            const Point3& c = mesh.vertices[static_cast<size_t>(tri[2])];
            total += 0.5 * Length(Cross(b - a, c - a));
            cumulative[t] = total;
        }
        if (!(total > 0.0))
            return samples;
        std::mt19937_64 random(12345);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        samples.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            const double at = unit(random) * total;
            const size_t t = std::min(
                static_cast<size_t>(std::upper_bound(cumulative.begin(), cumulative.end(), at) - cumulative.begin()),
                cumulative.size() - 1);
            double r1 = unit(random);
            double r2 = unit(random);
            if (r1 + r2 > 1.0)
            {
                r1 = 1.0 - r1;
                r2 = 1.0 - r2;
            }
            const auto& tri = mesh.triangles[t];
            const Point3& a = mesh.vertices[static_cast<size_t>(tri[0])];
            const Point3& b = mesh.vertices[static_cast<size_t>(tri[1])];
            const Point3& c = mesh.vertices[static_cast<size_t>(tri[2])];
            samples.push_back(a + (b - a) * r1 + (c - a) * r2);
        }
        return samples;
    }

    SymmetryScore ScoreMirror(const TriangleGrid& grid, const std::vector<Point3>& samples, const Feature& plane,
                              double search, double tolerance)
    {
        SymmetryScore score;
        if (samples.empty())
            return score;
        const Point3 n = Normalised(plane.direction);
        std::vector<double> distances(samples.size(), -1.0);
        std::vector<size_t> order(samples.size());
        std::iota(order.begin(), order.end(), size_t{ 0 });
        std::for_each(std::execution::par, order.begin(), order.end(), [&](size_t i) {
            const TriangleGrid::Nearest q = grid.Find(Mirror(samples[i], n, plane.point), search);
            if (q.found)
                distances[i] = q.distance;
        });
        size_t matched = 0, within = 0;
        double sumSquared = 0.0;
        double sumTrimmed = 0.0;
        for (double d : distances)
        {
            const double capped = (d < 0.0) ? tolerance : std::min(d, tolerance);
            sumTrimmed += capped * capped;
            if (d < 0.0)
                continue;
            ++matched;
            sumSquared += d * d;
            within += (d <= tolerance) ? 1 : 0;
        }
        const double count = static_cast<double>(samples.size());
        score.matched = static_cast<double>(matched) / count;
        score.within = static_cast<double>(within) / count;
        score.rms = matched > 0 ? std::sqrt(sumSquared / static_cast<double>(matched)) : 0.0;
        score.trimmed = std::sqrt(sumTrimmed / count);
        return score;
    }

    std::vector<SymmetryCandidate> FindSymmetryPlanes(const Mesh& mesh, const OrientedBox& box,
                                                      const std::vector<Feature>& fitted,
                                                      const SymmetryOptions& options)
    {
        std::vector<SymmetryCandidate> result;
        const std::vector<Point3> samples = SurfaceSamples(mesh, options.samples);
        if (samples.size() < 10)
            return result;

        const double diagonal = 2.0 * std::sqrt(box.halfSize[0] * box.halfSize[0] + box.halfSize[1] * box.halfSize[1] +
                                                box.halfSize[2] * box.halfSize[2]);
        const double search = (options.search > 0.0) ? options.search : std::max(1.0, 0.03 * diagonal);
        const double halfWidth = std::max({ box.halfSize[0], box.halfSize[1], box.halfSize[2] }) * 1.1;
        // Cells a few triangles across, so each search looks at few triangles,
        // but no smaller than a sixteenth of the search, so a point with nothing
        // near looks through few empty cells.
        const double spacing = std::sqrt(SurfaceArea(mesh) / static_cast<double>(mesh.triangles.size()));
        const TriangleGrid grid(mesh, std::min(search / 4.0, std::max(3.0 * spacing, search / 16.0)));

        // The samples' centroid and principal directions.
        Point3 centroid;
        for (const Point3& p : samples)
            centroid = centroid + p;
        centroid = centroid / static_cast<double>(samples.size());
        double covariance[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
        for (const Point3& p : samples)
        {
            const double d[3] = { p.x - centroid.x, p.y - centroid.y, p.z - centroid.z };
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    covariance[r][c] += d[r] * d[c];
        }
        double values[3];
        Point3 principal[3];
        SymmetricEigen(covariance, values, principal);

        std::vector<Feature> starts;
        auto add = [&](const Point3& normal, const Point3& through) {
            if (Length(normal) < 1e-6)
                return;
            const Feature plane = MakePlane(normal, through, box.centre, halfWidth);
            for (const Feature& f : starts)
                if (SamePlane(f, plane, kPi / 180.0, 0.5))
                    return;
            starts.push_back(plane);
        };
        for (int k = 0; k < 3; ++k)
            add(box.axes[k], box.centre);
        for (int k = 0; k < 3; ++k)
            add(principal[k], centroid);
        for (const Feature& f : fitted)
        {
            const Point3 d = Normalised(f.direction);
            if (f.kind == Feature::Kind::Plane || f.kind == Feature::Kind::Axis)
            {
                add(d, centroid);
                add(d, box.centre);
            }
            if (f.kind == Feature::Kind::Axis)
                for (int k = 0; k < 3; ++k)
                    add(box.axes[k] - d * Dot(box.axes[k], d), f.point);
        }

        std::vector<SymmetryCandidate> scored;
        for (const Feature& plane : starts)
            scored.push_back({ plane, ScoreMirror(grid, samples, plane, search, options.tolerance) });
        std::stable_sort(scored.begin(), scored.end(), [](const SymmetryCandidate& a, const SymmetryCandidate& b) {
            return a.score.trimmed < b.score.trimmed;
        });
        if (scored.size() > options.refine)
            scored.resize(options.refine);

        // Each start a few steps, then only the best of them to the end.
        const int firstSteps = std::min(options.iterations, 8);
        std::vector<SymmetryCandidate> rough;
        for (const SymmetryCandidate& start : scored)
        {
            SymmetryCandidate refined;
            refined.plane = Refine(grid, samples, start.plane, box.centre, options, search, firstSteps);
            refined.score = ScoreMirror(grid, samples, refined.plane, search, options.tolerance);
            rough.push_back(refined);
        }
        std::stable_sort(rough.begin(), rough.end(), [](const SymmetryCandidate& a, const SymmetryCandidate& b) {
            return a.score.trimmed < b.score.trimmed;
        });
        if (!rough.empty() && options.iterations > firstSteps)
        {
            rough.front().plane = Refine(grid, samples, rough.front().plane, box.centre, options, search,
                                         options.iterations - firstSteps);
            rough.front().score = ScoreMirror(grid, samples, rough.front().plane, search, options.tolerance);
        }
        for (const SymmetryCandidate& refined : rough)
        {
            bool same = false;
            for (SymmetryCandidate& other : result)
                if (SamePlane(other.plane, refined.plane, 0.1 * kPi / 180.0, 0.1))
                {
                    same = true;
                    if (refined.score.trimmed < other.score.trimmed)
                        other = refined;
                }
            if (!same)
                result.push_back(refined);
        }
        std::stable_sort(result.begin(), result.end(), [](const SymmetryCandidate& a, const SymmetryCandidate& b) {
            return a.score.trimmed < b.score.trimmed;
        });
        return result;
    }

    std::array<SymmetryScore, 3> ScoreWorldPlanes(const Mesh& mesh, const SymmetryOptions& options)
    {
        std::array<SymmetryScore, 3> scores{};
        const std::vector<Point3> samples = SurfaceSamples(mesh, options.samples);
        if (samples.size() < 10)
            return scores;
        const Box3 bounds = Bounds(mesh);
        const double search = (options.search > 0.0) ? options.search : std::max(1.0, 0.03 * Length(bounds.Size()));
        const double spacing = std::sqrt(SurfaceArea(mesh) / static_cast<double>(mesh.triangles.size()));
        const TriangleGrid grid(mesh, std::min(search / 4.0, std::max(3.0 * spacing, search / 16.0)));
        const Point3 normals[3] = { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } };
        for (int k = 0; k < 3; ++k)
        {
            Feature plane;
            plane.kind = Feature::Kind::Plane;
            plane.direction = normals[k];
            scores[static_cast<size_t>(k)] = ScoreMirror(grid, samples, plane, search, options.tolerance);
        }
        return scores;
    }

    bool MidPlane(const Feature& a, const Feature& b, Feature& plane, std::string& message)
    {
        const double halfWidth = std::max(a.halfWidth, b.halfWidth);
        if (a.kind != b.kind)
        {
            message = "Mid Plane needs two planes, two axes or two points.";
            return false;
        }
        if (a.kind == Feature::Kind::Plane)
        {
            const Point3 n1 = Normalised(a.direction);
            Point3 n2 = Normalised(b.direction);
            if (Dot(n1, n2) < 0.0)
                n2 = n2 * -1.0;
            const double d1 = Dot(n1, a.point);
            const double d2 = Dot(n2, b.point);
            if (Dot(n1, n2) > std::cos(1e-6) && std::fabs(d1 - d2) < 1e-6)
            {
                message = "They're the same plane.";
                return false;
            }
            const Point3 sum = n1 + n2;
            const double length = Length(sum);
            const Point3 normal = sum / length;
            const double offset = (d1 + d2) / length;
            const Point3 middle = (a.point + b.point) * 0.5;
            plane.kind = Feature::Kind::Plane;
            plane.direction = normal;
            plane.point = middle - normal * (Dot(normal, middle) - offset);
            plane.halfWidth = halfWidth;
            return true;
        }

        Point3 c1 = a.point, c2 = b.point;
        if (a.kind == Feature::Kind::Axis)
        {
            const Point3 d1 = Normalised(a.direction);
            const Point3 d2 = Normalised(b.direction);
            const Point3 w = a.point - b.point;
            const double bb = Dot(d1, d2);
            const double denominator = 1.0 - bb * bb;
            if (denominator < 1e-12)
            {
                c2 = b.point + d2 * Dot(a.point - b.point, d2);     // parallel: across from a's point
            }
            else
            {
                const double s = (bb * Dot(d2, w) - Dot(d1, w)) / denominator;
                const double t = (Dot(d2, w) - bb * Dot(d1, w)) / denominator;
                c1 = a.point + d1 * s;
                c2 = b.point + d2 * t;
            }
        }
        const Point3 joining = c2 - c1;
        if (Length(joining) < 1e-6)
        {
            message = (a.kind == Feature::Kind::Axis) ? "The axes meet, so there's no plane between them."
                                                      : "They're the same point.";
            return false;
        }
        plane.kind = Feature::Kind::Plane;
        plane.direction = Normalised(joining);
        plane.point = (c1 + c2) * 0.5;
        plane.halfWidth = halfWidth > 0.0 ? halfWidth : Length(joining);
        return true;
    }
}
