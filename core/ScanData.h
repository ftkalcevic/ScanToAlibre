// ScanData.h : the scan as it's saved inside the part, through Alibre's add-on
// data stream.
//
// Binary, little endian:
//   8 bytes   "STASCAN" and a zero
//   uint32    format version (kScanDataVersion)
//   uint32    flags, 0 for now
//   uint64    vertex count, uint64 triangle count
//   uint32    source name length, then the name in UTF-8 (the file it was loaded from)
//   float32   x, y, z for each vertex, in mm
//   uint32    three vertex numbers for each triangle
// From version 2, the planes and axes fitted in Orient:
//   uint32    feature count, then for each:
//     uint8     kind: 0 plane, 1 axis, 2 point
//     float64   point x, y, z (mm), direction x, y, z (unit), half width (mm, for drawing)
//     uint32    name length, then the name in UTF-8
//     uint8     flags (version 3 on): 1 a plane of symmetry
// Version 1 reads with no features. In version 2 a plane named "Symmetry
// plane..." or "Mid plane..." is a plane of symmetry, as names were then fixed. A newer version is refused, so it's never
// overwritten by an older add-on.

#pragma once

#include <string>
#include <vector>

#include "Mesh.h"
#include "Orient.h"

namespace scancore
{
    const unsigned kScanDataVersion = 3;

    struct ScanData
    {
        Mesh mesh;
        std::string sourceName;             // UTF-8
        std::vector<Feature> features;      // fitted planes and axes
    };

    std::string WriteScanData(const ScanData& scan);

    enum class ScanDataStatus
    {
        Ok,
        NotScanData,
        Newer,      // from a newer ScanToAlibre
        Damaged,
    };

    ScanDataStatus ReadScanData(const std::string& bytes, ScanData& scan, std::string& message);

    // Version 1 and 2 exactly, for testing that they still read.
    std::string WriteScanDataVersion1(const ScanData& scan);
    std::string WriteScanDataVersion2(const ScanData& scan);
}
