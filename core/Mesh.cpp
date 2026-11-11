// Mesh.cpp : triangle meshes from scans.

#include "Mesh.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace scancore
{
    namespace
    {
        // Joins corners that are exactly the same float point.
        class VertexJoiner
        {
        public:
            explicit VertexJoiner(Mesh& mesh, size_t expected) : m_mesh(mesh)
            {
                m_index.reserve(expected);
            }

            int Add(float x, float y, float z)
            {
                Key key;
                std::memcpy(&key.a, &x, 4);
                std::memcpy(&key.b, &y, 4);
                std::memcpy(&key.c, &z, 4);
                // +0 and -0 are the same point.
                if (x == 0.0f) key.a = 0;
                if (y == 0.0f) key.b = 0;
                if (z == 0.0f) key.c = 0;

                const auto found = m_index.find(key);
                if (found != m_index.end())
                    return found->second;
                const int index = static_cast<int>(m_mesh.vertices.size());
                m_mesh.vertices.push_back({ x, y, z });
                m_index.emplace(key, index);
                return index;
            }

        private:
            struct Key
            {
                uint32_t a, b, c;
                bool operator==(const Key& other) const { return a == other.a && b == other.b && c == other.c; }
            };
            struct KeyHash
            {
                size_t operator()(const Key& k) const
                {
                    uint64_t h = 1469598103934665603ull;
                    for (uint32_t v : { k.a, k.b, k.c })
                    {
                        h ^= v;
                        h *= 1099511628211ull;
                        h ^= h >> 29;
                    }
                    return static_cast<size_t>(h);
                }
            };

            Mesh& m_mesh;
            std::unordered_map<Key, int, KeyHash> m_index;
        };

        void AddTriangle(Mesh& mesh, const std::array<int, 3>& t, StlReadInfo& info)
        {
            if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2])
            {
                ++info.degenerate;
                return;
            }
            mesh.triangles.push_back(t);
        }

        bool ParseBinary(const std::string& bytes, Mesh& mesh, StlReadInfo& info)
        {
            uint32_t count = 0;
            std::memcpy(&count, bytes.data() + 80, 4);
            info.binary = true;
            info.fileTriangles = count;

            VertexJoiner joiner(mesh, count / 2 + 16);
            mesh.triangles.reserve(count);
            const char* p = bytes.data() + 84;
            for (uint32_t i = 0; i < count; ++i, p += 50)
            {
                float v[9];
                std::memcpy(v, p + 12, sizeof(v));
                std::array<int, 3> t;
                for (int k = 0; k < 3; ++k)
                    t[k] = joiner.Add(v[3 * k], v[3 * k + 1], v[3 * k + 2]);
                AddTriangle(mesh, t, info);
            }
            return true;
        }

        bool ParseAscii(const std::string& bytes, Mesh& mesh, StlReadInfo& info, std::string& message)
        {
            info.binary = false;
            VertexJoiner joiner(mesh, 1024);
            std::istringstream stream(bytes);
            std::string word;
            std::array<int, 3> t{};
            int corner = 0;
            while (stream >> word)
            {
                if (word == "vertex")
                {
                    double x, y, z;
                    if (!(stream >> x >> y >> z))
                    {
                        message = "A vertex in the STL file isn't three numbers.";
                        return false;
                    }
                    if (corner < 3)
                        t[corner] = joiner.Add(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                    ++corner;
                }
                else if (word == "endfacet")
                {
                    if (corner != 3)
                    {
                        message = "A facet in the STL file doesn't have three vertices.";
                        return false;
                    }
                    ++info.fileTriangles;
                    AddTriangle(mesh, t, info);
                    corner = 0;
                }
            }
            return true;
        }
    }

    bool ParseStl(const std::string& bytes, Mesh& mesh, std::string& message, StlReadInfo* pInfo)
    {
        StlReadInfo info;
        mesh = Mesh();

        // Binary files often start with "solid" too, so the size decides.
        bool binary = false;
        if (bytes.size() >= 84)
        {
            uint32_t count = 0;
            std::memcpy(&count, bytes.data() + 80, 4);
            binary = (84ull + 50ull * count == bytes.size());
        }

        bool ok = false;
        if (binary)
        {
            ok = ParseBinary(bytes, mesh, info);
        }
        else
        {
            size_t start = bytes.find_first_not_of(" \t\r\n");
            if (start == std::string::npos || bytes.compare(start, 5, "solid") != 0)
            {
                message = "The file isn't an STL file: it's neither binary STL nor starts with \"solid\".";
                return false;
            }
            ok = ParseAscii(bytes, mesh, info, message);
        }
        if (!ok)
            return false;

        if (mesh.triangles.empty())
        {
            message = "The STL file has no triangles.";
            return false;
        }
        if (pInfo != nullptr)
            *pInfo = info;
        return true;
    }

    bool ReadStl(const std::filesystem::path& path, Mesh& mesh, std::string& message, StlReadInfo* info)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            message = "Could not open " + path.u8string() + ".";
            return false;
        }
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::string bytes(static_cast<size_t>(size), '\0');
        if (size > 0 && !file.read(&bytes[0], size))
        {
            message = "Could not read " + path.u8string() + ".";
            return false;
        }
        return ParseStl(bytes, mesh, message, info);
    }

    Box3 Bounds(const Mesh& mesh)
    {
        Box3 box;
        for (const Point3& p : mesh.vertices)
            box.Add(p);
        return box;
    }

    double SurfaceArea(const Mesh& mesh)
    {
        double area = 0.0;
        for (const auto& t : mesh.triangles)
        {
            const Point3& a = mesh.vertices[t[0]];
            area += 0.5 * Length(Cross(mesh.vertices[t[1]] - a, mesh.vertices[t[2]] - a));
        }
        return area;
    }

    std::vector<Point3> VertexNormals(const Mesh& mesh)
    {
        std::vector<Point3> normals(mesh.vertices.size());
        for (const auto& t : mesh.triangles)
        {
            const Point3& a = mesh.vertices[t[0]];
            const Point3 n = Cross(mesh.vertices[t[1]] - a, mesh.vertices[t[2]] - a);   // twice the area
            for (int k = 0; k < 3; ++k)
                normals[t[k]] = normals[t[k]] + n;
        }
        for (Point3& n : normals)
            n = Normalised(n);
        return normals;
    }

    Mesh SimplifyForDisplay(const Mesh& mesh, size_t maxTriangles)
    {
        if (mesh.triangles.size() <= maxTriangles || maxTriangles < 4)
            return mesh;

        const Box3 box = Bounds(mesh);
        const double area = SurfaceArea(mesh);
        // About two triangles to a vertex, and each vertex a cell of the surface.
        double cell = std::sqrt(area / (0.5 * static_cast<double>(maxTriangles)));
        const Point3 size = box.Size();
        cell = std::max(cell, 1e-9 * std::max({ size.x, size.y, size.z, 1.0 }));

        for (int attempt = 0; attempt < 12; ++attempt, cell *= 1.25)
        {
            const long long nx = static_cast<long long>(size.x / cell) + 1;
            const long long ny = static_cast<long long>(size.y / cell) + 1;

            std::unordered_map<long long, int> cellIndex;
            cellIndex.reserve(mesh.vertices.size() / 4 + 16);
            Mesh out;
            std::vector<double> counts;
            std::vector<int> map(mesh.vertices.size());
            for (size_t i = 0; i < mesh.vertices.size(); ++i)
            {
                const Point3 d = mesh.vertices[i] - box.low;
                const long long cx = static_cast<long long>(d.x / cell);
                const long long cy = static_cast<long long>(d.y / cell);
                const long long cz = static_cast<long long>(d.z / cell);
                const long long key = (cz * ny + cy) * nx + cx;
                auto found = cellIndex.find(key);
                int index;
                if (found == cellIndex.end())
                {
                    index = static_cast<int>(out.vertices.size());
                    cellIndex.emplace(key, index);
                    out.vertices.push_back(mesh.vertices[i]);
                    counts.push_back(1.0);
                }
                else
                {
                    index = found->second;
                    out.vertices[index] = out.vertices[index] + mesh.vertices[i];
                    counts[index] += 1.0;
                }
                map[i] = index;
            }
            for (size_t i = 0; i < out.vertices.size(); ++i)
                out.vertices[i] = out.vertices[i] / counts[i];

            for (const auto& t : mesh.triangles)
            {
                const std::array<int, 3> s{ map[t[0]], map[t[1]], map[t[2]] };
                if (s[0] != s[1] && s[1] != s[2] && s[0] != s[2])
                    out.triangles.push_back(s);
            }
            if (out.triangles.size() <= maxTriangles)
                return out;
        }

        // Shouldn't happen; fall back to every nth triangle.
        Mesh out = mesh;
        const size_t step = mesh.triangles.size() / maxTriangles + 1;
        out.triangles.clear();
        for (size_t i = 0; i < mesh.triangles.size(); i += step)
            out.triangles.push_back(mesh.triangles[i]);
        return out;
    }

    RayHit IntersectLine(const Mesh& mesh, const Ray& ray)
    {
        // Moller-Trumbore on every triangle, keeping hits on either side of the origin.
        RayHit best;
        best.t = std::numeric_limits<double>::infinity();
        const Point3& d = ray.direction;
        for (size_t i = 0; i < mesh.triangles.size(); ++i)
        {
            const auto& t = mesh.triangles[i];
            const Point3& a = mesh.vertices[t[0]];
            const Point3 e1 = mesh.vertices[t[1]] - a;
            const Point3 e2 = mesh.vertices[t[2]] - a;
            const Point3 p = Cross(d, e2);
            const double det = Dot(e1, p);
            if (std::fabs(det) < 1e-18)
                continue;
            const double inv = 1.0 / det;
            const Point3 s = ray.origin - a;
            const double u = Dot(s, p) * inv;
            if (u < 0.0 || u > 1.0)
                continue;
            const Point3 q = Cross(s, e1);
            const double v = Dot(d, q) * inv;
            if (v < 0.0 || u + v > 1.0)
                continue;
            const double distance = Dot(e2, q) * inv;
            if (distance < best.t)
            {
                best.t = distance;
                best.triangle = static_cast<int>(i);
                best.hit = true;
            }
        }
        if (best.hit)
            best.point = ray.origin + d * best.t;
        return best;
    }

    std::string WriteBinaryStl(const Mesh& mesh, const std::string& header)
    {
        std::string out(80, ' ');
        std::memcpy(&out[0], header.data(), std::min<size_t>(header.size(), 80));
        const uint32_t count = static_cast<uint32_t>(mesh.triangles.size());
        out.append(reinterpret_cast<const char*>(&count), 4);
        out.reserve(out.size() + 50ull * count);
        for (const auto& t : mesh.triangles)
        {
            const Point3& a = mesh.vertices[t[0]];
            const Point3& b = mesh.vertices[t[1]];
            const Point3& c = mesh.vertices[t[2]];
            const Point3 n = Normalised(Cross(b - a, c - a));
            const float values[12] = {
                static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z),
                static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z),
                static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z),
                static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z),
            };
            out.append(reinterpret_cast<const char*>(values), sizeof(values));
            out.append(2, static_cast<char>(0));
        }
        return out;
    }
}
