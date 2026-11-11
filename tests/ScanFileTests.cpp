// ScanFileTests.cpp : PLY reading, choosing the reader by extension, and the
// scan data saved in the part.

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Ply.h"
#include "ScanData.h"
#include "ScanFile.h"

using namespace scancore;

namespace
{
    // The unit square's corners, as two triangles (or one quad).
    const double kCorners[4][3] = { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 } };

    template <class T>
    void PutBinary(std::string& out, T value, bool bigEndian)
    {
        char raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        if (bigEndian)
            std::reverse(raw, raw + sizeof(T));
        out.append(raw, sizeof(T));
    }

    // A square with normals and a quad face carrying a texture list too, as
    // scanners write them.
    std::string BinaryPly(bool bigEndian)
    {
        std::string out = "ply\n";
        out += bigEndian ? "format binary_big_endian 1.0\n" : "format binary_little_endian 1.0\n";
        out += "comment File generated\n"
               "element vertex 4\n"
               "property float x\nproperty float y\nproperty float z\n"
               "property float nx\nproperty float ny\nproperty float nz\n"
               "element face 1\n"
               "property list uchar int32 vertex_indices\n"
               "property list uint8 float texcoord\n"
               "end_header\n";
        for (const auto& c : kCorners)
        {
            for (double v : c)
                PutBinary<float>(out, static_cast<float>(v), bigEndian);
            PutBinary<float>(out, 0.0f, bigEndian);
            PutBinary<float>(out, 0.0f, bigEndian);
            PutBinary<float>(out, 1.0f, bigEndian);
        }
        PutBinary<uint8_t>(out, 4, bigEndian);
        for (int32_t i : { 0, 1, 2, 3 })
            PutBinary<int32_t>(out, i, bigEndian);
        PutBinary<uint8_t>(out, 2, bigEndian);
        PutBinary<float>(out, 0.5f, bigEndian);
        PutBinary<float>(out, 0.25f, bigEndian);
        return out;
    }
}

TEST_CASE("an ASCII PLY mesh is read, with its polygons split into triangles")
{
    const std::string text =
        "ply\r\nformat ascii 1.0\r\ncomment made by hand\r\n"
        "element vertex 4\r\nproperty double x\r\nproperty double y\r\nproperty double z\r\nproperty uchar red\r\n"
        "element face 1\r\nproperty list uchar int vertex_index\r\n"
        "end_header\r\n"
        "0 0 0 255\r\n10 0 0 255\r\n10 10 0 255\r\n0 10 0 255\r\n"
        "4 0 1 2 3\r\n";
    Mesh mesh;
    std::string message;
    PlyReadInfo info;
    REQUIRE(ParsePly(text, mesh, message, &info));
    CHECK(info.format == "ascii");
    CHECK(mesh.vertices.size() == 4);
    REQUIRE(mesh.triangles.size() == 2);
    CHECK(mesh.vertices[2].x == 10.0);
    CHECK(mesh.vertices[2].y == 10.0);
    CHECK(SurfaceArea(mesh) == doctest::Approx(100.0));
    CHECK_FALSE(info.hasNormals);
}

TEST_CASE("binary PLY meshes are read in either byte order, past other properties and lists")
{
    for (bool bigEndian : { false, true })
    {
        Mesh mesh;
        std::string message;
        PlyReadInfo info;
        REQUIRE(ParsePly(BinaryPly(bigEndian), mesh, message, &info));
        CHECK(info.hasNormals);
        CHECK(info.faces == 1);
        CHECK(mesh.vertices.size() == 4);
        REQUIRE(mesh.triangles.size() == 2);
        CHECK(mesh.vertices[1].x == 10.0);
        CHECK(mesh.vertices[3].y == 10.0);
        CHECK(SurfaceArea(mesh) == doctest::Approx(100.0));
    }
}

TEST_CASE("a PLY point cloud reads with no triangles, and Load says it isn't supported yet")
{
    const std::string text =
        "ply\nformat ascii 1.0\nelement vertex 3\n"
        "property float x\nproperty float y\nproperty float z\nproperty float intensity\n"
        "end_header\n1 2 3 0.5\n4 5 6 0.5\n7 8 9 0.5\n";
    Mesh mesh;
    std::string message;
    REQUIRE(ParsePly(text, mesh, message));
    CHECK(mesh.vertices.size() == 3);
    CHECK(mesh.triangles.empty());

    ScanFileInfo info;
    CHECK_FALSE(ParseScanFile(text, "cloud.PLY", mesh, info, message));
    CHECK(info.pointCloud);
    CHECK(message.find("point cloud") != std::string::npos);
}

TEST_CASE("broken PLY files are refused with a reason")
{
    Mesh mesh;
    std::string message;
    CHECK_FALSE(ParsePly("solid nope", mesh, message));
    CHECK_FALSE(ParsePly("ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\n", mesh, message));
    CHECK_FALSE(ParsePly("ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nend_header\n1\n", mesh, message));
    CHECK_FALSE(ParsePly("ply\nformat ascii 1.0\nelement vertex 2\nproperty float x\nproperty float y\nproperty float z\n"
                         "end_header\n1 2 3\n", mesh, message));
    CHECK_FALSE(ParsePly("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
                         "element face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 7\n",
                         mesh, message));
    CHECK(message.find("doesn't exist") != std::string::npos);

    std::string truncated = BinaryPly(false);
    truncated.resize(truncated.size() - 6);
    CHECK_FALSE(ParsePly(truncated, mesh, message));
}

TEST_CASE("scan files are read by their extension")
{
    Mesh mesh;
    std::string message;
    ScanFileInfo info;
    REQUIRE(ParseScanFile(BinaryPly(false), "Mesh.ply", mesh, info, message));
    CHECK(info.description == "PLY binary_little_endian");
    CHECK(mesh.triangles.size() == 2);

    CHECK_FALSE(ParseScanFile(BinaryPly(false), "Mesh.obj", mesh, info, message));
    CHECK(message.find(".stl or .ply") != std::string::npos);
}

TEST_CASE("scan data round trips, and newer or damaged data is refused")
{
    ScanData scan;
    std::string message;
    ScanFileInfo info;
    REQUIRE(ParseScanFile(BinaryPly(false), "Mesh.ply", scan.mesh, info, message));
    scan.sourceName = u8"C:\\scans\\cover \u00e9.ply";

    const std::string bytes = WriteScanData(scan);
    CHECK(bytes.size() == 8 + 4 + 4 + 8 + 8 + 4 + scan.sourceName.size() + 4 * 12 + 2 * 12);

    ScanData back;
    REQUIRE(ReadScanData(bytes, back, message) == ScanDataStatus::Ok);
    CHECK(back.sourceName == scan.sourceName);
    REQUIRE(back.mesh.vertices.size() == 4);
    CHECK(back.mesh.vertices[2].x == 10.0);
    CHECK(back.mesh.triangles == scan.mesh.triangles);

    // Newer.
    std::string newer = bytes;
    const uint32_t version = kScanDataVersion + 1;
    std::memcpy(&newer[8], &version, 4);
    CHECK(ReadScanData(newer, back, message) == ScanDataStatus::Newer);

    // Not ours, truncated, a bad vertex number, a count that doesn't match.
    CHECK(ReadScanData("something else entirely", back, message) == ScanDataStatus::NotScanData);
    CHECK(ReadScanData(bytes.substr(0, bytes.size() - 1), back, message) == ScanDataStatus::Damaged);
    std::string badIndex = bytes;
    const uint32_t nine = 9;
    std::memcpy(&badIndex[bytes.size() - 4], &nine, 4);
    CHECK(ReadScanData(badIndex, back, message) == ScanDataStatus::Damaged);
    CHECK(back.mesh.triangles.empty());
    std::string badCount = bytes;
    const uint64_t huge = 1ull << 40;
    std::memcpy(&badCount[16], &huge, 8);
    CHECK(ReadScanData(badCount, back, message) == ScanDataStatus::Damaged);
}

// Skipped unless asked for: set SCANTOALIBRE_PLY to a PLY scan and run
//   ScanCoreTests.exe -tc="real PLY timing*" --no-skip
TEST_CASE("real PLY timing" * doctest::skip())
{
    char* value = nullptr;
    size_t length = 0;
    REQUIRE(_dupenv_s(&value, &length, "SCANTOALIBRE_PLY") == 0);
    REQUIRE(value != nullptr);
    const std::string path(value);
    free(value);

    const auto t0 = std::chrono::steady_clock::now();
    Mesh mesh;
    std::string message;
    ScanFileInfo info;
    const bool read = ReadScanFile(path, mesh, info, message);
    const auto t1 = std::chrono::steady_clock::now();
    ScanData scan{ mesh, path };
    const std::string bytes = WriteScanData(scan);
    ScanData back;
    const ScanDataStatus status = ReadScanData(bytes, back, message);
    const auto t2 = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    MESSAGE(info.description << ": read " << read << " (" << message << "), " << mesh.vertices.size()
                             << " vertices, " << mesh.triangles.size() << " triangles, " << info.degenerate
                             << " degenerate, in " << ms(t0, t1) << " ms; saved " << bytes.size() / 1e6
                             << " MB and read back (" << static_cast<int>(status) << ") in " << ms(t1, t2) << " ms");
}
