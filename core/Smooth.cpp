// Smooth.cpp : Laplacian, Taubin and bilateral smoothing.

#include "Smooth.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <execution>
#include <numeric>
#include <unordered_map>

namespace scancore
{
    namespace
    {
        uint64_t EdgeKey(int a, int b)
        {
            if (a > b)
                std::swap(a, b);
            return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
        }

        // One umbrella step: each free vertex moves factor of the way to its
        // neighbours' middle (a negative factor moves it away).
        void UmbrellaStep(std::vector<Point3>& points, const std::vector<std::vector<int>>& neighbours,
                          const std::vector<bool>& fixed, double factor)
        {
            const std::vector<Point3> before = points;
            std::vector<size_t> order(points.size());
            std::iota(order.begin(), order.end(), size_t{ 0 });
            std::for_each(std::execution::par, order.begin(), order.end(), [&](size_t v) {
                if (fixed[v] || neighbours[v].empty())
                    return;
                Point3 sum;
                for (int n : neighbours[v])
                    sum = sum + before[n];
                const Point3 middle = sum / static_cast<double>(neighbours[v].size());
                points[v] = before[v] + (middle - before[v]) * factor;
            });
        }

        // Bilateral normal filtering (Sun et al. 2007; Zheng et al. 2011): each
        // triangle's normal is averaged with those of the triangles sharing a
        // vertex with it, weighted by area and by how alike the normals are (a
        // Gaussian of their difference, so triangles across an edge hardly count),
        // `passes` times; then the vertices are moved, a few times over, towards
        // the planes of their triangles' filtered normals.
        void BilateralNormalSmooth(std::vector<Point3>& points, const Mesh& mesh, const std::vector<bool>& fixed,
                                   int passes, double strength)
        {
            const size_t faceCount = mesh.triangles.size();
            std::vector<std::vector<int>> vertexFaces(points.size());
            for (size_t f = 0; f < faceCount; ++f)
                for (int k : mesh.triangles[f])
                    vertexFaces[k].push_back(static_cast<int>(f));

            std::vector<Point3> normals(faceCount);
            std::vector<double> areas(faceCount);
            for (size_t f = 0; f < faceCount; ++f)
            {
                const auto& t = mesh.triangles[f];
                const Point3 n = Cross(points[t[1]] - points[t[0]], points[t[2]] - points[t[0]]);
                areas[f] = 0.5 * Length(n);
                normals[f] = Normalised(n);
            }

            std::vector<size_t> faces(faceCount);
            std::iota(faces.begin(), faces.end(), size_t{ 0 });

            // Normals within about 20 to 35 degrees of each other count; across a
            // sharp edge they don't.
            const double sigma = 0.25 + 0.35 * std::clamp(strength, 0.0, 1.0);
            const double twoSigma2 = 2.0 * sigma * sigma;
            for (int pass = 0; pass < passes; ++pass)
            {
                const std::vector<Point3> before = normals;
                std::for_each(std::execution::par, faces.begin(), faces.end(), [&](size_t f) {
                    if (Length(before[f]) == 0.0)
                        return;
                    Point3 sum;
                    std::vector<int> around;
                    for (int k : mesh.triangles[f])
                        around.insert(around.end(), vertexFaces[k].begin(), vertexFaces[k].end());
                    std::sort(around.begin(), around.end());
                    around.erase(std::unique(around.begin(), around.end()), around.end());
                    for (int g : around)
                    {
                        const Point3 d = before[f] - before[g];
                        sum = sum + before[g] * (areas[g] * std::exp(-Dot(d, d) / twoSigma2));
                    }
                    const Point3 n = Normalised(sum);
                    if (Length(n) > 0.0)
                        normals[f] = n;
                });
            }

            // Vertex update: each vertex moves by the average, over its triangles, of
            // its offset to the triangle's plane through its middle.
            std::vector<size_t> vertices(points.size());
            std::iota(vertices.begin(), vertices.end(), size_t{ 0 });
            const int updates = 10;
            for (int iteration = 0; iteration < updates; ++iteration)
            {
                const std::vector<Point3> before = points;
                std::for_each(std::execution::par, vertices.begin(), vertices.end(), [&](size_t v) {
                    if (fixed[v] || vertexFaces[v].empty())
                        return;
                    Point3 move;
                    int count = 0;
                    for (int f : vertexFaces[v])
                    {
                        if (Length(normals[f]) == 0.0)
                            continue;
                        const auto& t = mesh.triangles[f];
                        const Point3 middle = (before[t[0]] + before[t[1]] + before[t[2]]) / 3.0;
                        move = move + normals[f] * Dot(normals[f], middle - before[v]);
                        ++count;
                    }
                    if (count > 0)
                        points[v] = before[v] + move / static_cast<double>(count);
                });
            }
        }
    }

    std::vector<bool> OpenEdgeVertices(const Mesh& mesh)
    {
        std::unordered_map<uint64_t, int> use;
        use.reserve(mesh.triangles.size() * 2);
        for (const auto& t : mesh.triangles)
            for (int k = 0; k < 3; ++k)
                ++use[EdgeKey(t[k], t[(k + 1) % 3])];
        std::vector<bool> open(mesh.vertices.size(), false);
        for (const auto& entry : use)
        {
            if (entry.second == 1)
            {
                open[static_cast<size_t>(entry.first >> 32)] = true;
                open[static_cast<size_t>(entry.first & 0xffffffffu)] = true;
            }
        }
        return open;
    }

    std::vector<std::vector<int>> VertexNeighbours(const Mesh& mesh)
    {
        std::vector<std::vector<int>> neighbours(mesh.vertices.size());
        for (const auto& t : mesh.triangles)
        {
            for (int k = 0; k < 3; ++k)
            {
                neighbours[t[k]].push_back(t[(k + 1) % 3]);
                neighbours[t[k]].push_back(t[(k + 2) % 3]);
            }
        }
        for (auto& list : neighbours)
        {
            std::sort(list.begin(), list.end());
            list.erase(std::unique(list.begin(), list.end()), list.end());
        }
        return neighbours;
    }

    Mesh Smooth(const Mesh& mesh, const SmoothOptions& options, SmoothResult* pResult)
    {
        Mesh out = mesh;
        const std::vector<std::vector<int>> neighbours = VertexNeighbours(mesh);
        const std::vector<bool> fixed =
            options.keepOpenEdges ? OpenEdgeVertices(mesh) : std::vector<bool>(mesh.vertices.size(), false);
        const double strength = std::clamp(options.strength, 0.0, 1.0);

        for (int pass = 0; pass < options.passes; ++pass)
        {
            switch (options.method)
            {
            case SmoothMethod::Laplacian:
                UmbrellaStep(out.vertices, neighbours, fixed, strength);
                break;
            case SmoothMethod::Taubin:
            {
                // lambda forwards, mu back, with a pass-band of 0.1 (Taubin's suggestion).
                const double lambda = std::max(strength, 0.01);
                const double mu = 1.0 / (0.1 - 1.0 / lambda);
                UmbrellaStep(out.vertices, neighbours, fixed, lambda);
                UmbrellaStep(out.vertices, neighbours, fixed, mu);
                break;
            }
            case SmoothMethod::Bilateral:
                break;      // all its passes at once, below
            }
        }

        if (options.method == SmoothMethod::Bilateral)
            BilateralNormalSmooth(out.vertices, mesh, fixed, options.passes, strength);

        if (pResult != nullptr)
        {
            SmoothResult result;
            double sum = 0.0;
            for (size_t v = 0; v < out.vertices.size(); ++v)
            {
                const double move = Length(out.vertices[v] - mesh.vertices[v]);
                result.largestMove = std::max(result.largestMove, move);
                sum += move;
            }
            result.averageMove = out.vertices.empty() ? 0.0 : sum / static_cast<double>(out.vertices.size());
            *pResult = result;
        }
        return out;
    }
}
