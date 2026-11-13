// ScanFile.cpp : reads a scan file by its extension.

#include "ScanFile.h"

#include <algorithm>
#include <cctype>

#include "Ply.h"
#include "Reconstruct.h"

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
            std::vector<Point3> normals;
            if (!ParsePly(bytes, mesh, message, &ply, &normals))
                return false;
            info.description = "PLY " + ply.format;
            info.fileTriangles = ply.faces;
            info.degenerate = ply.degenerate;
            info.hasNormals = ply.hasNormals;
            if (mesh.triangles.empty())
            {
                info.pointCloud = true;
                info.points = mesh.vertices.size();
                if (!ply.hasNormals)
                {
                    message = "This PLY file is a point cloud (" + std::to_string(mesh.vertices.size()) +
                              " points) without normals. Making a mesh needs each point's normal (nx, ny, nz); "
                              "export the cloud with normals, or load a mesh.";
                    mesh = Mesh();
                    return false;
                }
                const std::vector<Point3> points = std::move(mesh.vertices);
                ReconstructResult made;
                if (!ReconstructSurface(points, normals, ReconstructOptions(), mesh, message, &made))
                    return false;
                info.cellSize = made.cellSize;
                info.description = "PLY point cloud (" + ply.format + "), meshed";
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
