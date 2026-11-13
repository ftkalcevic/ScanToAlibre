// ScanFile.h : reads a scan file by its extension: .stl or .ply.

#pragma once

#include <filesystem>
#include <string>

#include "Mesh.h"

namespace scancore
{
    struct ScanFileInfo
    {
        std::string description;    // such as "binary STL" or "PLY binary_little_endian"
        size_t fileTriangles = 0;   // triangles (STL) or faces (PLY) as stored
        size_t degenerate = 0;      // left out
        bool pointCloud = false;    // a PLY with no faces, meshed by ReconstructSurface
        bool hasNormals = false;
        size_t points = 0;          // a point cloud's
        double cellSize = 0.0;      // mm, the mesh's grid for a point cloud
    };

    // A PLY point cloud (vertices with normals, no faces) is made into a mesh
    // (Reconstruct); one without normals returns false with a message.
    bool ReadScanFile(const std::filesystem::path& path, Mesh& mesh, ScanFileInfo& info, std::string& message);

    // The same, from the file's bytes and name (only its extension is used).
    bool ParseScanFile(const std::string& bytes, const std::filesystem::path& name, Mesh& mesh, ScanFileInfo& info,
                       std::string& message);
}
