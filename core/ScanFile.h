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
        bool pointCloud = false;    // a PLY with no faces
        bool hasNormals = false;
    };

    // Point clouds aren't loaded yet (they'll be made into a mesh): one returns
    // false with info.pointCloud set and a message saying so.
    bool ReadScanFile(const std::filesystem::path& path, Mesh& mesh, ScanFileInfo& info, std::string& message);

    // The same, from the file's bytes and name (only its extension is used).
    bool ParseScanFile(const std::string& bytes, const std::filesystem::path& name, Mesh& mesh, ScanFileInfo& info,
                       std::string& message);
}
