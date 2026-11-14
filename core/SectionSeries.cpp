// SectionSeries.cpp : a series of sections through a scan, for lofting.

#include "SectionSeries.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace scancore
{
    namespace
    {
        const double kPi = 3.14159265358979323846;

        Vec2 Add(const Vec2& a, const Vec2& b) { return { a.x + b.x, a.y + b.y }; }
        Vec2 Sub(const Vec2& a, const Vec2& b) { return { a.x - b.x, a.y - b.y }; }
        Vec2 Scale(const Vec2& a, double s) { return { a.x * s, a.y * s }; }
        double Dot2(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
        double Cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
        double Len(const Vec2& a) { return std::hypot(a.x, a.y); }

        // The difference between two angles, 0 to pi.
        double AngleApart(double a, double b)
        {
            double d = std::fmod(std::fabs(a - b), 2.0 * kPi);
            return d > kPi ? 2.0 * kPi - d : d;
        }

        // A closed loop in its slice's frame, anticlockwise.
        struct Loop
        {
            size_t polyline = 0;
            std::vector<Vec2> points;
            std::vector<double> along;      // arc length at each point; along.back() + the closing edge = perimeter
            double perimeter = 0.0;
            double area = 0.0;
            Vec2 centroid;

            void Measure()
            {
                along.assign(points.size(), 0.0);
                for (size_t i = 1; i < points.size(); ++i)
                    along[i] = along[i - 1] + Len(Sub(points[i], points[i - 1]));
                perimeter = along.empty() ? 0.0 : along.back() + Len(Sub(points.front(), points.back()));
            }

            // The point at arc length t (taken round the loop).
            Vec2 At(double t) const
            {
                if (points.empty() || perimeter <= 0.0)
                    return {};
                t = std::fmod(t, perimeter);
                if (t < 0.0)
                    t += perimeter;
                const size_t i = static_cast<size_t>(std::upper_bound(along.begin(), along.end(), t) - along.begin()) - 1;
                const Vec2& a = points[i];
                const Vec2& b = points[(i + 1) % points.size()];
                const double length = ((i + 1 < points.size()) ? along[i + 1] : perimeter) - along[i];
                const double f = length > 1e-12 ? (t - along[i]) / length : 0.0;
                return Add(a, Scale(Sub(b, a), f));
            }

            double AngleOf(const Vec2& p) const { return std::atan2(p.y - centroid.y, p.x - centroid.x); }
        };

        // Signed area and centroid of a closed polygon.
        void AreaAndCentroid(const std::vector<Vec2>& p, double& area, Vec2& centroid)
        {
            double a = 0.0, cx = 0.0, cy = 0.0;
            for (size_t i = 0; i < p.size(); ++i)
            {
                const Vec2& u = p[i];
                const Vec2& v = p[(i + 1) % p.size()];
                const double c = Cross2(u, v);
                a += c;
                cx += (u.x + v.x) * c;
                cy += (u.y + v.y) * c;
            }
            area = 0.5 * a;
            if (std::fabs(a) > 1e-300)
                centroid = { cx / (3.0 * a), cy / (3.0 * a) };
            else if (!p.empty())
                centroid = p.front();
        }

        // A corner of a loop: where it is along the loop, how sharp, and its angle round the centroid.
        struct Corner
        {
            double t = 0.0;
            double turn = 0.0;              // radians, either way
            double angle = 0.0;
            Vec2 point;
        };

        // Corners: points where the loop turns more than the corner angle over
        // a short window either side, keeping the sharpest within two windows.
        std::vector<Corner> FindCorners(const Loop& loop, double tolerance, double cornerRadians)
        {
            std::vector<Corner> found;
            const size_t n = loop.points.size();
            if (n < 4 || loop.perimeter <= 0.0)
                return found;
            const double w = std::max(4.0 * tolerance, loop.perimeter / 100.0);
            std::vector<double> turn(n, 0.0);
            for (size_t i = 0; i < n; ++i)
            {
                const Vec2 p = loop.points[i];
                const Vec2 a = Sub(p, loop.At(loop.along[i] - w));
                const Vec2 b = Sub(loop.At(loop.along[i] + w), p);
                if (Len(a) < 1e-12 || Len(b) < 1e-12)
                    continue;
                turn[i] = std::atan2(Cross2(a, b), Dot2(a, b));
            }
            std::vector<size_t> order(n);
            std::iota(order.begin(), order.end(), size_t{ 0 });
            std::sort(order.begin(), order.end(),
                      [&](size_t a, size_t b) { return std::fabs(turn[a]) > std::fabs(turn[b]); });
            for (size_t i : order)
            {
                if (std::fabs(turn[i]) <= cornerRadians)
                    break;
                bool near = false;
                for (const Corner& c : found)
                {
                    double d = std::fabs(c.t - loop.along[i]);
                    d = std::min(d, loop.perimeter - d);
                    near = near || d < 2.0 * w;
                }
                if (near)
                    continue;
                Corner c;
                c.t = loop.along[i];
                c.turn = turn[i];
                c.point = loop.points[i];
                c.angle = loop.AngleOf(c.point);
                found.push_back(c);
            }
            std::sort(found.begin(), found.end(), [](const Corner& a, const Corner& b) { return a.t < b.t; });
            return found;
        }

        // The loop's point (on its outline) whose angle round the centroid is
        // nearest the given angle.
        double PointAtAngle(const Loop& loop, double angle)
        {
            double best = 1e300, t = 0.0;
            for (size_t i = 0; i < loop.points.size(); ++i)
            {
                const double d = AngleApart(loop.AngleOf(loop.points[i]), angle);
                if (d < best)
                {
                    best = d;
                    t = loop.along[i];
                }
            }
            return t;
        }

        // Nearest corner to an angle (then a point), within maxApart; -1 if none.
        int NearestCorner(const std::vector<Corner>& corners, double angle, const Vec2& point, double maxApart,
                          const std::vector<bool>* used = nullptr)
        {
            int best = -1;
            double bestApart = 1e300, bestDistance = 1e300;
            for (size_t k = 0; k < corners.size(); ++k)
            {
                if (used != nullptr && (*used)[k])
                    continue;
                const double apart = AngleApart(corners[k].angle, angle);
                if (apart > maxApart)
                    continue;
                const double distance = Len(Sub(corners[k].point, point));
                // Angles within a degree count as equal; then the nearer.
                if (apart < bestApart - 1.0 * kPi / 180.0 ||
                    (apart < bestApart + 1.0 * kPi / 180.0 && distance < bestDistance))
                {
                    best = static_cast<int>(k);
                    bestApart = std::min(apart, bestApart);
                    bestDistance = distance;
                }
            }
            return best;
        }

        // Follows each of the first slice's corners through the slices, by angle
        // round the centroid (within 25 degrees, each corner used once). Returns
        // per track the corner index in each slice, for tracks in every slice.
        std::vector<std::vector<int>> TrackCorners(const std::vector<std::vector<Corner>>& corners)
        {
            const double maxApart = 25.0 * kPi / 180.0;
            std::vector<std::vector<int>> tracks;
            if (corners.empty())
                return tracks;
            for (size_t k = 0; k < corners[0].size(); ++k)
                tracks.push_back({ static_cast<int>(k) });
            for (size_t s = 1; s < corners.size(); ++s)
            {
                std::vector<bool> used(corners[s].size(), false);
                std::vector<std::vector<int>> kept;
                for (std::vector<int>& track : tracks)
                {
                    const Corner& last = corners[s - 1][track.back()];
                    const int next = NearestCorner(corners[s], last.angle, last.point, maxApart, &used);
                    if (next < 0)
                        continue;
                    used[next] = true;
                    track.push_back(next);
                    kept.push_back(track);
                }
                tracks = std::move(kept);
            }
            return tracks;
        }

        // Solves a cyclic tridiagonal system a[i] x[i-1] + b[i] x[i] + c[i] x[i+1] = d[i]
        // (indices taken round), by Sherman-Morrison on the plain tridiagonal one.
        std::vector<double> SolveCyclic(std::vector<double> a, std::vector<double> b, std::vector<double> c,
                                        const std::vector<double>& d)
        {
            const size_t n = b.size();
            std::vector<double> x(n, 0.0);
            if (n == 1)
            {
                x[0] = d[0] / (a[0] + b[0] + c[0]);
                return x;
            }
            if (n == 2)
            {
                // Both neighbours are the other point.
                const double m00 = b[0], m01 = a[0] + c[0], m10 = a[1] + c[1], m11 = b[1];
                const double det = m00 * m11 - m01 * m10;
                x[0] = (d[0] * m11 - m01 * d[1]) / det;
                x[1] = (m00 * d[1] - m10 * d[0]) / det;
                return x;
            }
            const double alpha = c[n - 1];  // bottom left
            const double beta = a[0];       // top right
            const double gamma = -b[0];
            b[0] -= gamma;
            b[n - 1] -= alpha * beta / gamma;
            auto thomas = [&](const std::vector<double>& rhs) {
                std::vector<double> cp(n), dp(n), out(n);
                cp[0] = c[0] / b[0];
                dp[0] = rhs[0] / b[0];
                for (size_t i = 1; i < n; ++i)
                {
                    const double m = b[i] - a[i] * cp[i - 1];
                    cp[i] = (i + 1 < n) ? c[i] / m : 0.0;
                    dp[i] = (rhs[i] - a[i] * dp[i - 1]) / m;
                }
                out[n - 1] = dp[n - 1];
                for (size_t i = n - 1; i-- > 0;)
                    out[i] = dp[i] - cp[i] * out[i + 1];
                return out;
            };
            const std::vector<double> y = thomas(d);
            std::vector<double> u(n, 0.0);
            u[0] = gamma;
            u[n - 1] = alpha;
            const std::vector<double> z = thomas(u);
            const double vy = y[0] + beta / gamma * y[n - 1];
            const double vz = z[0] + beta / gamma * z[n - 1];
            const double f = vy / (1.0 + vz);
            for (size_t i = 0; i < n; ++i)
                x[i] = y[i] - f * z[i];
            return x;
        }

        // Distances to a closed polyline, through a grid of its segments.
        class PolylineDistance
        {
        public:
            explicit PolylineDistance(const std::vector<Vec2>& points) : m_points(points)
            {
                const size_t n = points.size();
                double total = 0.0;
                m_low = { 1e300, 1e300 };
                Vec2 high{ -1e300, -1e300 };
                for (size_t i = 0; i < n; ++i)
                {
                    total += Len(Sub(points[(i + 1) % n], points[i]));
                    m_low = { std::min(m_low.x, points[i].x), std::min(m_low.y, points[i].y) };
                    high = { std::max(high.x, points[i].x), std::max(high.y, points[i].y) };
                }
                m_cell = std::max(2.0 * total / std::max<size_t>(n, 1), 1e-6);
                m_nx = std::max(1, static_cast<int>((high.x - m_low.x) / m_cell) + 1);
                m_ny = std::max(1, static_cast<int>((high.y - m_low.y) / m_cell) + 1);
                // Coarsen if the grid would be huge.
                while (static_cast<double>(m_nx) * m_ny > 4e6)
                {
                    m_cell *= 2.0;
                    m_nx = std::max(1, static_cast<int>((high.x - m_low.x) / m_cell) + 1);
                    m_ny = std::max(1, static_cast<int>((high.y - m_low.y) / m_cell) + 1);
                }
                m_cells.assign(static_cast<size_t>(m_nx) * m_ny, {});
                for (size_t i = 0; i < n; ++i)
                {
                    const Vec2& a = points[i];
                    const Vec2& b = points[(i + 1) % n];
                    const int x0 = Cx(std::min(a.x, b.x)), x1 = Cx(std::max(a.x, b.x));
                    const int y0 = Cy(std::min(a.y, b.y)), y1 = Cy(std::max(a.y, b.y));
                    for (int y = y0; y <= y1; ++y)
                        for (int x = x0; x <= x1; ++x)
                            m_cells[static_cast<size_t>(y) * m_nx + x].push_back(static_cast<int>(i));
                }
            }

            double To(const Vec2& p) const
            {
                const int cx = Cx(p.x), cy = Cy(p.y);
                double best = 1e300;
                const int most = std::max(m_nx, m_ny);
                for (int ring = 0; ring <= most; ++ring)
                {
                    for (int y = cy - ring; y <= cy + ring; ++y)
                        for (int x = cx - ring; x <= cx + ring; ++x)
                        {
                            if (std::max(std::abs(x - cx), std::abs(y - cy)) != ring)
                                continue;
                            if (x < 0 || y < 0 || x >= m_nx || y >= m_ny)
                                continue;
                            for (int i : m_cells[static_cast<size_t>(y) * m_nx + x])
                                best = std::min(best, SegmentDistance(p, i));
                        }
                    // Every segment not yet seen is at least ring cells away.
                    if (best < 1e300 && best <= ring * m_cell)
                        break;
                }
                return best;
            }

        private:
            int Cx(double x) const { return std::clamp(static_cast<int>((x - m_low.x) / m_cell), 0, m_nx - 1); }
            int Cy(double y) const { return std::clamp(static_cast<int>((y - m_low.y) / m_cell), 0, m_ny - 1); }
            double SegmentDistance(const Vec2& p, int i) const
            {
                const Vec2& a = m_points[i];
                const Vec2& b = m_points[(i + 1) % m_points.size()];
                const Vec2 ab = Sub(b, a);
                const double l2 = Dot2(ab, ab);
                const double f = l2 > 0.0 ? std::clamp(Dot2(Sub(p, a), ab) / l2, 0.0, 1.0) : 0.0;
                return Len(Sub(p, Add(a, Scale(ab, f))));
            }

            const std::vector<Vec2>& m_points;
            Vec2 m_low;
            double m_cell = 1.0;
            int m_nx = 1, m_ny = 1;
            std::vector<std::vector<int>> m_cells;
        };

        // The 99th percentile distance of the loop's points from the spline through samples.
        double SplineDeviation(const Loop& loop, const std::vector<Vec2>& samples)
        {
            const std::vector<Vec2> curve = ClosedSplineCurve(samples, 8);
            const PolylineDistance distance(curve);
            std::vector<double> d;
            d.reserve(loop.points.size());
            for (const Vec2& p : loop.points)
                d.push_back(distance.To(p));
            if (d.empty())
                return 0.0;
            const size_t k = std::min(d.size() - 1, static_cast<size_t>(0.99 * static_cast<double>(d.size())));
            std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(k), d.end());
            return d[k];
        }

        // Positions along a loop from its start (0 to perimeter).
        double FromStart(double t, double start, double perimeter)
        {
            double u = std::fmod(t - start, perimeter);
            return u < 0.0 ? u + perimeter : u;
        }

        // Fit Spline for one chain.
        void FitChainSplines(const std::vector<Loop>& loops, const std::vector<SectionFrame>& frames,
                             const SeriesOptions& options, SeriesChain& chain)
        {
            const size_t S = loops.size();
            const double cornerRadians = options.cornerDegrees * kPi / 180.0;
            std::vector<std::vector<Corner>> corners(S);
            for (size_t s = 0; s < S; ++s)
                corners[s] = FindCorners(loops[s], options.tolerance, cornerRadians);

            // Starts: the sharpest corner of the first slice (or the point at
            // angle 0), then each slice's corner nearest the last start (or the
            // point at its angle, keeping the seam straight).
            std::vector<double> start(S, 0.0);
            std::vector<int> startCorner(S, -1);
            for (size_t s = 0; s < S; ++s)
            {
                const Loop& loop = loops[s];
                if (s == 0)
                {
                    int sharpest = -1;
                    for (size_t k = 0; k < corners[0].size(); ++k)
                        if (sharpest < 0 || std::fabs(corners[0][k].turn) > std::fabs(corners[0][sharpest].turn))
                            sharpest = static_cast<int>(k);
                    startCorner[0] = sharpest;
                    start[0] = sharpest >= 0 ? corners[0][sharpest].t : PointAtAngle(loop, 0.0);
                    continue;
                }
                const Vec2 last = loops[s - 1].At(start[s - 1]);
                const double angle = loops[s - 1].AngleOf(last);
                const int k = NearestCorner(corners[s], angle, last, 30.0 * kPi / 180.0);
                startCorner[s] = k;
                start[s] = k >= 0 ? corners[s][k].t : PointAtAngle(loop, angle);
            }

            // Corners found in every slice, by their positions from each start,
            // in the same order in every slice. The start's own corner is the start.
            std::vector<std::vector<int>> tracks = TrackCorners(corners);
            std::vector<std::vector<double>> pinned;    // per track, position from the start in each slice
            for (const std::vector<int>& track : tracks)
            {
                std::vector<double> u(S);
                bool atStart = false;
                for (size_t s = 0; s < S; ++s)
                {
                    u[s] = FromStart(corners[s][track[s]].t, start[s], loops[s].perimeter);
                    const double gap = std::min(u[s], loops[s].perimeter - u[s]);
                    atStart = atStart || gap < 1e-9 || track[s] == startCorner[s];
                }
                if (!atStart)
                    pinned.push_back(u);
            }
            std::sort(pinned.begin(), pinned.end(),
                      [](const std::vector<double>& a, const std::vector<double>& b) { return a[0] < b[0]; });
            std::vector<std::vector<double>> ordered;
            for (const std::vector<double>& u : pinned)
            {
                bool inOrder = true;
                if (!ordered.empty())
                    for (size_t s = 0; s < S; ++s)
                        inOrder = inOrder && u[s] > ordered.back()[s] + 1e-9;
                if (inOrder)
                    ordered.push_back(u);
            }
            chain.corners = ordered.size() + (startCorner[0] >= 0 ? 1 : 0);

            // Stretches between the pins, and each one's share of the loop on average.
            const size_t J = ordered.size() + 1;
            std::vector<double> share(J, 0.0);
            for (size_t s = 0; s < S; ++s)
            {
                const double P = loops[s].perimeter;
                double from = 0.0;
                for (size_t j = 0; j < J; ++j)
                {
                    const double to = (j + 1 < J) ? ordered[j][s] : P;
                    share[j] += (to - from) / P / static_cast<double>(S);
                    from = to;
                }
            }

            auto counts = [&](size_t N) {
                std::vector<size_t> n(J, 1);
                size_t used = J;
                // Largest remainder, after one each.
                std::vector<double> want(J);
                for (size_t j = 0; j < J; ++j)
                    want[j] = std::max(0.0, share[j] * static_cast<double>(N) - 1.0);
                const size_t rest = N > J ? N - J : 0;
                size_t given = 0;
                for (size_t j = 0; j < J; ++j)
                {
                    const size_t whole = static_cast<size_t>(std::floor(want[j]));
                    n[j] += whole;
                    given += whole;
                }
                std::vector<size_t> order(J);
                std::iota(order.begin(), order.end(), size_t{ 0 });
                std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                    return want[a] - std::floor(want[a]) > want[b] - std::floor(want[b]);
                });
                for (size_t k = 0; given < rest && k < J; ++k, ++given)
                    n[order[k]]++;
                (void)used;
                return n;
            };
            auto samplesOf = [&](size_t s, const std::vector<size_t>& n) {
                std::vector<Vec2> out;
                const double P = loops[s].perimeter;
                double from = 0.0;
                for (size_t j = 0; j < J; ++j)
                {
                    const double to = (j + 1 < J) ? ordered[j][s] : P;
                    for (size_t i = 0; i < n[j]; ++i)
                        out.push_back(loops[s].At(start[s] + from + (to - from) * static_cast<double>(i) / n[j]));
                    from = to;
                }
                return out;
            };

            // N from 16 (or two a stretch), doubling until every slice is within tolerance.
            size_t N = 16;
            while (N < 2 * J)
                N *= 2;
            std::vector<size_t> n;
            std::vector<std::vector<Vec2>> samples(S);
            double worst = 0.0;
            for (;;)
            {
                n = counts(N);
                worst = 0.0;
                for (size_t s = 0; s < S; ++s)
                {
                    samples[s] = samplesOf(s, n);
                    worst = std::max(worst, SplineDeviation(loops[s], samples[s]));
                }
                if (worst <= options.tolerance || N * 2 > options.maxSplinePoints)
                    break;
                N *= 2;
            }
            chain.withinTolerance = worst <= options.tolerance;
            chain.largestDeviation = worst;
            chain.spline = samples;

            // Guides: the start if it's a corner, and each pinned corner; with
            // none, four points evenly spaced by arc length.
            std::vector<size_t> guideIndex;
            if (chain.corners > 0)
            {
                if (startCorner[0] >= 0)
                    guideIndex.push_back(0);
                size_t index = 0;
                for (size_t j = 0; j + 1 < J; ++j)
                {
                    index += n[j];
                    guideIndex.push_back(index);
                }
            }
            else
            {
                const size_t total = samples[0].size();
                for (size_t k = 0; k < 4; ++k)
                    guideIndex.push_back(k * total / 4);
            }
            if (options.guides && S >= 2)
                for (size_t g : guideIndex)
                {
                    std::vector<Point3> guide;
                    for (size_t s = 0; s < S; ++s)
                        guide.push_back(frames[s].ToWorld(samples[s][g]));
                    chain.guides.push_back(guide);
                }
        }

        double SegmentLength(const ProfileSegment& seg)
        {
            if (seg.kind == ProfileSegment::Kind::Line)
                return Len(Sub(seg.end, seg.start));
            if (seg.kind == ProfileSegment::Kind::Circle)
                return 2.0 * kPi * seg.radius;
            const double a = std::atan2(seg.start.y - seg.centre.y, seg.start.x - seg.centre.x);
            const double b = std::atan2(seg.end.y - seg.centre.y, seg.end.x - seg.centre.x);
            double sweep = b - a;
            if (seg.anticlockwise && sweep <= 0.0)
                sweep += 2.0 * kPi;
            else if (!seg.anticlockwise && sweep >= 0.0)
                sweep -= 2.0 * kPi;
            return std::fabs(sweep) * seg.radius;
        }

        // A segment's direction at its start or end.
        Vec2 Tangent(const ProfileSegment& seg, bool atEnd)
        {
            if (seg.kind == ProfileSegment::Kind::Line)
            {
                const Vec2 d = Sub(seg.end, seg.start);
                const double l = Len(d);
                return l > 0.0 ? Scale(d, 1.0 / l) : Vec2{ 1, 0 };
            }
            const Vec2 r = Sub(atEnd ? seg.end : seg.start, seg.centre);
            const double l = Len(r);
            if (l <= 0.0)
                return { 1, 0 };
            const Vec2 t{ -r.y / l, r.x / l };
            return seg.anticlockwise ? t : Scale(t, -1.0);
        }

        // The point a fraction f of the way along a line or arc.
        Vec2 PointOnSegment(const ProfileSegment& seg, double f)
        {
            if (seg.kind == ProfileSegment::Kind::Line)
                return Add(seg.start, Scale(Sub(seg.end, seg.start), f));
            const double from = std::atan2(seg.start.y - seg.centre.y, seg.start.x - seg.centre.x);
            const double sweep = (seg.anticlockwise ? 1.0 : -1.0) * SegmentLength(seg) / seg.radius;
            const double m = from + f * sweep;
            return { seg.centre.x + seg.radius * std::cos(m), seg.centre.y + seg.radius * std::sin(m) };
        }

        // Splits a line or arc in two, f of the way along; the curve doesn't move.
        std::pair<ProfileSegment, ProfileSegment> SplitAt(const ProfileSegment& seg, double f)
        {
            ProfileSegment a = seg, b = seg;
            const Vec2 middle = PointOnSegment(seg, f);
            a.end = middle;
            b.start = middle;
            return { a, b };
        }

        // Splits a line or arc in two at its middle.
        std::pair<ProfileSegment, ProfileSegment> Split(const ProfileSegment& seg)
        {
            return SplitAt(seg, 0.5);
        }

        // The point at a fraction of a profile's length.
        Vec2 ProfilePointAt(const Profile& profile, double fraction)
        {
            double total = 0.0;
            for (const ProfileSegment& seg : profile.segments)
                total += SegmentLength(seg);
            double t = fraction * total;
            for (const ProfileSegment& seg : profile.segments)
            {
                const double l = SegmentLength(seg);
                if (t <= l || &seg == &profile.segments.back())
                {
                    const double f = l > 0.0 ? std::clamp(t / l, 0.0, 1.0) : 0.0;
                    if (seg.kind == ProfileSegment::Kind::Line)
                        return Add(seg.start, Scale(Sub(seg.end, seg.start), f));
                    const double from = std::atan2(seg.start.y - seg.centre.y, seg.start.x - seg.centre.x);
                    const double a = from + (seg.anticlockwise ? 1.0 : -1.0) * f * l / seg.radius;
                    return { seg.centre.x + seg.radius * std::cos(a), seg.centre.y + seg.radius * std::sin(a) };
                }
                t -= l;
            }
            return {};
        }

        // Lines and arcs for one chain; false if the slices can't be matched.
        bool FitChainLinesArcs(const std::vector<Loop>& loops, const std::vector<SectionFrame>& frames,
                               const SeriesOptions& options, SeriesChain& chain, std::string& why)
        {
            const size_t S = loops.size();
            std::vector<Profile> profiles(S);
            for (size_t s = 0; s < S; ++s)
            {
                profiles[s] = FitProfile(loops[s].points, true, options.tolerance, options.fitArcs);
                if (profiles[s].segments.empty())
                {
                    why = "a slice has no fit";
                    return false;
                }
            }
            auto isCircle = [](const Profile& p) {
                return p.segments.size() == 1 && p.segments[0].kind == ProfileSegment::Kind::Circle;
            };
            size_t circles = 0;
            for (const Profile& p : profiles)
                circles += isCircle(p) ? 1 : 0;
            if (circles == S)
            {
                chain.profiles = profiles;
                for (const Profile& p : profiles)
                    chain.largestDeviation = std::max(chain.largestDeviation, p.largestDeviation);
                if (options.guides && S >= 2)
                    for (int k = 0; k < 4; ++k)
                    {
                        std::vector<Point3> guide;
                        for (size_t s = 0; s < S; ++s)
                        {
                            const ProfileSegment& c = profiles[s].segments[0];
                            const double a = k * 0.5 * kPi;
                            guide.push_back(frames[s].ToWorld(
                                { c.centre.x + c.radius * std::cos(a), c.centre.y + c.radius * std::sin(a) }));
                        }
                        chain.guides.push_back(guide);
                    }
                return true;
            }
            // Some slices are whole circles and others aren't: a circle becomes two
            // half arcs, from its point at angle 0 round its centre.
            for (Profile& p : profiles)
                if (isCircle(p))
                {
                    const ProfileSegment c = p.segments[0];
                    ProfileSegment a = c;
                    a.kind = ProfileSegment::Kind::Arc;
                    a.anticlockwise = true;
                    a.start = { c.centre.x + c.radius, c.centre.y };
                    a.end = { c.centre.x - c.radius, c.centre.y };
                    ProfileSegment b = a;
                    b.start = a.end;
                    b.end = a.start;
                    p.segments = { a, b };
                }

            // Corners at joins.
            const double cornerRadians = options.cornerDegrees * kPi / 180.0;
            std::vector<std::vector<Corner>> corners(S);
            std::vector<std::vector<size_t>> cornerSegment(S);   // the segment starting at each corner
            for (size_t s = 0; s < S; ++s)
            {
                const std::vector<ProfileSegment>& segs = profiles[s].segments;
                double t = 0.0;
                for (size_t k = 0; k < segs.size(); ++k)
                {
                    const ProfileSegment& before = segs[(k + segs.size() - 1) % segs.size()];
                    const Vec2 a = Tangent(before, true), b = Tangent(segs[k], false);
                    const double turn = std::atan2(Cross2(a, b), Dot2(a, b));
                    if (std::fabs(turn) > cornerRadians)
                    {
                        Corner c;
                        c.t = t;
                        c.turn = turn;
                        c.point = segs[k].start;
                        c.angle = loops[s].AngleOf(c.point);
                        corners[s].push_back(c);
                        cornerSegment[s].push_back(k);
                    }
                    t += SegmentLength(segs[k]);
                }
            }
            std::vector<std::vector<int>> tracks = TrackCorners(corners);
            std::vector<size_t> stretchStarts;      // segment index where each stretch starts, after splitting

            // Rotate each slice's profile to start at segment first[s].
            auto rotate = [&](size_t s, size_t first) {
                std::vector<ProfileSegment>& segs = profiles[s].segments;
                std::rotate(segs.begin(), segs.begin() + static_cast<std::ptrdiff_t>(first), segs.end());
            };

            if (tracks.empty())
            {
                // No corner common to every slice (Frank: lines and arcs anyway,
                // not splines). Each slice starts where the last slice's start is
                // by its angle round the centroid, split there if need be, as the
                // splines keep their seam straight; then the joins are gathered by
                // how far round they are, and each slice is split (never moved)
                // where it has none, so every slice has the same segments in the
                // same places.
                for (size_t s = 1; s < S; ++s)
                {
                    const Vec2 last = profiles[s - 1].segments[0].start;
                    const double angle = loops[s - 1].AngleOf(last);
                    std::vector<ProfileSegment>& segs = profiles[s].segments;
                    size_t bestSegment = 0;
                    double bestF = 0.0, bestApart = 1e300, bestDistance = 1e300;
                    for (size_t k = 0; k < segs.size(); ++k)
                        for (int i = 0; i < 64; ++i)
                        {
                            const double f = i / 64.0;
                            const Vec2 p = PointOnSegment(segs[k], f);
                            const double apart = AngleApart(loops[s].AngleOf(p), angle);
                            const double distance = Len(Sub(p, last));
                            if (apart < bestApart - 1e-3 || (apart < bestApart + 1e-3 && distance < bestDistance))
                            {
                                bestApart = std::min(apart, bestApart);
                                bestDistance = distance;
                                bestSegment = k;
                                bestF = f;
                            }
                        }
                    // A start within a tolerance of a join stays on the join.
                    const double length = SegmentLength(segs[bestSegment]);
                    if (bestF * length > options.tolerance && (1.0 - bestF) * length > options.tolerance)
                    {
                        const auto halves = SplitAt(segs[bestSegment], bestF);
                        segs[bestSegment] = halves.first;
                        segs.insert(segs.begin() + static_cast<std::ptrdiff_t>(bestSegment) + 1, halves.second);
                        ++bestSegment;
                    }
                    else if ((1.0 - bestF) * length <= options.tolerance && bestF > 0.5)
                    {
                        bestSegment = (bestSegment + 1) % segs.size();
                    }
                    rotate(s, bestSegment);
                }

                // Joins by how far round they are (0 to 1): gathered into places
                // within 2% of the way round, at most one join of a slice each.
                struct Join
                {
                    double u;
                    size_t slice;
                };
                std::vector<Join> joins;
                std::vector<double> totals(S, 0.0);
                for (size_t s = 0; s < S; ++s)
                {
                    for (const ProfileSegment& seg : profiles[s].segments)
                        totals[s] += SegmentLength(seg);
                    double t = 0.0;
                    for (size_t k = 0; k + 1 < profiles[s].segments.size(); ++k)
                    {
                        t += SegmentLength(profiles[s].segments[k]);
                        joins.push_back({ t / totals[s], s });
                    }
                }
                std::sort(joins.begin(), joins.end(), [](const Join& a, const Join& b) { return a.u < b.u; });
                struct Place
                {
                    double first = 0.0, sum = 0.0;
                    std::vector<bool> has;
                    size_t count = 0;
                };
                std::vector<Place> places;
                for (const Join& j : joins)
                {
                    if (places.empty() || j.u - places.back().first > 0.02 || places.back().has[j.slice])
                    {
                        Place p;
                        p.first = j.u;
                        p.has.assign(S, false);
                        places.push_back(p);
                    }
                    places.back().has[j.slice] = true;
                    places.back().sum += j.u;
                    places.back().count++;
                }
                // Split each slice where it has no join, from the end back so the
                // earlier segments keep their places.
                for (size_t s = 0; s < S; ++s)
                {
                    std::vector<ProfileSegment>& segs = profiles[s].segments;
                    for (size_t pi = places.size(); pi-- > 0;)
                    {
                        if (places[pi].has[s])
                            continue;
                        const double target = places[pi].sum / places[pi].count * totals[s];
                        double t = 0.0;
                        for (size_t k = 0; k < segs.size(); ++k)
                        {
                            const double length = SegmentLength(segs[k]);
                            if (target <= t + length || k + 1 == segs.size())
                            {
                                const double f = length > 0.0 ? std::clamp((target - t) / length, 0.01, 0.99) : 0.5;
                                const auto halves = SplitAt(segs[k], f);
                                segs[k] = halves.first;
                                segs.insert(segs.begin() + static_cast<std::ptrdiff_t>(k) + 1, halves.second);
                                break;
                            }
                            t += length;
                        }
                    }
                }
                const size_t count = profiles[0].segments.size();
                for (const Profile& p : profiles)
                    if (p.segments.size() != count)
                    {
                        why = "the slices' segments couldn't be matched";
                        return false;
                    }
                chain.corners = 0;
            }
            else
            {
                // Common corners in the same order from the first one in every slice.
                std::vector<std::vector<size_t>> segAt;     // per kept track, the segment in each slice
                for (const std::vector<int>& track : tracks)
                {
                    std::vector<size_t> k(S);
                    for (size_t s = 0; s < S; ++s)
                        k[s] = cornerSegment[s][track[s]];
                    segAt.push_back(k);
                }
                // Sorted by their segment in the first slice, then kept only while
                // they come in the same order (counted from the first) in every slice.
                std::sort(segAt.begin(), segAt.end(),
                          [](const std::vector<size_t>& a, const std::vector<size_t>& b) { return a[0] < b[0]; });
                std::vector<std::vector<size_t>> kept;
                for (const std::vector<size_t>& k : segAt)
                {
                    bool inOrder = true;
                    if (!kept.empty())
                        for (size_t s = 0; s < S; ++s)
                        {
                            const size_t n = profiles[s].segments.size();
                            const size_t prev = (kept.back()[s] + n - kept[0][s]) % n;
                            const size_t here = (k[s] + n - kept[0][s]) % n;
                            inOrder = inOrder && here > prev;
                        }
                    if (inOrder)
                        kept.push_back(k);
                }
                chain.corners = kept.size();
                // Stretches between common corners, per slice: segment counts.
                const size_t J = kept.size();
                std::vector<std::vector<std::vector<ProfileSegment>>> stretches(S, std::vector<std::vector<ProfileSegment>>(J));
                std::vector<size_t> target(J, 0);
                for (size_t s = 0; s < S; ++s)
                {
                    const std::vector<ProfileSegment>& segs = profiles[s].segments;
                    const size_t n = segs.size();
                    for (size_t j = 0; j < J; ++j)
                    {
                        const size_t from = kept[j][s];
                        const size_t next = kept[(j + 1) % J][s];
                        size_t count = (next + n - from) % n;
                        if (count == 0)
                            count = n;      // one common corner: the whole loop
                        for (size_t i = 0; i < count; ++i)
                            stretches[s][j].push_back(segs[(from + i) % n]);
                        target[j] = std::max(target[j], stretches[s][j].size());
                    }
                }
                size_t index = 0;
                for (size_t j = 0; j < J; ++j)
                {
                    stretchStarts.push_back(index);
                    index += target[j];
                }
                // Split the longest segment until each stretch has the target count.
                for (size_t s = 0; s < S; ++s)
                {
                    std::vector<ProfileSegment> joined;
                    for (size_t j = 0; j < J; ++j)
                    {
                        std::vector<ProfileSegment>& st = stretches[s][j];
                        while (st.size() < target[j])
                        {
                            size_t longest = 0;
                            for (size_t k = 1; k < st.size(); ++k)
                                if (SegmentLength(st[k]) > SegmentLength(st[longest]))
                                    longest = k;
                            const auto halves = Split(st[longest]);
                            st[longest] = halves.first;
                            st.insert(st.begin() + static_cast<std::ptrdiff_t>(longest) + 1, halves.second);
                        }
                        joined.insert(joined.end(), st.begin(), st.end());
                    }
                    profiles[s].segments = joined;
                }
            }

            chain.profiles = profiles;
            for (const Profile& p : profiles)
                chain.largestDeviation = std::max(chain.largestDeviation, p.largestDeviation);

            // Guides: the common corners (each stretch's first point), or four
            // points evenly spaced along.
            if (options.guides && S >= 2)
            {
                if (chain.corners > 0)
                {
                    // Each stretch starts at a common corner, at the same segment in every slice.
                    for (size_t k : stretchStarts)
                    {
                        std::vector<Point3> guide;
                        for (size_t s = 0; s < S; ++s)
                            guide.push_back(frames[s].ToWorld(profiles[s].segments[k].start));
                        chain.guides.push_back(guide);
                    }
                }
                else
                {
                    for (int k = 0; k < 4; ++k)
                    {
                        std::vector<Point3> guide;
                        for (size_t s = 0; s < S; ++s)
                            guide.push_back(frames[s].ToWorld(ProfilePointAt(profiles[s], k / 4.0)));
                        chain.guides.push_back(guide);
                    }
                }
            }
            return true;
        }

        // Pairs two slices' loops by centroid and area: match[i] is b's loop for
        // a's loop i. False if any loop has no partner.
        bool InsidePolygon(const Vec2& p, const std::vector<Vec2>& polygon)
        {
            bool inside = false;
            for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
            {
                const Vec2& a = polygon[i];
                const Vec2& b = polygon[j];
                if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
                    inside = !inside;
            }
            return inside;
        }

        bool SegmentsCross(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d)
        {
            const double d1 = Cross2(Sub(b, a), Sub(c, a)), d2 = Cross2(Sub(b, a), Sub(d, a));
            const double d3 = Cross2(Sub(d, c), Sub(a, c)), d4 = Cross2(Sub(d, c), Sub(b, c));
            return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
        }

        bool MatchLoops(const std::vector<Loop>& a, const std::vector<Loop>& b, std::vector<size_t>& match)
        {
            if (a.size() != b.size())
                return false;
            struct Pair
            {
                double cost;
                size_t i, j;
            };
            std::vector<Pair> pairs;
            for (size_t i = 0; i < a.size(); ++i)
                for (size_t j = 0; j < b.size(); ++j)
                {
                    const double sizeA = std::sqrt(a[i].area), sizeB = std::sqrt(b[j].area);
                    const double apart = Len(Sub(a[i].centroid, b[j].centroid));
                    const double ratio = std::max(a[i].area, b[j].area) / std::max(1e-12, std::min(a[i].area, b[j].area));
                    if (apart > sizeA + sizeB || ratio > 25.0)
                        continue;
                    pairs.push_back({ apart / (0.5 * (sizeA + sizeB)) + std::log(ratio), i, j });
                }
            std::sort(pairs.begin(), pairs.end(), [](const Pair& x, const Pair& y) { return x.cost < y.cost; });
            match.assign(a.size(), static_cast<size_t>(-1));
            std::vector<bool> taken(b.size(), false);
            size_t made = 0;
            for (const Pair& p : pairs)
            {
                if (match[p.i] != static_cast<size_t>(-1) || taken[p.j])
                    continue;
                match[p.i] = p.j;
                taken[p.j] = true;
                ++made;
            }
            return made == a.size();
        }

        std::string Slices(size_t first, size_t last)
        {
            char text[64];
            if (first == last)
                std::snprintf(text, sizeof text, "slice %zu", first + 1);
            else
                std::snprintf(text, sizeof text, "slices %zu to %zu", first + 1, last + 1);
            return text;
        }
    }

    bool ValidateOffsets(const std::vector<double>& offsets, std::string& message)
    {
        if (offsets.empty())
        {
            message = "There are no planes. Press Make, or Add one.";
            return false;
        }
        for (size_t k = 0; k < offsets.size(); ++k)
        {
            if (!std::isfinite(offsets[k]))
            {
                message = "Plane " + std::to_string(k + 1) + "'s offset isn't a number.";
                return false;
            }
            if (k > 0 && !(offsets[k] > offsets[k - 1]))
            {
                message = "Offsets must go up from each plane to the next (plane " + std::to_string(k + 1) +
                          " isn't past plane " + std::to_string(k) + ").";
                return false;
            }
        }
        message.clear();
        return true;
    }

    std::vector<double> EvenOffsets(double first, double last, int count)
    {
        std::vector<double> out;
        if (count <= 0)
            return out;
        if (count == 1)
            return { first };
        for (int k = 0; k < count; ++k)
            out.push_back(first + (last - first) * k / (count - 1));
        return out;
    }

    void DefaultSeriesRange(const Mesh& mesh, const SectionFrame& base, double& first, double& last)
    {
        const Point3 normal = base.Normal();
        double low = 1e300, high = -1e300;
        for (const Point3& v : mesh.vertices)
        {
            const double h = Dot(v - base.origin, normal);
            low = std::min(low, h);
            high = std::max(high, h);
        }
        if (low > high)
        {
            first = last = 0.0;
            return;
        }
        const double margin = 0.02 * (high - low);
        first = low + margin;
        last = high - margin;
    }

    std::vector<Vec2> ClosedSplineCurve(const std::vector<Vec2>& points, int steps)
    {
        const size_t n = points.size();
        std::vector<Vec2> out;
        if (n < 3)
            return points;
        std::vector<double> h(n);
        for (size_t i = 0; i < n; ++i)
            h[i] = std::max(1e-9, Len(Sub(points[(i + 1) % n], points[i])));
        std::vector<double> a(n), b(n), c(n), dx(n), dy(n);
        for (size_t i = 0; i < n; ++i)
        {
            const size_t p = (i + n - 1) % n, q = (i + 1) % n;
            a[i] = h[p];
            b[i] = 2.0 * (h[p] + h[i]);
            c[i] = h[i];
            dx[i] = 6.0 * ((points[q].x - points[i].x) / h[i] - (points[i].x - points[p].x) / h[p]);
            dy[i] = 6.0 * ((points[q].y - points[i].y) / h[i] - (points[i].y - points[p].y) / h[p]);
        }
        const std::vector<double> mx = SolveCyclic(a, b, c, dx);
        const std::vector<double> my = SolveCyclic(a, b, c, dy);
        out.reserve(n * static_cast<size_t>(steps));
        for (size_t i = 0; i < n; ++i)
        {
            const size_t q = (i + 1) % n;
            for (int k = 0; k < steps; ++k)
            {
                const double t = h[i] * k / steps;
                const double A = (h[i] - t) / h[i], B = t / h[i];
                const double e = (A * A * A - A) * h[i] * h[i] / 6.0, f = (B * B * B - B) * h[i] * h[i] / 6.0;
                out.push_back({ A * points[i].x + B * points[q].x + e * mx[i] + f * mx[q],
                                A * points[i].y + B * points[q].y + e * my[i] + f * my[q] });
            }
        }
        return out;
    }

    std::vector<Vec2> MapPoints(const std::vector<Vec2>& points, const SectionFrame& from, const SectionFrame& to)
    {
        std::vector<Vec2> out;
        out.reserve(points.size());
        for (const Vec2& p : points)
            out.push_back(to.ToPlane(from.ToWorld(p)));
        return out;
    }

    Profile MapProfile(const Profile& profile, const SectionFrame& from, const SectionFrame& to)
    {
        Profile out = profile;
        const bool mirrored = Dot(from.Normal(), to.Normal()) < 0.0;
        auto point = [&](const Vec2& p) { return to.ToPlane(from.ToWorld(p)); };
        auto direction = [&](const Vec2& d) {
            const Point3 w = from.xAxis * d.x + from.yAxis * d.y;
            return Vec2{ Dot(w, to.xAxis), Dot(w, to.yAxis) };
        };
        for (ProfileSegment& seg : out.segments)
        {
            seg.start = point(seg.start);
            seg.end = point(seg.end);
            seg.centre = point(seg.centre);
            seg.fitPoint = point(seg.fitPoint);
            seg.fitCentre = point(seg.fitCentre);
            seg.fitDirection = direction(seg.fitDirection);
            if (mirrored)
                seg.anticlockwise = !seg.anticlockwise;
        }
        return out;
    }

    SeriesResult SliceSeries(const Mesh& mesh, const SectionFrame& base, const std::vector<double>& offsets,
                             const SeriesOptions& options)
    {
        const auto started = std::chrono::steady_clock::now();
        SeriesResult result;
        const Point3 normal = base.Normal();
        std::vector<std::vector<Loop>> loops(offsets.size());
        for (size_t s = 0; s < offsets.size(); ++s)
        {
            SeriesSlice slice;
            slice.offset = offsets[s];
            slice.frame = base;
            slice.frame.origin = base.origin + normal * offsets[s];
            slice.section = SectionMesh(mesh, slice.frame, options.tolerance, 0.01, options.fitArcs);
            // Closed loops, anticlockwise; tiny scraps left out of matching.
            double largest = 0.0;
            std::vector<Loop> found;
            for (size_t k = 0; k < slice.section.slices.size(); ++k)
            {
                const SlicePolyline& poly = slice.section.slices[k];
                if (!poly.closed)
                {
                    ++slice.open;
                    continue;
                }
                Loop loop;
                loop.polyline = k;
                for (const Point3& p : poly.points)
                    loop.points.push_back(slice.frame.ToPlane(p));
                AreaAndCentroid(loop.points, loop.area, loop.centroid);
                if (loop.area < 0.0)
                {
                    std::reverse(loop.points.begin(), loop.points.end());
                    loop.area = -loop.area;
                }
                loop.Measure();
                largest = std::max(largest, loop.area);
                found.push_back(std::move(loop));
            }
            const double scrap = std::max(std::pow(10.0 * options.tolerance, 2.0), 1e-4 * largest);
            for (Loop& loop : found)
            {
                if (loop.points.size() < 4 || loop.area < scrap)
                    ++slice.scraps;
                else
                    loops[s].push_back(std::move(loop));
            }
            slice.loops = loops[s].size();
            result.openPolylines += slice.open;
            result.slices.push_back(std::move(slice));
        }

        // Runs: slices with the same loops, each matched to the next.
        std::vector<std::vector<size_t>> toNext(offsets.size());     // per slice, its loops' partners in the next
        size_t first = 0;
        for (size_t s = 0; s < offsets.size(); ++s)
        {
            const bool last = s + 1 == offsets.size();
            const bool matched = !last && MatchLoops(loops[s], loops[s + 1], toNext[s]);
            if (!matched)
            {
                result.runs.push_back({ first, s, loops[s].size() });
                first = s + 1;
            }
        }

        if (!options.loft)
        {
            result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            return result;
        }

        for (size_t r = 0; r < result.runs.size(); ++r)
        {
            const SeriesRun& run = result.runs[r];
            for (size_t k = 0; k < run.loops; ++k)
            {
                SeriesChain chain;
                chain.run = r;
                chain.firstSlice = run.firstSlice;
                std::vector<Loop> chainLoops;
                std::vector<SectionFrame> frames;
                size_t index = k;
                for (size_t s = run.firstSlice; s <= run.lastSlice; ++s)
                {
                    chainLoops.push_back(loops[s][index]);
                    frames.push_back(result.slices[s].frame);
                    chain.polyline.push_back(loops[s][index].polyline);
                    if (s < run.lastSlice)
                        index = toNext[s][index];
                }
                std::string why;
                if (!options.splines)
                {
                    chain.splines = false;
                    if (!FitChainLinesArcs(chainLoops, frames, options, chain, why))
                    {
                        chain.splines = true;
                        chain.fellBack = true;
                        chain.profiles.clear();
                        chain.guides.clear();
                        chain.largestDeviation = 0.0;
                        result.notes.push_back(Slices(run.firstSlice, run.lastSlice) + ", loop " +
                                               std::to_string(k + 1) + ": " + why + ", so it's fitted as splines");
                    }
                }
                if (chain.splines)
                    FitChainSplines(chainLoops, frames, options, chain);
                if (chain.splines && !chain.withinTolerance)
                {
                    char text[200];
                    std::snprintf(text, sizeof text, ", loop %zu: %zu spline points leave the slice up to %.3f mm off",
                                  k + 1, chain.spline.empty() ? size_t{ 0 } : chain.spline[0].size(),
                                  chain.largestDeviation);
                    result.notes.push_back(Slices(run.firstSlice, run.lastSlice) + text);
                }
                // Neighbouring sections that don't overlap: warned about on Create Loft.
                for (size_t i = 0; i + 1 < chainLoops.size(); ++i)
                    if (!OutlinesOverlap(chainLoops[i].points, chainLoops[i + 1].points))
                    {
                        SeriesGap gap;
                        gap.chain = result.chains.size();
                        gap.first = run.firstSlice + i;
                        gap.second = gap.first + 1;
                        gap.firstArea = chainLoops[i].area;
                        gap.secondArea = chainLoops[i + 1].area;
                        result.gaps.push_back(gap);
                    }
                result.chains.push_back(std::move(chain));
            }
        }
        result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return result;
    }

    bool OutlinesOverlap(const std::vector<Vec2>& a, const std::vector<Vec2>& b)
    {
        if (a.size() < 3 || b.size() < 3)
            return false;
        Vec2 lowA{ 1e300, 1e300 }, highA{ -1e300, -1e300 }, lowB = lowA, highB = highA;
        for (const Vec2& p : a)
        {
            lowA = { std::min(lowA.x, p.x), std::min(lowA.y, p.y) };
            highA = { std::max(highA.x, p.x), std::max(highA.y, p.y) };
        }
        for (const Vec2& p : b)
        {
            lowB = { std::min(lowB.x, p.x), std::min(lowB.y, p.y) };
            highB = { std::max(highB.x, p.x), std::max(highB.y, p.y) };
        }
        if (lowA.x > highB.x || lowB.x > highA.x || lowA.y > highB.y || lowB.y > highA.y)
            return false;
        // A point of either inside the other (checking at most about 200 of each).
        const size_t stepA = std::max<size_t>(1, a.size() / 200), stepB = std::max<size_t>(1, b.size() / 200);
        for (size_t i = 0; i < a.size(); i += stepA)
            if (InsidePolygon(a[i], b))
                return true;
        for (size_t i = 0; i < b.size(); i += stepB)
            if (InsidePolygon(b[i], a))
                return true;
        // Edges crossing, with no checked point inside (thin crossing shapes).
        for (size_t i = 0; i < a.size(); i += stepA)
        {
            const Vec2& p = a[i];
            const Vec2& q = a[std::min(i + stepA, a.size()) % a.size()];
            for (size_t j = 0; j < b.size(); j += stepB)
                if (SegmentsCross(p, q, b[j], b[std::min(j + stepB, b.size()) % b.size()]))
                    return true;
        }
        return false;
    }

    std::string DescribeRuns(const SeriesResult& result)
    {
        std::string text;
        for (const SeriesRun& run : result.runs)
        {
            if (!text.empty())
                text += "; ";
            text += Slices(run.firstSlice, run.lastSlice) + ": " + std::to_string(run.loops) +
                    (run.loops == 1 ? " loop" : " loops");
        }
        return text;
    }
}
