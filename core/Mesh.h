// Mesh.h : triangle meshes from scans: reading STL, simplifying for display,
// and finding where a line meets the mesh.

#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "Geometry.h"

namespace scancore
{
    // Triangles index shared vertices, anticlockwise seen from outside.
    struct Mesh
    {
        std::vector<Point3> vertices;
        std::vector<std::array<int, 3>> triangles;
    };

    struct StlReadInfo
    {
        bool binary = false;
        size_t fileTriangles = 0;       // as stored, before joining vertices
        size_t degenerate = 0;          // left out: two corners the same
    };

    // Reads binary or ASCII STL in millimetres, joining corners that are exactly
    // the same point. On failure, message says why.
    bool ReadStl(const std::filesystem::path& path, Mesh& mesh, std::string& message, StlReadInfo* info = nullptr);

    // The same, from the file's bytes.
    bool ParseStl(const std::string& bytes, Mesh& mesh, std::string& message, StlReadInfo* info = nullptr);

    Box3 Bounds(const Mesh& mesh);
    double SurfaceArea(const Mesh& mesh);

    // Each vertex's normal: the area-weighted sum of its triangles' normals, made unit length.
    std::vector<Point3> VertexNormals(const Mesh& mesh);

    // A simpler mesh of at most about maxTriangles, by clustering vertices on a grid
    // (each cell's vertices become their average). A mesh already small enough is
    // returned as it is.
    Mesh SimplifyForDisplay(const Mesh& mesh, size_t maxTriangles);

    struct Ray
    {
        Point3 origin;
        Point3 direction;
    };

    struct RayHit
    {
        bool hit = false;
        Point3 point;
        int triangle = -1;
        double t = 0.0;     // along the ray: point = origin + direction * t
    };

    // Where the whole line through the ray meets the mesh, at the smallest t
    // (nearest the origin's end when direction points away from the viewer).
    RayHit IntersectLine(const Mesh& mesh, const Ray& ray);

    // The mesh as a binary STL file's bytes, in mm, with each triangle's normal.
    std::string WriteBinaryStl(const Mesh& mesh, const std::string& header);
}
