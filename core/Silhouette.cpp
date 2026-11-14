// Silhouette.cpp : the scan's outline along a plane's normal.

#include "Silhouette.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include <deque>
#include <iterator>
#include <map>

namespace scancore
{
    namespace
    {
        const double kPi = 3.14159265358979323846;

        double SignedArea(const std::vector<Vec2>& loop)
        {
            double area = 0.0;
            for (size_t i = 0; i < loop.size(); ++i)
            {
                const Vec2& a = loop[i];
                const Vec2& b = loop[(i + 1) % loop.size()];
                area += a.x * b.y - b.x * a.y;
            }
            return 0.5 * area;
        }

        Vec2 ClosestOnSegment(const Vec2& p, const Vec2& a, const Vec2& b)
        {
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double length = dx * dx + dy * dy;
            double t = (length > 0.0) ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / length : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            return { a.x + dx * t, a.y + dy * t };
        }

        // The mesh's outline edges seen along the normal: open edges, and edges
        // whose two triangles face differently along it (towards, edge on, away).
        std::vector<std::array<int, 2>> OutlineEdges(const Mesh& mesh, const Point3& normal)
        {
            const double edgeOn = 0.02;
            std::vector<signed char> facing(mesh.triangles.size());
            for (size_t t = 0; t < mesh.triangles.size(); ++t)
            {
                const auto& tri = mesh.triangles[t];
                const Point3& a = mesh.vertices[static_cast<size_t>(tri[0])];
                const Point3& b = mesh.vertices[static_cast<size_t>(tri[1])];
                const Point3& c = mesh.vertices[static_cast<size_t>(tri[2])];
                const double d = Dot(Normalised(Cross(b - a, c - a)), normal);
                facing[t] = static_cast<signed char>(d > edgeOn ? 1 : (d < -edgeOn ? -1 : 0));
            }
            struct HalfEdge
            {
                uint64_t key;
                int triangle;
            };
            std::vector<HalfEdge> halves;
            halves.reserve(mesh.triangles.size() * 3);
            for (size_t t = 0; t < mesh.triangles.size(); ++t)
                for (int k = 0; k < 3; ++k)
                {
                    const uint32_t a = static_cast<uint32_t>(mesh.triangles[t][k]);
                    const uint32_t b = static_cast<uint32_t>(mesh.triangles[t][(k + 1) % 3]);
                    if (a == b)
                        continue;
                    const uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
                    halves.push_back({ key, static_cast<int>(t) });
                }
            std::sort(halves.begin(), halves.end(),
                      [](const HalfEdge& x, const HalfEdge& y) { return x.key < y.key; });
            std::vector<std::array<int, 2>> edges;
            for (size_t i = 0; i < halves.size();)
            {
                size_t j = i + 1;
                bool differ = false;
                while (j < halves.size() && halves[j].key == halves[i].key)
                {
                    differ = differ || facing[static_cast<size_t>(halves[j].triangle)] !=
                                           facing[static_cast<size_t>(halves[i].triangle)];
                    ++j;
                }
                if (j - i == 1 || differ)
                    edges.push_back({ static_cast<int>(halves[i].key >> 32),
                                      static_cast<int>(halves[i].key & 0xffffffffu) });
                i = j;
            }
            return edges;
        }
    }

    SectionResult SilhouetteMesh(const Mesh& mesh, const SectionFrame& frame, double tolerance,
                                 double referenceTolerance, double smallestHole, SilhouetteInfo* info, bool arcs)
    {
        SectionResult result;
        SilhouetteInfo local;
        SilhouetteInfo& stats = (info != nullptr) ? *info : local;
        stats = SilhouetteInfo();
        if (mesh.triangles.empty())
            return result;

        const Point3 normal = frame.Normal();
        std::vector<Vec2> flat(mesh.vertices.size());
        double lowX = 1e300, lowY = 1e300, highX = -1e300, highY = -1e300;
        result.behind = 0.0;
        result.ahead = 0.0;
        bool first = true;
        for (size_t i = 0; i < mesh.vertices.size(); ++i)
        {
            flat[i] = frame.ToPlane(mesh.vertices[i]);
            lowX = std::min(lowX, flat[i].x);
            highX = std::max(highX, flat[i].x);
            lowY = std::min(lowY, flat[i].y);
            highY = std::max(highY, flat[i].y);
            const double along = Dot(mesh.vertices[i] - frame.origin, normal);
            result.behind = first ? along : std::min(result.behind, along);
            result.ahead = first ? along : std::max(result.ahead, along);
            first = false;
        }
        result.behind = std::min(result.behind, 0.0);
        result.ahead = std::max(result.ahead, 0.0);

        // The grid, two empty cells round it, at most about 16 million cells.
        const double extent = std::max(highX - lowX, highY - lowY);
        double cell = std::max({ tolerance, extent / 4000.0, 1e-6 });
        if ((highX - lowX) * (highY - lowY) / (cell * cell) > 16.0e6)
            cell = std::sqrt((highX - lowX) * (highY - lowY) / 16.0e6);
        stats.cell = cell;
        const int pad = 2;
        const int nx = static_cast<int>(std::ceil((highX - lowX) / cell)) + 2 * pad + 1;
        const int ny = static_cast<int>(std::ceil((highY - lowY) / cell)) + 2 * pad + 1;
        const double x0 = lowX - pad * cell, y0 = lowY - pad * cell;
        std::vector<char> filled(static_cast<size_t>(nx) * ny, 0);
        auto index = [&](int i, int j) { return static_cast<size_t>(i) + static_cast<size_t>(j) * nx; };
        auto cellOf = [&](double x, double y, int& i, int& j) {
            i = std::clamp(static_cast<int>(std::floor((x - x0) / cell)), 0, nx - 1);
            j = std::clamp(static_cast<int>(std::floor((y - y0) / cell)), 0, ny - 1);
        };

        // 1. Draw every triangle: its corners' cells and the cells whose middles it covers.
        for (const auto& tri : mesh.triangles)
        {
            const Vec2 p[3] = { flat[static_cast<size_t>(tri[0])], flat[static_cast<size_t>(tri[1])],
                                flat[static_cast<size_t>(tri[2])] };
            int i0 = nx, j0 = ny, i1 = -1, j1 = -1;
            for (const Vec2& q : p)
            {
                int i, j;
                cellOf(q.x, q.y, i, j);
                filled[index(i, j)] = 1;
                i0 = std::min(i0, i);
                j0 = std::min(j0, j);
                i1 = std::max(i1, i);
                j1 = std::max(j1, j);
            }
            const double d = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
            if (d == 0.0)
                continue;
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i)
                {
                    const double x = x0 + (i + 0.5) * cell, y = y0 + (j + 0.5) * cell;
                    const double a = ((p[1].x - x) * (p[2].y - y) - (p[2].x - x) * (p[1].y - y)) / d;
                    const double b = ((p[2].x - x) * (p[0].y - y) - (p[0].x - x) * (p[2].y - y)) / d;
                    if (a >= 0.0 && b >= 0.0 && a + b <= 1.0)
                        filled[index(i, j)] = 1;
                }
        }

        // Close gaps of a cell: grow by one, then shrink by one.
        auto grow = [&](char value) {
            std::vector<char> next = filled;
            for (int j = 1; j < ny - 1; ++j)
                for (int i = 1; i < nx - 1; ++i)
                    if (filled[index(i, j)] != value)
                        for (int dj = -1; dj <= 1; ++dj)
                            for (int di = -1; di <= 1; ++di)
                                if (filled[index(i + di, j + dj)] == value)
                                    next[index(i, j)] = value;
            filled.swap(next);
        };
        grow(1);
        grow(0);

        // 2. Regions, 4-connected: empty ones that don't reach the border are holes.
        const double smallestArea = kPi * 0.25 * smallestHole * smallestHole;
        std::vector<int> label(filled.size(), -1);
        auto flood = [&](size_t start, int current, char value, bool& border) {
            std::vector<size_t> cells{ start };
            std::deque<size_t> queue{ start };
            label[start] = current;
            border = false;
            while (!queue.empty())
            {
                const size_t at = queue.front();
                queue.pop_front();
                const int i = static_cast<int>(at % nx), j = static_cast<int>(at / nx);
                if (i == 0 || j == 0 || i == nx - 1 || j == ny - 1)
                    border = true;
                const int di[4] = { 1, -1, 0, 0 }, dj[4] = { 0, 0, 1, -1 };
                for (int k = 0; k < 4; ++k)
                {
                    const int a = i + di[k], b = j + dj[k];
                    if (a < 0 || b < 0 || a >= nx || b >= ny)
                        continue;
                    const size_t next = index(a, b);
                    if (filled[next] != value || label[next] >= 0)
                        continue;
                    label[next] = current;
                    queue.push_back(next);
                    cells.push_back(next);
                }
            }
            return cells;
        };
        int labels = 0;
        for (size_t start = 0; start < filled.size(); ++start)
        {
            if (filled[start] || label[start] >= 0)
                continue;
            bool border = false;
            const std::vector<size_t> cells = flood(start, labels++, 0, border);
            if (!border && static_cast<double>(cells.size()) * cell * cell < smallestArea)
            {
                for (size_t c : cells)
                    filled[c] = 1;
                ++stats.filledHoles;
            }
        }
        std::fill(label.begin(), label.end(), -1);
        for (size_t start = 0; start < filled.size(); ++start)
        {
            if (!filled[start] || label[start] >= 0)
                continue;
            bool border = false;
            const std::vector<size_t> cells = flood(start, labels++, 1, border);
            if (static_cast<double>(cells.size()) * cell * cell < smallestArea)
            {
                for (size_t c : cells)
                    filled[c] = 0;
                ++stats.droppedPieces;
            }
        }

        // 3. Boundary edges, filled on their left, chained into loops; where
        // two cells meet at a corner, turn left, so loops don't cross.
        const int cornersX = nx + 1;
        auto in = [&](int i, int j) { return i >= 0 && j >= 0 && i < nx && j < ny && filled[index(i, j)]; };
        auto corner = [&](int i, int j) { return i + j * cornersX; };
        std::multimap<int, int> edges;
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
        auto cornerPoint = [&](int c) { return Vec2{ x0 + (c % cornersX) * cell, y0 + (c / cornersX) * cell }; };
        std::vector<std::vector<Vec2>> loops;
        while (!edges.empty())
        {
            auto it = edges.begin();
            const int start = it->first;
            int from = start, to = it->second;
            edges.erase(it);
            std::vector<int> loop{ start };
            while (to != start)
            {
                loop.push_back(to);
                auto range = edges.equal_range(to);
                if (range.first == range.second)
                    break;
                auto pick = range.first;
                if (std::next(range.first) != range.second)
                {
                    // The left turn: the outgoing direction anticlockwise from the incoming.
                    const int inX = (to % cornersX) - (from % cornersX), inY = (to / cornersX) - (from / cornersX);
                    for (auto e = range.first; e != range.second; ++e)
                    {
                        const int outX = (e->second % cornersX) - (to % cornersX);
                        const int outY = (e->second / cornersX) - (to / cornersX);
                        if (inX * outY - inY * outX > 0)
                            pick = e;
                    }
                }
                from = to;
                to = pick->second;
                edges.erase(pick);
            }
            if (loop.size() < 4)
                continue;
            std::vector<Vec2> points;
            points.reserve(loop.size());
            for (int c : loop)
                points.push_back(cornerPoint(c));
            loops.push_back(std::move(points));
        }

        // Snap each loop point onto the nearest outline edge within 1.5 cells.
        const std::vector<std::array<int, 2>> outline = OutlineEdges(mesh, normal);
        const double bucket = 2.0 * cell;
        const int bx = std::max(1, static_cast<int>(std::ceil((nx * cell) / bucket)) + 1);
        const int by = std::max(1, static_cast<int>(std::ceil((ny * cell) / bucket)) + 1);
        auto bucketOf = [&](double x, double y, int& i, int& j) {
            i = std::clamp(static_cast<int>(std::floor((x - x0) / bucket)), 0, bx - 1);
            j = std::clamp(static_cast<int>(std::floor((y - y0) / bucket)), 0, by - 1);
        };
        std::vector<uint32_t> start(static_cast<size_t>(bx) * by + 1, 0);
        std::vector<int> items;
        for (int pass = 0; pass < 2; ++pass)
        {
            std::vector<uint32_t> fill;
            if (pass == 1)
            {
                for (size_t k = 1; k < start.size(); ++k)
                    start[k] += start[k - 1];
                items.resize(start.back());
                fill.assign(start.begin(), start.end() - 1);
            }
            for (size_t e = 0; e < outline.size(); ++e)
            {
                const Vec2& a = flat[static_cast<size_t>(outline[e][0])];
                const Vec2& b = flat[static_cast<size_t>(outline[e][1])];
                int i0, j0, i1, j1;
                bucketOf(std::min(a.x, b.x), std::min(a.y, b.y), i0, j0);
                bucketOf(std::max(a.x, b.x), std::max(a.y, b.y), i1, j1);
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i)
                    {
                        const size_t k = static_cast<size_t>(i) + static_cast<size_t>(j) * bx;
                        if (pass == 0)
                            ++start[k + 1];
                        else
                            items[fill[k]++] = static_cast<int>(e);
                    }
            }
        }
        const double reach = 1.5 * cell;
        for (std::vector<Vec2>& loop : loops)
        {
            std::vector<char> snapped(loop.size(), 0);
            for (size_t k = 0; k < loop.size(); ++k)
            {
                const Vec2 p = loop[k];
                int ci, cj;
                bucketOf(p.x, p.y, ci, cj);
                double best = reach * reach;
                Vec2 bestPoint = p;
                for (int j = std::max(0, cj - 1); j <= std::min(by - 1, cj + 1); ++j)
                    for (int i = std::max(0, ci - 1); i <= std::min(bx - 1, ci + 1); ++i)
                    {
                        const size_t b = static_cast<size_t>(i) + static_cast<size_t>(j) * bx;
                        for (uint32_t n = start[b]; n < start[b + 1]; ++n)
                        {
                            const auto& e = outline[static_cast<size_t>(items[n])];
                            const Vec2 q = ClosestOnSegment(p, flat[static_cast<size_t>(e[0])],
                                                            flat[static_cast<size_t>(e[1])]);
                            const double d = (q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y);
                            if (d < best)
                            {
                                best = d;
                                bestPoint = q;
                                snapped[k] = 1;
                            }
                        }
                    }
                loop[k] = bestPoint;
            }
            // Smooth the staircase where nothing was snapped.
            for (int pass = 0; pass < 3; ++pass)
            {
                std::vector<Vec2> next = loop;
                for (size_t k = 0; k < loop.size(); ++k)
                {
                    if (snapped[k])
                        continue;
                    const Vec2& a = loop[(k + loop.size() - 1) % loop.size()];
                    const Vec2& c = loop[(k + 1) % loop.size()];
                    next[k] = { 0.25 * a.x + 0.5 * loop[k].x + 0.25 * c.x, 0.25 * a.y + 0.5 * loop[k].y + 0.25 * c.y };
                }
                loop.swap(next);
            }
            stats.points += loop.size();
            for (char s : snapped)
                stats.snapped += s ? 1 : 0;
            // Points that snapped onto the same place.
            std::vector<Vec2> kept;
            kept.reserve(loop.size());
            for (const Vec2& p : loop)
                if (kept.empty() || std::hypot(p.x - kept.back().x, p.y - kept.back().y) > 1e-6)
                    kept.push_back(p);
            while (kept.size() > 1 && std::hypot(kept.front().x - kept.back().x, kept.front().y - kept.back().y) <= 1e-6)
                kept.pop_back();
            loop.swap(kept);
        }

        // Pieces first (largest first), then holes.
        std::stable_sort(loops.begin(), loops.end(), [](const std::vector<Vec2>& a, const std::vector<Vec2>& b) {
            return SignedArea(a) > SignedArea(b);
        });

        // 4. Fit each loop, as Section does.
        for (const std::vector<Vec2>& loop : loops)
        {
            if (loop.size() < 3)
                continue;
            (SignedArea(loop) > 0.0 ? stats.pieces : stats.holes)++;
            SlicePolyline slice;
            slice.closed = true;
            slice.points.reserve(loop.size());
            for (const Vec2& p : loop)
                slice.points.push_back(frame.ToWorld(p));
            Profile profile = FitProfile(loop, true, tolerance, arcs);
            result.largestDeviation = std::max(result.largestDeviation, profile.largestDeviation);
            result.slicePoints += loop.size();
            result.slices.push_back(std::move(slice));
            result.profiles.push_back(std::move(profile));
            result.reference.push_back(SimplifyPolyline(loop, true, referenceTolerance));
        }
        return result;
    }
}
