// ScanData.cpp : the scan as it's saved inside the part.

#include "ScanData.h"

#include <cstdint>
#include <cstring>

namespace scancore
{
    namespace
    {
        const char kMagic[8] = { 'S', 'T', 'A', 'S', 'C', 'A', 'N', '\0' };

        template <class T>
        void Put(std::string& out, T value)
        {
            out.append(reinterpret_cast<const char*>(&value), sizeof(T));
        }

        template <class T>
        bool Get(const std::string& bytes, size_t& at, T& value)
        {
            if (at + sizeof(T) > bytes.size())
                return false;
            std::memcpy(&value, bytes.data() + at, sizeof(T));
            at += sizeof(T);
            return true;
        }
    }

    std::string WriteScanData(const ScanData& scan)
    {
        std::string out;
        out.reserve(64 + scan.sourceName.size() + scan.mesh.vertices.size() * 12 + scan.mesh.triangles.size() * 12);
        out.append(kMagic, sizeof(kMagic));
        Put<uint32_t>(out, kScanDataVersion);
        Put<uint32_t>(out, 0);
        Put<uint64_t>(out, scan.mesh.vertices.size());
        Put<uint64_t>(out, scan.mesh.triangles.size());
        Put<uint32_t>(out, static_cast<uint32_t>(scan.sourceName.size()));
        out += scan.sourceName;
        for (const Point3& p : scan.mesh.vertices)
        {
            Put<float>(out, static_cast<float>(p.x));
            Put<float>(out, static_cast<float>(p.y));
            Put<float>(out, static_cast<float>(p.z));
        }
        for (const auto& t : scan.mesh.triangles)
        {
            for (int corner : t)
                Put<uint32_t>(out, static_cast<uint32_t>(corner));
        }
        return out;
    }

    ScanDataStatus ReadScanData(const std::string& bytes, ScanData& scan, std::string& message)
    {
        scan = ScanData();
        if (bytes.size() < sizeof(kMagic) || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0)
        {
            message = "The part's ScanToAlibre data isn't a scan.";
            return ScanDataStatus::NotScanData;
        }

        size_t at = sizeof(kMagic);
        uint32_t version = 0, flags = 0, nameLength = 0;
        uint64_t vertices = 0, triangles = 0;
        if (!Get(bytes, at, version))
        {
            message = "The part's scan is damaged: it ends early.";
            return ScanDataStatus::Damaged;
        }
        if (version > kScanDataVersion)
        {
            message = "The part's scan was saved by a newer ScanToAlibre (format " + std::to_string(version) +
                      "); update ScanToAlibre to use it.";
            return ScanDataStatus::Newer;
        }
        if (!Get(bytes, at, flags) || !Get(bytes, at, vertices) || !Get(bytes, at, triangles) ||
            !Get(bytes, at, nameLength) || at + nameLength > bytes.size())
        {
            message = "The part's scan is damaged: it ends early.";
            return ScanDataStatus::Damaged;
        }
        scan.sourceName.assign(bytes.data() + at, nameLength);
        at += nameLength;

        // The counts must match the size exactly, which also stops a huge count
        // from a damaged header asking for too much memory.
        const uint64_t expected = static_cast<uint64_t>(at) + vertices * 12ull + triangles * 12ull;
        if (vertices > (1ull << 32) || triangles > (1ull << 32) || expected != bytes.size())
        {
            message = "The part's scan is damaged: its size doesn't match its counts.";
            return ScanDataStatus::Damaged;
        }

        scan.mesh.vertices.resize(static_cast<size_t>(vertices));
        for (Point3& p : scan.mesh.vertices)
        {
            float v[3];
            std::memcpy(v, bytes.data() + at, sizeof(v));
            at += sizeof(v);
            p = { v[0], v[1], v[2] };
        }
        scan.mesh.triangles.resize(static_cast<size_t>(triangles));
        for (auto& t : scan.mesh.triangles)
        {
            uint32_t c[3];
            std::memcpy(c, bytes.data() + at, sizeof(c));
            at += sizeof(c);
            for (int k = 0; k < 3; ++k)
            {
                if (c[k] >= vertices)
                {
                    scan = ScanData();
                    message = "The part's scan is damaged: a triangle refers to a vertex that doesn't exist.";
                    return ScanDataStatus::Damaged;
                }
                t[k] = static_cast<int>(c[k]);
            }
        }
        return ScanDataStatus::Ok;
    }
}
