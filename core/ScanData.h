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
// A newer version is refused, so it's never overwritten by an older add-on.

#pragma once

#include <string>

#include "Mesh.h"

namespace scancore
{
    const unsigned kScanDataVersion = 1;

    struct ScanData
    {
        Mesh mesh;
        std::string sourceName;     // UTF-8
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
}
