// Reconstruct.cpp : a mesh from a point cloud with normals, by signed distance
// and surface nets.

#include "Reconstruct.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <execution>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace scancore
{
    namespace
    {
        // Integer grid coordinates packed into one key, 21 bits each.
        const int64_t kBits = 21;
        const int64_t kLimit = (int64_t{ 1 } << kBits) - 1;

        uint64_t Key(int64_t i, int64_t j, int64_t k)
        {
            return (static_cast<uint64_t>(i) << (2 * kBits)) | (static_cast<uint64_t>(j) << kBits) |
                   static_cast<uint64_t>(k);
        }

        void Unkey(uint64_t key, int64_t& i, int64_t& j, int64_t& k)
        {
            i = static_cast<int64_t>(key >> (2 * kBits)) & kLimit;
            j = static_cast<int64_t>(key >> kBits) & kLimit;
            k = static_cast<int64_t>(key) & kLimit;
        }

        // Points bucketed into cubes of a given size: sorted by cube, with each
        // cube's first index and count.
        class PointGrid
        {
        public:
            PointGrid(const std::vector<Point3>& points, const Point3& origin, double size)
                : m_points(points), m_origin(origin), m_size(size)
            {
                std::vector<std::pair<uint64_t, int>> keyed(points.size());
                for (size_t i = 0; i < points.size(); ++i)
                {
                    int64_t a, b, c;
                    CellOf(points[i], a, b, c);
                    keyed[i] = { Key(a, b, c), static_cast<int>(i) };
                }
                std::sort(std::execution::par, keyed.begin(), keyed.end());
                m_order.resize(keyed.size());
                for (size_t i = 0; i < keyed.size(); ++i)
                {
                    m_order[i] = keyed[i].second;
                    if (i == 0 || keyed[i].first != keyed[i - 1].first)
                        m_cells[keyed[i].first] = { static_cast<int>(i), 0 };
                    ++m_cells[keyed[i].first].second;
                }
            }

            void CellOf(const Point3& p, int64_t& a, int64_t& b, int64_t& c) const
            {
                a = static_cast<int64_t>(std::floor((p.x - m_origin.x) / m_size)) + 2;
                b = static_cast<int64_t>(std::floor((p.y - m_origin.y) / m_size)) + 2;
                c = static_cast<int64_t>(std::floor((p.z - m_origin.z) / m_size)) + 2;
            }

            // Calls visit(index) for each point in the 27 cubes round p.
            template <class Visit>
            void Around(const Point3& p, Visit visit) const
            {
                int64_t a, b, c;
                CellOf(p, a, b, c);
                for (int64_t i = a - 1; i <= a + 1; ++i)
                    for (int64_t j = b - 1; j <= b + 1; ++j)
                        for (int64_t k = c - 1; k <= c + 1; ++k)
                        {
                            if (i < 0 || j < 0 || k < 0)
                                continue;
                            const auto found = m_cells.find(Key(i, j, k));
                            if (found == m_cells.end())
                                continue;
                            for (int n = 0; n < found->second.second; ++n)
                                visit(m_order[found->second.first + n]);
                        }
            }

            size_t CellCount() const { return m_cells.size(); }

        private:
            const std::vector<Point3>& m_points;
            Point3 m_origin;
            double m_size;
            std::vector<int> m_order;
            std::unordered_map<uint64_t, std::pair<int, int>> m_cells;
        };

        size_t OccupiedCells(const std::vector<Point3>& points, const Point3& origin, double size)
        {
            std::vector<uint64_t> keys(points.size());
            for (size_t i = 0; i < points.size(); ++i)
            {
                const int64_t a = static_cast<int64_t>(std::floor((points[i].x - origin.x) / size));
                const int64_t b = static_cast<int64_t>(std::floor((points[i].y - origin.y) / size));
                const int64_t c = static_cast<int64_t>(std::floor((points[i].z - origin.z) / size));
                keys[i] = Key(a, b, c);
            }
            std::sort(std::execution::par, keys.begin(), keys.end());
            return static_cast<size_t>(std::unique(keys.begin(), keys.end()) - keys.begin());
        }
    }

    double PointSpacing(const std::vector<Point3>& points)
    {
        // The side of the square each point covers: the circle out to the 12th
        // nearest neighbour holds about 12 points' squares. The nearest
        // neighbour alone was far too close for a scanner's points, which come
        // in lines (9 triangles a point instead of 2 on Frank's clouds).
        const size_t kNeighbours = 12;
        const double kPi = 3.14159265358979323846;
        if (points.size() < 2)
            return 0.0;
        Box3 box;
        for (const Point3& p : points)
            box.Add(p);
        const Point3 size = box.Size();
        const double diagonal = std::max(Length(size), 1e-9);
        // A first guess for a surface: the side of a square holding one point.
        double bucket = std::max(diagonal / std::sqrt(static_cast<double>(points.size())), diagonal * 1e-6);

        const size_t sampleCount = std::min<size_t>(points.size(), 4000);
        const size_t stride = std::max<size_t>(1, points.size() / sampleCount);
        for (int attempt = 0; attempt < 8; ++attempt, bucket *= 2.0)
        {
            const PointGrid grid(points, box.low, bucket);
            std::vector<double> spacings;
            std::vector<double> distances;
            for (size_t s = 0; s < points.size(); s += stride)
            {
                const Point3& p = points[s];
                distances.clear();
                grid.Around(p, [&](int other) {
                    if (static_cast<size_t>(other) != s)
                        distances.push_back(Length(points[other] - p));
                });
                // Only neighbours within the bucket are certainly the nearest.
                if (distances.size() < kNeighbours)
                    continue;
                std::nth_element(distances.begin(), distances.begin() + (kNeighbours - 1), distances.end());
                const double reach = distances[kNeighbours - 1];
                if (reach <= bucket && reach > 0.0)
                    spacings.push_back(reach * std::sqrt(kPi / static_cast<double>(kNeighbours)));
            }
            if (spacings.size() >= points.size() / stride / 2)
            {
                std::nth_element(spacings.begin(), spacings.begin() + spacings.size() / 2, spacings.end());
                return spacings[spacings.size() / 2];
            }
        }
        return bucket;
    }

    bool ReconstructSurface(const std::vector<Point3>& points, const std::vector<Point3>& normals,
                            const ReconstructOptions& options, Mesh& mesh, std::string& message,
                            ReconstructResult* pResult)
    {
        mesh = Mesh();
        ReconstructResult result;
        result.points = points.size();
        if (points.size() < 10 || normals.size() != points.size())
        {
            message = "A point cloud needs at least 10 points, each with a normal, to make a mesh from.";
            return false;
        }

        Box3 box;
        for (const Point3& p : points)
            box.Add(p);

        // The cell: the points' spacing, grown if the points would fill too many cells.
        result.spacing = PointSpacing(points);
        double cell = (options.cellSize > 0.0) ? options.cellSize : result.spacing;
        if (!(cell > 0.0))
        {
            message = "The points are all in one place.";
            return false;
        }
        for (int attempt = 0; attempt < 10 && options.cellSize <= 0.0; ++attempt)
        {
            const size_t occupied = OccupiedCells(points, box.low, cell);
            if (occupied <= options.maxPointCells)
                break;
            cell *= std::sqrt(static_cast<double>(occupied) / static_cast<double>(options.maxPointCells)) * 1.02;
        }
        const Point3 extent = box.Size();
        if (std::max({ extent.x, extent.y, extent.z }) / cell > static_cast<double>(kLimit - 8))
        {
            message = "The point cloud is too large for its spacing to make a mesh from.";
            return false;
        }
        result.cellSize = cell;

        const double sigma = std::max(options.smoothing, 0.25) * cell;
        const int gap = std::clamp(options.gapCells, 1, 4);
        const double kVertexReach = 1.5;        // cells, from a surface vertex to the nearest point
        const double reach = std::max(2.0 * sigma, (gap + 1) * cell);
        const PointGrid nearby(points, box.low, reach);
        const double margin = (gap + 2) * cell;
        const Point3 origin = box.low - Point3{ margin, margin, margin };
        auto cellCoord = [&](const Point3& p, int64_t& a, int64_t& b, int64_t& c) {
            a = static_cast<int64_t>(std::floor((p.x - origin.x) / cell));
            b = static_cast<int64_t>(std::floor((p.y - origin.y) / cell));
            c = static_cast<int64_t>(std::floor((p.z - origin.z) / cell));
        };
        auto cornerPosition = [&](int64_t a, int64_t b, int64_t c) {
            return origin + Point3{ a * cell, b * cell, c * cell };
        };

        // Active cells: those holding a point, and those within the gap of them.
        std::vector<uint64_t> occupied(points.size());
        for (size_t i = 0; i < points.size(); ++i)
        {
            int64_t a, b, c;
            cellCoord(points[i], a, b, c);
            occupied[i] = Key(a, b, c);
        }
        std::sort(std::execution::par, occupied.begin(), occupied.end());
        occupied.erase(std::unique(occupied.begin(), occupied.end()), occupied.end());
        std::vector<uint64_t> cells;
        cells.reserve(occupied.size() * 27);
        for (uint64_t key : occupied)
        {
            int64_t a, b, c;
            Unkey(key, a, b, c);
            for (int64_t i = a - gap; i <= a + gap; ++i)
                for (int64_t j = b - gap; j <= b + gap; ++j)
                    for (int64_t k = c - gap; k <= c + gap; ++k)
                        cells.push_back(Key(i, j, k));
        }
        std::sort(std::execution::par, cells.begin(), cells.end());
        cells.erase(std::unique(cells.begin(), cells.end()), cells.end());

        // Their corners, and the signed distance at each.
        std::vector<uint64_t> corners;
        corners.reserve(cells.size() * 2);
        for (uint64_t key : cells)
        {
            int64_t a, b, c;
            Unkey(key, a, b, c);
            for (int n = 0; n < 8; ++n)
                corners.push_back(Key(a + (n & 1), b + ((n >> 1) & 1), c + ((n >> 2) & 1)));
        }
        std::sort(std::execution::par, corners.begin(), corners.end());
        corners.erase(std::unique(corners.begin(), corners.end()), corners.end());
        result.gridCorners = corners.size();

        const double nan = std::numeric_limits<double>::quiet_NaN();
        std::vector<double> distance(corners.size(), nan);      // signed
        std::vector<double> closest(corners.size(), nan);       // to the nearest point
        std::vector<size_t> order(corners.size());
        std::iota(order.begin(), order.end(), size_t{ 0 });
        std::for_each(std::execution::par, order.begin(), order.end(), [&](size_t n) {
            int64_t a, b, c;
            Unkey(corners[n], a, b, c);
            const Point3 x = cornerPosition(a, b, c);
            double sum = 0.0, weights = 0.0, best = std::numeric_limits<double>::infinity();
            nearby.Around(x, [&](int i) {
                const Point3 d = x - points[i];
                const double d2 = Dot(d, d);
                best = std::min(best, d2);
                if (d2 > reach * reach)
                    return;
                const double w = std::exp(-d2 / (2.0 * sigma * sigma));
                sum += w * Dot(normals[i], d);
                weights += w;
            });
            if (weights > 1e-12)
            {
                distance[n] = sum / weights;
                closest[n] = std::sqrt(best);
            }
        });

        auto cornerIndex = [&](int64_t a, int64_t b, int64_t c) -> int64_t {
            const uint64_t key = Key(a, b, c);
            const auto found = std::lower_bound(corners.begin(), corners.end(), key);
            return (found != corners.end() && *found == key) ? static_cast<int64_t>(found - corners.begin()) : -1;
        };

        // A vertex in each cell the surface crosses, near the points: the
        // average of where it crosses the cell's twelve edges.
        static const int kEdges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 },
                                           { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        std::vector<int> cellVertex(cells.size(), -1);
        std::vector<Point3> cellPoint(cells.size());
        std::vector<char> cellHas(cells.size(), 0);
        std::vector<size_t> cellOrder(cells.size());
        std::iota(cellOrder.begin(), cellOrder.end(), size_t{ 0 });
        std::for_each(std::execution::par, cellOrder.begin(), cellOrder.end(), [&](size_t n) {
            int64_t a, b, c;
            Unkey(cells[n], a, b, c);
            double f[8];
            Point3 x[8];
            double near = std::numeric_limits<double>::infinity();
            for (int q = 0; q < 8; ++q)
            {
                const int64_t qa = a + (q & 1), qb = b + ((q >> 1) & 1), qc = c + ((q >> 2) & 1);
                const int64_t index = cornerIndex(qa, qb, qc);
                if (index < 0 || std::isnan(distance[index]))
                    return;
                f[q] = distance[index];
                near = std::min(near, closest[index]);
                x[q] = cornerPosition(qa, qb, qc);
            }
            // Only where points are close by, so edges and gaps stay open.
            if (near > gap * cell)
                return;
            Point3 sum;
            int crossings = 0;
            for (const auto& e : kEdges)
            {
                const double f0 = f[e[0]], f1 = f[e[1]];
                if ((f0 < 0.0) == (f1 < 0.0))
                    continue;
                const double t = f0 / (f0 - f1);
                sum = sum + x[e[0]] + (x[e[1]] - x[e[0]]) * t;
                ++crossings;
            }
            if (crossings == 0)
                return;
            const Point3 vertex = sum / static_cast<double>(crossings);
            // The vertex itself must be near a point: between two lines of the
            // scanner's points it's at most half the gap away, but past the
            // scan's edge (or into a real hole) it would be further, and that
            // skirt is left out.
            double nearest2 = std::numeric_limits<double>::infinity();
            nearby.Around(vertex, [&](int i) {
                const Point3 d = vertex - points[i];
                nearest2 = std::min(nearest2, Dot(d, d));
            });
            if (nearest2 > kVertexReach * kVertexReach * cell * cell)
                return;
            cellPoint[n] = vertex;
            cellHas[n] = 1;
        });
        for (size_t n = 0; n < cells.size(); ++n)
        {
            if (!cellHas[n])
                continue;
            cellVertex[n] = static_cast<int>(mesh.vertices.size());
            mesh.vertices.push_back(cellPoint[n]);
        }
        auto vertexOfCell = [&](int64_t a, int64_t b, int64_t c) -> int {
            const uint64_t key = Key(a, b, c);
            const auto found = std::lower_bound(cells.begin(), cells.end(), key);
            return (found != cells.end() && *found == key) ? cellVertex[found - cells.begin()] : -1;
        };

        // A quad for each grid edge the surface crosses, joining the vertices of
        // the four cells round it, turned so it faces the positive side.
        for (size_t n = 0; n < corners.size(); ++n)
        {
            const double f0 = distance[n];
            if (std::isnan(f0))
                continue;
            int64_t a, b, c;
            Unkey(corners[n], a, b, c);
            const int64_t at[3] = { a, b, c };
            for (int axis = 0; axis < 3; ++axis)
            {
                int64_t next[3] = { a, b, c };
                ++next[axis];
                const int64_t other = cornerIndex(next[0], next[1], next[2]);
                if (other < 0 || std::isnan(distance[other]))
                    continue;
                const double f1 = distance[other];
                if ((f0 < 0.0) == (f1 < 0.0))
                    continue;

                // The four cells sharing this edge, anticlockwise about +axis.
                const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                const int offsets[4][2] = { { -1, -1 }, { 0, -1 }, { 0, 0 }, { -1, 0 } };
                int quad[4];
                bool complete = true;
                for (int q = 0; q < 4 && complete; ++q)
                {
                    int64_t cellAt[3] = { at[0], at[1], at[2] };
                    cellAt[u] += offsets[q][0];
                    cellAt[v] += offsets[q][1];
                    quad[q] = vertexOfCell(cellAt[0], cellAt[1], cellAt[2]);
                    complete = quad[q] >= 0;
                }
                if (!complete)
                    continue;
                if (!(f0 < 0.0))
                    std::swap(quad[1], quad[3]);        // the surface faces the other way
                // Split along the shorter diagonal.
                const double d02 = Length(mesh.vertices[quad[0]] - mesh.vertices[quad[2]]);
                const double d13 = Length(mesh.vertices[quad[1]] - mesh.vertices[quad[3]]);
                if (d02 <= d13)
                {
                    mesh.triangles.push_back({ quad[0], quad[1], quad[2] });
                    mesh.triangles.push_back({ quad[0], quad[2], quad[3] });
                }
                else
                {
                    mesh.triangles.push_back({ quad[0], quad[1], quad[3] });
                    mesh.triangles.push_back({ quad[1], quad[2], quad[3] });
                }
            }
        }

        // Vertices no triangle uses (a cell whose edges are all half done) go.
        std::vector<int> used(mesh.vertices.size(), -1);
        Mesh compact;
        for (auto& t : mesh.triangles)
        {
            for (int& k : t)
            {
                if (used[k] < 0)
                {
                    used[k] = static_cast<int>(compact.vertices.size());
                    compact.vertices.push_back(mesh.vertices[k]);
                }
                k = used[k];
            }
        }
        compact.triangles = std::move(mesh.triangles);
        mesh = std::move(compact);

        result.triangles = mesh.triangles.size();
        if (pResult != nullptr)
            *pResult = result;
        if (mesh.triangles.empty())
        {
            message = "No surface could be made from the point cloud.";
            return false;
        }
        return true;
    }
}
