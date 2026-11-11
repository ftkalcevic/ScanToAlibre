// ScanFile.cpp : reads a scan file by its extension.

#include "ScanFile.h"

#include <algorithm>
#include <cctype>

#include "Ply.h"

namespace scancore
{
    bool ParseScanFile(const std::string& bytes, const std::filesystem::path& name, Mesh& mesh, ScanFileInfo& info,
                       std::string& message)
    {
        info = ScanFileInfo();
        std::string extension = name.extension().u8string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (extension == ".stl")
        {
            StlReadInfo stl;
            if (!ParseStl(bytes, mesh, message, &stl))
                return false;
            info.description = stl.binary ? "binary STL" : "ASCII STL";
            info.fileTriangles = stl.fileTriangles;
            info.degenerate = stl.degenerate;
            return true;
        }

        if (extension == ".ply")
        {
            PlyReadInfo ply;
            if (!ParsePly(bytes, mesh, message, &ply))
                return false;
            info.description = "PLY " + ply.format;
            info.fileTriangles = ply.faces;
            info.degenerate = ply.degenerate;
            info.hasNormals = ply.hasNormals;
            if (mesh.triangles.empty())
            {
                info.pointCloud = true;
                message = "This PLY file is a point cloud (" + std::to_string(mesh.vertices.size()) +
                          " points, no faces). Making a mesh from a point cloud isn't built yet; load a mesh "
                          "(STL, or PLY with faces) for now.";
                mesh = Mesh();
                return false;
            }
            return true;
        }

        message = "Scans must be .stl or .ply files.";
        return false;
    }

    bool ReadScanFile(const std::filesystem::path& path, Mesh& mesh, ScanFileInfo& info, std::string& message)
    {
        std::string bytes;
        if (!ReadFileBytes(path, bytes, message))
            return false;
        return ParseScanFile(bytes, path, mesh, info, message);
    }
}
