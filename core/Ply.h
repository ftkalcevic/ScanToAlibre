// Ply.h : reads PLY files (ASCII, binary little or big endian).
//
// Vertices take x, y and z, of any numeric type; faces take vertex_indices (or
// vertex_index), a list of any numeric types, and polygons are split into
// triangles as fans. Other properties and elements are read past.

#pragma once

#include <filesystem>
#include <string>

#include "Mesh.h"

namespace scancore
{
    struct PlyReadInfo
    {
        std::string format;             // "ascii", "binary_little_endian" or "binary_big_endian"
        size_t vertices = 0;
        size_t faces = 0;               // as stored
        size_t degenerate = 0;          // triangles left out: two corners the same
        bool hasNormals = false;        // nx, ny and nz on the vertices
    };

    // A file with vertices but no faces is a point cloud: it reads, with no
    // triangles, and the caller decides what to do with it.
    bool ParsePly(const std::string& bytes, Mesh& mesh, std::string& message, PlyReadInfo* info = nullptr);
    bool ReadPly(const std::filesystem::path& path, Mesh& mesh, std::string& message, PlyReadInfo* info = nullptr);

    // Whole-file reading shared by the readers.
    bool ReadFileBytes(const std::filesystem::path& path, std::string& bytes, std::string& message);
}
