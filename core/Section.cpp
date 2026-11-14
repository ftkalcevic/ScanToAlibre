// Section.cpp : sections through a scan, for sketches.

#include "Section.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace scancore
{
    namespace
    {
        const double kPi = 3.14159265358979323846;

        Vec2 operator+(const Vec2& a, const Vec2& b) { return { a.x + b.x, a.y + b.y }; }
        Vec2 operator-(const Vec2& a, const Vec2& b) { return { a.x - b.x, a.y - b.y }; }
        Vec2 operator*(const Vec2& a, double s) { return { a.x * s, a.y * s }; }
        double Dot2(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
        double Cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
        double Length2(const Vec2& a) { return std::hypot(a.x, a.y); }
        Vec2 Unit2(const Vec2& a)
        {
            const double l = Length2(a);
            return (l > 0.0) ? a * (1.0 / l) : Vec2{};
        }

        uint64_t EdgeKey(int a, int b)
        {
            if (a > b)
                std::swap(a, b);
            return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
        }

        double DistanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b)
        {
            const Vec2 ab = b - a;
            const double length2 = Dot2(ab, ab);
            double t = (length2 > 0.0) ? Dot2(p - a, ab) / length2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            return Length2(p - (a + ab * t));
        }

        // The least-squares circle through a and b (its centre on their
        // perpendicular bisector) fitted to the points between.
        bool CircleThroughEnds(const std::vector<Vec2>& q, int i, int j, Vec2& centre, double& radius)
        {
            const Vec2 a = q[i], b = q[j];
            const Vec2 m = (a + b) * 0.5;
            const Vec2 chord = b - a;
            const double length = Length2(chord);
            if (length <= 0.0)
                return false;
            const Vec2 n{ -chord.y / length, chord.x / length };
            // |p - c|^2 - |a - c|^2 with c = m + t n is |p-m|^2 - |a-m|^2 - 2t n.(p - a).
            double sumAB = 0.0, sumBB = 0.0;
            const Vec2 am = a - m;
            for (int k = i + 1; k < j; ++k)
            {
                const Vec2 pm = q[k] - m;
                const double A = Dot2(pm, pm) - Dot2(am, am);
                const double B = Dot2(n, q[k] - a);
                sumAB += A * B;
                sumBB += B * B;
            }
            if (sumBB < 1e-24)
                return false;
            const double t = sumAB / (2.0 * sumBB);
            centre = m + n * t;
            radius = Length2(a - centre);
            return std::isfinite(radius);
        }

        bool LineFits(const std::vector<Vec2>& q, int i, int j, double tolerance, double* deviation = nullptr)
        {
            double worst = 0.0;
            for (int k = i + 1; k < j; ++k)
            {
                const double d = DistanceToSegment(q[k], q[i], q[j]);
                if (d > tolerance)
                    return false;
                worst = std::max(worst, d);
            }
            if (deviation != nullptr)
                *deviation = worst;
            return true;
        }

        // The points from i to j on one arc within tolerance, less than a
        // whole turn. (Whether a line would do is the caller's question.)
        bool ArcFits(const std::vector<Vec2>& q, int i, int j, double tolerance, ProfileSegment* arc = nullptr,
                     double* deviation = nullptr)
        {
            if (j - i < 3)
                return false;
            Vec2 c;
            double r = 0.0;
            if (!CircleThroughEnds(q, i, j, c, r))
                return false;
            double worst = 0.0, sweep = 0.0;
            for (int k = i; k < j; ++k)
            {
                if (k > i)
                {
                    const double d = std::fabs(Length2(q[k] - c) - r);
                    if (d > tolerance)
                        return false;
                    worst = std::max(worst, d);
                }
                sweep += std::atan2(Cross2(q[k] - c, q[k + 1] - c), Dot2(q[k] - c, q[k + 1] - c));
            }
            if (std::fabs(sweep) >= 2.0 * kPi - 1e-6)
                return false;
            if (arc != nullptr)
            {
                arc->kind = ProfileSegment::Kind::Arc;
                arc->start = q[i];
                arc->end = q[j];
                arc->centre = c;
                arc->radius = r;
                arc->anticlockwise = sweep > 0.0;
            }
            if (deviation != nullptr)
                *deviation = worst;
            return true;
        }

        // The furthest j from i for which fits(i, j) holds, doubling then halving.
        template <class Fits>
        int Furthest(int i, int last, Fits fits)
        {
            int good = i + 1;
            int step = 1;
            int bad = -1;
            while (true)
            {
                const int j = std::min(i + step * 2, last);
                if (j <= good)
                    break;
                if (fits(j))
                {
                    good = j;
                    if (j == last)
                        break;
                    step *= 2;
                }
                else
                {
                    bad = j;
                    break;
                }
            }
            if (bad < 0)
                return good;
            while (bad - good > 1)
            {
                const int mid = (good + bad) / 2;
                if (fits(mid))
                    good = mid;
                else
                    bad = mid;
            }
            return good;
        }

        double TurnAt(const std::vector<Vec2>& p, size_t k, size_t reach)
        {
            const size_t n = p.size();
            const Vec2 before = Unit2(p[k] - p[(k + n - reach) % n]);
            const Vec2 after = Unit2(p[(k + reach) % n] - p[k]);
            return std::fabs(std::atan2(Cross2(before, after), Dot2(before, after)));
        }

        // Points i to j as one line or arc within tolerance.
        bool FitRun(const std::vector<Vec2>& q, int i, int j, ProfileSegment::Kind kind, double tolerance,
                    ProfileSegment& segment, double& deviation)
        {
            if (j <= i)
                return false;
            if (kind == ProfileSegment::Kind::Arc)
            {
                if (!ArcFits(q, i, j, tolerance, &segment, &deviation))
                    return false;
            }
            else
            {
                if (!LineFits(q, i, j, tolerance, &deviation))
                    return false;
                segment.kind = ProfileSegment::Kind::Line;
                segment.start = q[i];
                segment.end = q[j];
            }
            segment.firstPoint = i;
            segment.lastPoint = j;
            return true;
        }

        // The sum of the squared distances of points i to j from a segment.
        double SquaredError(const std::vector<Vec2>& q, const ProfileSegment& segment)
        {
            double sum = 0.0;
            for (int k = segment.firstPoint; k <= segment.lastPoint; ++k)
            {
                const double d = (segment.kind == ProfileSegment::Kind::Line)
                                     ? DistanceToSegment(q[k], segment.start, segment.end)
                                     : Length2(q[k] - segment.centre) - segment.radius;
                sum += d * d;
            }
            return sum;
        }

        // The greedy fit lets a line run past a tangent point into the arc
        // after it (it's still within tolerance), which tilts the arc's start.
        // Each join moves to where its two segments fit best, by their summed
        // squared errors (the worse one's largest error stalls with both ends
        // of an arc wrong).
        // Each join depends on its neighbours', so it goes round a few times.
        void RefineJunctions(const std::vector<Vec2>& q, std::vector<ProfileSegment>& segments, double tolerance)
        {
            for (int pass = 0; pass < 6; ++pass)
            {
            bool moved = false;
            for (size_t s = 0; s + 1 < segments.size(); ++s)
            {
                ProfileSegment& a = segments[s];
                ProfileSegment& b = segments[s + 1];
                if (a.kind == ProfileSegment::Kind::Circle || b.kind == ProfileSegment::Kind::Circle)
                    continue;
                const int window = std::min({ 200, (a.lastPoint - a.firstPoint) / 2, (b.lastPoint - b.firstPoint) / 2 });
                if (window < 1)
                    continue;
                ProfileSegment bestA = a, bestB = b;
                double best = 1e300;
                for (int k = a.lastPoint - window; k <= a.lastPoint + window; ++k)
                {
                    ProfileSegment na, nb;
                    double da = 0.0, db = 0.0;
                    if (!FitRun(q, a.firstPoint, k, a.kind, tolerance, na, da) ||
                        !FitRun(q, k, b.lastPoint, b.kind, tolerance, nb, db))
                        continue;
                    const double score = SquaredError(q, na) + SquaredError(q, nb);
                    if (score < best - 1e-12)
                    {
                        best = score;
                        bestA = na;
                        bestB = nb;
                    }
                }
                moved = moved || bestA.lastPoint != a.lastPoint;
                a = bestA;
                b = bestB;
            }
            if (!moved)
                break;
            }
        }

        // A closed loop starting partway along a line or arc ends with the
        // rest of it: join the last segment and the first into one.
        double MergeAcrossStart(const std::vector<Vec2>& q, std::vector<ProfileSegment>& segments, double tolerance)
        {
            if (segments.size() < 3)
                return 0.0;
            const ProfileSegment& first = segments.front();
            const ProfileSegment& last = segments.back();
            if (first.kind != last.kind || first.kind == ProfileSegment::Kind::Circle)
                return 0.0;
            // q ends with its first point again, so last ends where first starts.
            std::vector<Vec2> run(q.begin() + last.firstPoint, q.end());
            run.insert(run.end(), q.begin() + 1, q.begin() + first.lastPoint + 1);
            ProfileSegment merged;
            double deviation = 0.0;
            if (!FitRun(run, 0, static_cast<int>(run.size()) - 1, first.kind, tolerance, merged, deviation))
                return 0.0;
            merged.firstPoint = last.firstPoint;
            merged.lastPoint = first.lastPoint;     // wraps past the start
            segments.pop_back();
            segments.front() = merged;
            return deviation;
        }

        // The least-squares line (principal direction) and circle (Kasa, then
        // geometric Gauss-Newton) through points.
        void FitLeastSquares(const std::vector<Vec2>& points, ProfileSegment& segment)
        {
            if (points.size() < 2)
                return;
            Vec2 mean;
            for (const Vec2& q : points)
                mean = mean + q;
            mean = mean * (1.0 / points.size());
            if (segment.kind == ProfileSegment::Kind::Line)
            {
                double sxx = 0, syy = 0, sxy = 0;
                for (const Vec2& q : points)
                {
                    const Vec2 d = q - mean;
                    sxx += d.x * d.x;
                    syy += d.y * d.y;
                    sxy += d.x * d.y;
                }
                const double angle = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
                Vec2 direction{ std::cos(angle), std::sin(angle) };
                if (Dot2(direction, segment.end - segment.start) < 0.0)
                    direction = direction * -1.0;
                segment.fitPoint = mean;
                segment.fitDirection = direction;
                return;
            }
            Vec2 c = segment.centre;
            double r = segment.radius;
            for (int iteration = 0; iteration < 20; ++iteration)
            {
                // Rows d(r_k)/d(cx, cy, r) = (-(qx-cx)/l, -(qy-cy)/l, -1)... solved as
                // normal equations for the step.
                double a[3][3] = {}, b[3] = {};
                for (const Vec2& q : points)
                {
                    const Vec2 d = q - c;
                    const double l = Length2(d);
                    if (l <= 0.0)
                        continue;
                    const double row[3] = { -d.x / l, -d.y / l, -1.0 };
                    const double residual = l - r;
                    for (int i = 0; i < 3; ++i)
                    {
                        b[i] -= row[i] * residual;
                        for (int j = 0; j < 3; ++j)
                            a[i][j] += row[i] * row[j];
                    }
                }
                // Cramer's rule.
                auto det3 = [](const double m[3][3]) {
                    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
                };
                const double det = det3(a);
                if (std::fabs(det) < 1e-30)
                    break;
                double step[3];
                for (int k = 0; k < 3; ++k)
                {
                    double m[3][3];
                    for (int i = 0; i < 3; ++i)
                        for (int j = 0; j < 3; ++j)
                            m[i][j] = (j == k) ? b[i] : a[i][j];
                    step[k] = det3(m) / det;
                }
                c = c + Vec2{ step[0], step[1] };
                r += step[2];
                if (std::fabs(step[0]) + std::fabs(step[1]) + std::fabs(step[2]) < 1e-10)
                    break;
            }
            if (std::isfinite(r) && r > 0.0)
            {
                segment.fitCentre = c;
                segment.fitRadius = r;
            }
            else
            {
                segment.fitCentre = segment.centre;
                segment.fitRadius = segment.radius;
            }
        }

        // How far from tangent two segments are where they join, in radians,
        // judged from their least-squares fits: a line a radius from an arc's
        // centre, or two arcs whose centres are the sum or difference of their
        // radii apart, are tangent wherever the join falls.
        double FitTangentError(const ProfileSegment& a, const ProfileSegment& b)
        {
            const bool lineA = a.kind == ProfileSegment::Kind::Line;
            const bool lineB = b.kind == ProfileSegment::Kind::Line;
            if (lineA && lineB)
                return kPi;
            double gap = 0.0, radius = 0.0;
            if (lineA != lineB)
            {
                const ProfileSegment& line = lineA ? a : b;
                const ProfileSegment& arc = lineA ? b : a;
                const double distance = std::fabs(Cross2(line.fitDirection, arc.fitCentre - line.fitPoint));
                gap = std::fabs(distance - arc.fitRadius);
                radius = arc.fitRadius;
            }
            else
            {
                const double distance = Length2(a.fitCentre - b.fitCentre);
                // Turning the same way, one is inside the other.
                const double expected = (a.anticlockwise == b.anticlockwise) ? std::fabs(a.fitRadius - b.fitRadius)
                                                                             : a.fitRadius + b.fitRadius;
                gap = std::fabs(distance - expected);
                radius = std::min(a.fitRadius, b.fitRadius);
            }
            if (radius <= 0.0)
                return kPi;
            // A line gap from a circle of that radius meets it at about sqrt(2 gap / r).
            return std::sqrt(2.0 * gap / radius);
        }

        // The direction of travel at a segment's start or end.
        Vec2 Tangent(const ProfileSegment& s, bool atEnd)
        {
            if (s.kind == ProfileSegment::Kind::Line)
                return Unit2(s.end - s.start);
            const Vec2 r = (atEnd ? s.end : s.start) - s.centre;
            const Vec2 t{ -r.y, r.x };      // anticlockwise travel
            return Unit2(s.anticlockwise ? t : t * -1.0);
        }
    }

    Point3 SectionFrame::Normal() const
    {
        return Normalised(Cross(xAxis, yAxis));
    }

    Vec2 SectionFrame::ToPlane(const Point3& p) const
    {
        return { Dot(p - origin, xAxis), Dot(p - origin, yAxis) };
    }

    Point3 SectionFrame::ToWorld(const Vec2& p) const
    {
        return origin + xAxis * p.x + yAxis * p.y;
    }

    std::vector<SlicePolyline> SliceMesh(const Mesh& mesh, const Point3& point, const Point3& normalIn)
    {
        std::vector<SlicePolyline> slices;
        const Point3 normal = Normalised(normalIn);
        if (Length(normal) == 0.0)
            return slices;

        // Signed heights; a vertex exactly on the plane counts as just above it,
        // so every crossing is between two edges.
        std::vector<double> h(mesh.vertices.size());
        for (size_t v = 0; v < mesh.vertices.size(); ++v)
        {
            h[v] = Dot(mesh.vertices[v] - point, normal);
            if (h[v] == 0.0)
                h[v] = 1e-12;
        }

        std::unordered_map<uint64_t, Point3> crossing;
        std::unordered_map<uint64_t, std::vector<int>> segmentsAt;
        std::vector<std::pair<uint64_t, uint64_t>> segments;
        for (const auto& t : mesh.triangles)
        {
            uint64_t ends[2];
            int found = 0;
            for (int k = 0; k < 3 && found < 2; ++k)
            {
                const int a = t[k], b = t[(k + 1) % 3];
                if ((h[a] > 0.0) == (h[b] > 0.0))
                    continue;
                const uint64_t key = EdgeKey(a, b);
                if (crossing.find(key) == crossing.end())
                {
                    const double s = h[a] / (h[a] - h[b]);
                    crossing[key] = mesh.vertices[a] + (mesh.vertices[b] - mesh.vertices[a]) * s;
                }
                ends[found++] = key;
            }
            if (found != 2)
                continue;
            const int index = static_cast<int>(segments.size());
            segments.push_back({ ends[0], ends[1] });
            segmentsAt[ends[0]].push_back(index);
            segmentsAt[ends[1]].push_back(index);
        }

        std::vector<char> used(segments.size(), 0);
        auto walk = [&](int from, uint64_t key, std::vector<uint64_t>& keys) {
            int current = from;
            while (true)
            {
                int next = -1;
                for (int s : segmentsAt[key])
                    if (s != current && !used[s])
                    {
                        next = s;
                        break;
                    }
                if (next < 0)
                    return;
                used[next] = 1;
                key = (segments[next].first == key) ? segments[next].second : segments[next].first;
                keys.push_back(key);
                current = next;
            }
        };
        for (size_t s = 0; s < segments.size(); ++s)
        {
            if (used[s])
                continue;
            used[s] = 1;
            std::vector<uint64_t> forward{ segments[s].first, segments[s].second };
            walk(static_cast<int>(s), segments[s].second, forward);
            SlicePolyline slice;
            if (forward.size() > 2 && forward.back() == forward.front())
            {
                forward.pop_back();
                slice.closed = true;
            }
            else
            {
                std::vector<uint64_t> backward;
                walk(static_cast<int>(s), segments[s].first, backward);
                std::reverse(backward.begin(), backward.end());
                backward.insert(backward.end(), forward.begin(), forward.end());
                forward.swap(backward);
            }
            for (uint64_t key : forward)
            {
                const Point3& p = crossing[key];
                if (slice.points.empty() || Length(p - slice.points.back()) > 1e-9)
                    slice.points.push_back(p);
            }
            if (slice.closed && slice.points.size() > 1 && Length(slice.points.front() - slice.points.back()) <= 1e-9)
                slice.points.pop_back();
            if (slice.points.size() >= 2)
                slices.push_back(std::move(slice));
        }
        return slices;
    }

    namespace
    {
        // Lines and arcs along the points, greedily, then with their joins refined.
        std::vector<ProfileSegment> FitRuns(const std::vector<Vec2>& p, double tolerance)
        {
            std::vector<ProfileSegment> segments;
            const int last = static_cast<int>(p.size()) - 1;
            int i = 0;
            while (i < last)
            {
                const int lineEnd = Furthest(i, last, [&](int j) { return LineFits(p, i, j, tolerance); });
                const int arcEnd = Furthest(i, last, [&](int j) { return j - i < 3 || ArcFits(p, i, j, tolerance); });
                ProfileSegment segment;
                double deviation = 0.0;     // unused here
                int end = lineEnd;
                // An arc only where it reaches clearly further than a line: a
                // big arc can follow a line a little way round a corner.
                const bool arcFurther = arcEnd > lineEnd + 2 && (arcEnd - i) * 4 > (lineEnd - i) * 5;
                if (arcFurther && ArcFits(p, i, arcEnd, tolerance, &segment, &deviation))
                {
                    end = arcEnd;
                }
                else
                {
                    segment.kind = ProfileSegment::Kind::Line;
                    segment.start = p[i];
                    segment.end = p[lineEnd];
                    LineFits(p, i, lineEnd, tolerance, &deviation);
                }
                segment.firstPoint = i;
                segment.lastPoint = end;
                segments.push_back(segment);
                i = end;
            }
            RefineJunctions(p, segments, tolerance);
            return segments;
        }
    }

    Profile FitProfile(const std::vector<Vec2>& input, bool closed, double tolerance)
    {
        Profile profile;
        profile.closed = closed;
        if (input.size() < 2)
            return profile;

        std::vector<Vec2> p = input;
        size_t sharpestAt = 0;
        if (closed && p.size() >= 3)
        {
            // Start at the sharpest corner; a loop with none may be one circle.
            size_t sharpest = 0;
            double turn = -1.0;
            const size_t reach = std::max<size_t>(1, std::min<size_t>(3, p.size() / 8));
            for (size_t k = 0; k < p.size(); ++k)
            {
                const double a = TurnAt(p, k, reach);
                if (a > turn)
                {
                    turn = a;
                    sharpest = k;
                }
            }
            std::rotate(p.begin(), p.begin() + sharpest, p.end());
            sharpestAt = sharpest;

            if (turn < 20.0 * kPi / 180.0 && p.size() >= 8)
            {
                // Fit a circle to every point (Kasa, about the mean).
                double mx = 0.0, my = 0.0;
                for (const Vec2& q : p)
                {
                    mx += q.x;
                    my += q.y;
                }
                mx /= p.size();
                my /= p.size();
                double suu = 0, svv = 0, suv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
                for (const Vec2& q : p)
                {
                    const double u = q.x - mx, v = q.y - my;
                    suu += u * u; svv += v * v; suv += u * v;
                    suuu += u * u * u; svvv += v * v * v; suvv += u * v * v; svuu += v * u * u;
                }
                const double det = suu * svv - suv * suv;
                if (std::fabs(det) > 1e-24)
                {
                    const double r1 = 0.5 * (suuu + suvv), r2 = 0.5 * (svvv + svuu);
                    const Vec2 c{ (r1 * svv - r2 * suv) / det + mx, (r2 * suu - r1 * suv) / det + my };
                    double radius = 0.0;
                    for (const Vec2& q : p)
                        radius += Length2(q - c);
                    radius /= p.size();
                    double worst = 0.0;
                    for (const Vec2& q : p)
                        worst = std::max(worst, std::fabs(Length2(q - c) - radius));
                    if (worst <= tolerance)
                    {
                        ProfileSegment circle;
                        circle.kind = ProfileSegment::Kind::Circle;
                        circle.centre = c;
                        circle.radius = radius;
                        circle.start = circle.end = c + Vec2{ radius, 0.0 };
                        circle.firstPoint = 0;
                        circle.lastPoint = static_cast<int>(p.size()) - 1;
                        circle.fitCentre = c;
                        circle.fitRadius = radius;
                        profile.segments.push_back(circle);
                        profile.largestDeviation = worst;
                        return profile;
                    }
                }
            }
            p.push_back(p.front());     // closed back to the start
        }

        size_t offset = 0;      // input index of p[0]
        std::vector<ProfileSegment> segments = FitRuns(p, tolerance);
        double merged = 0.0;
        if (closed)
        {
            merged = MergeAcrossStart(p, segments, tolerance);
            // The join at the loop's start wasn't refined: fit again from a
            // join that was, so every join is.
            if (segments.size() >= 2)
            {
                const size_t n = p.size() - 1;
                const size_t start = static_cast<size_t>(segments[1].firstPoint) % n;
                p.pop_back();
                std::rotate(p.begin(), p.begin() + start, p.end());
                p.push_back(p.front());
                offset = (sharpestAt + start) % n;
                segments = FitRuns(p, tolerance);
                merged = MergeAcrossStart(p, segments, tolerance);
            }
            else
            {
                offset = sharpestAt;
            }
        }

        profile.largestDeviation = merged;
        for (const ProfileSegment& s : segments)
        {
            double deviation = 0.0;
            ProfileSegment refit;
            if (s.firstPoint < s.lastPoint && FitRun(p, s.firstPoint, s.lastPoint, s.kind, tolerance, refit, deviation))
                profile.largestDeviation = std::max(profile.largestDeviation, deviation);
        }
        for (ProfileSegment& s : segments)
        {
            std::vector<Vec2> covered;
            if (s.firstPoint <= s.lastPoint)
                covered.assign(p.begin() + s.firstPoint, p.begin() + s.lastPoint + 1);
            else
            {
                covered.assign(p.begin() + s.firstPoint, p.end());
                covered.insert(covered.end(), p.begin() + 1, p.begin() + s.lastPoint + 1);
            }
            FitLeastSquares(covered, s);
        }
        const size_t count = input.size();
        for (ProfileSegment& s : segments)
        {
            s.firstPoint = static_cast<int>((offset + s.firstPoint) % count);
            s.lastPoint = static_cast<int>((offset + s.lastPoint) % count);
        }
        profile.segments = std::move(segments);
        return profile;
    }

    std::vector<ProfileConstraint> FindConstraints(const std::vector<Profile>& profiles, const SnapOptions& options)
    {
        std::vector<ProfileConstraint> constraints;
        const double hv = std::sin(options.horizontalVerticalDegrees * kPi / 180.0);
        for (size_t pi = 0; pi < profiles.size(); ++pi)
        {
            const Profile& profile = profiles[pi];
            const size_t n = profile.segments.size();
            for (size_t s = 0; s < n; ++s)
            {
                const ProfileSegment& seg = profile.segments[s];
                if (seg.kind == ProfileSegment::Kind::Line && options.horizontalVertical)
                {
                    const Vec2 d = Unit2(seg.end - seg.start);
                    ProfileConstraint c;
                    c.profile = c.otherProfile = pi;
                    c.segment = c.otherSegment = s;
                    if (std::fabs(d.y) <= hv)
                    {
                        c.kind = ProfileConstraint::Kind::Horizontal;
                        constraints.push_back(c);
                    }
                    else if (std::fabs(d.x) <= hv)
                    {
                        c.kind = ProfileConstraint::Kind::Vertical;
                        constraints.push_back(c);
                    }
                }
                if (seg.kind != ProfileSegment::Kind::Line)
                {
                    ProfileConstraint c;
                    c.kind = ProfileConstraint::Kind::Radius;
                    c.profile = c.otherProfile = pi;
                    c.segment = c.otherSegment = s;
                    c.value = RoundTo(seg.fitRadius > 0.0 ? seg.fitRadius : seg.radius, options.roundTo);
                    constraints.push_back(c);
                }
                // Tangent to the next segment, where at least one is an arc.
                const bool hasNext = (s + 1 < n) || (profile.closed && n > 1);
                if (options.tangent && hasNext && seg.kind != ProfileSegment::Kind::Circle)
                {
                    const size_t t = (s + 1) % n;
                    const ProfileSegment& next = profile.segments[t];
                    const bool lines = seg.kind == ProfileSegment::Kind::Line && next.kind == ProfileSegment::Kind::Line;
                    // The ends' directions roughly (they pass through noisy end
                    // points), the fits closely.
                    const double cosRough = std::cos(std::min(3.0 * options.tangentDegrees, 45.0) * kPi / 180.0);
                    if (!lines && Dot2(Tangent(seg, true), Tangent(next, false)) >= cosRough &&
                        FitTangentError(seg, next) <= options.tangentDegrees * kPi / 180.0)
                    {
                        ProfileConstraint c;
                        c.kind = ProfileConstraint::Kind::Tangent;
                        c.profile = c.otherProfile = pi;
                        c.segment = s;
                        c.otherSegment = t;
                        constraints.push_back(c);
                    }
                }
            }
        }

        // Concentric: arcs and circles whose centres are close, each to the first of its group.
        if (options.concentric)
        {
            struct Round { size_t profile, segment; Vec2 centre; };
            std::vector<Round> rounds;
            for (size_t pi = 0; pi < profiles.size(); ++pi)
                for (size_t s = 0; s < profiles[pi].segments.size(); ++s)
                    if (profiles[pi].segments[s].kind != ProfileSegment::Kind::Line)
                    {
                        const ProfileSegment& seg = profiles[pi].segments[s];
                        rounds.push_back({ pi, s, seg.fitRadius > 0.0 ? seg.fitCentre : seg.centre });
                    }
            std::vector<char> grouped(rounds.size(), 0);
            for (size_t a = 0; a < rounds.size(); ++a)
            {
                if (grouped[a])
                    continue;
                for (size_t b = a + 1; b < rounds.size(); ++b)
                {
                    if (grouped[b] || Length2(rounds[a].centre - rounds[b].centre) > options.concentricDistance)
                        continue;
                    grouped[b] = 1;
                    ProfileConstraint c;
                    c.kind = ProfileConstraint::Kind::Concentric;
                    c.profile = rounds[a].profile;
                    c.segment = rounds[a].segment;
                    c.otherProfile = rounds[b].profile;
                    c.otherSegment = rounds[b].segment;
                    constraints.push_back(c);
                }
            }
        }
        return constraints;
    }

    double RoundTo(double value, double step)
    {
        if (!(step > 0.0))
            return value;
        return std::round(value / step) * step;
    }

    std::vector<Vec2> SimplifyPolyline(const std::vector<Vec2>& points, bool closed, double tolerance)
    {
        if (points.size() < 3)
            return points;
        std::vector<Vec2> p = points;
        size_t split = 0;
        if (closed)
        {
            // Split the loop at the point furthest from the first.
            double far = -1.0;
            for (size_t k = 1; k < p.size(); ++k)
            {
                const double d = Length2(p[k] - p[0]);
                if (d > far)
                {
                    far = d;
                    split = k;
                }
            }
            p.push_back(p.front());
        }

        std::vector<char> keep(p.size(), 0);
        keep.front() = keep.back() = 1;
        if (closed)
            keep[split] = 1;
        std::vector<std::pair<size_t, size_t>> stack;
        if (closed)
        {
            stack.push_back({ 0, split });
            stack.push_back({ split, p.size() - 1 });
        }
        else
        {
            stack.push_back({ 0, p.size() - 1 });
        }
        while (!stack.empty())
        {
            const auto [a, b] = stack.back();
            stack.pop_back();
            double worst = -1.0;
            size_t at = a;
            for (size_t k = a + 1; k < b; ++k)
            {
                const double d = DistanceToSegment(p[k], p[a], p[b]);
                if (d > worst)
                {
                    worst = d;
                    at = k;
                }
            }
            if (worst > tolerance)
            {
                keep[at] = 1;
                stack.push_back({ a, at });
                stack.push_back({ at, b });
            }
        }
        std::vector<Vec2> out;
        for (size_t k = 0; k < p.size(); ++k)
            if (keep[k])
                out.push_back(p[k]);
        if (closed && out.size() > 1)
            out.pop_back();         // the repeated first point
        return out;
    }

    SectionResult SectionMesh(const Mesh& mesh, const SectionFrame& frame, double tolerance,
                              double referenceTolerance)
    {
        SectionResult result;
        const Point3 normal = frame.Normal();
        result.slices = SliceMesh(mesh, frame.origin, normal);
        for (const SlicePolyline& slice : result.slices)
        {
            std::vector<Vec2> flat;
            flat.reserve(slice.points.size());
            for (const Point3& q : slice.points)
                flat.push_back(frame.ToPlane(q));
            result.slicePoints += flat.size();
            result.profiles.push_back(FitProfile(flat, slice.closed, tolerance));
            result.largestDeviation = std::max(result.largestDeviation, result.profiles.back().largestDeviation);
            result.reference.push_back(SimplifyPolyline(flat, slice.closed, referenceTolerance));
        }
        for (const Point3& v : mesh.vertices)
        {
            const double h = Dot(v - frame.origin, normal);
            result.behind = std::min(result.behind, h);
            result.ahead = std::max(result.ahead, h);
        }
        return result;
    }

    SectionFrame FrameForPlane(const Point3& origin, const Point3& normalIn)
    {
        SectionFrame frame;
        frame.origin = origin;
        const Point3 normal = Normalised(normalIn);
        Point3 along{ 1, 0, 0 };
        if (std::fabs(Dot(along, normal)) > 0.9)
            along = { 0, 1, 0 };
        frame.xAxis = Normalised(along - normal * Dot(along, normal));
        frame.yAxis = Cross(normal, frame.xAxis);
        return frame;
    }
}
