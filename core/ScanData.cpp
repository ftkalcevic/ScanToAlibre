// ScanData.cpp : the scan as it's saved inside the part.

#include "ScanData.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace scancore
{
    namespace
    {
        const char kMagic[8] = { 'S', 'T', 'A', 'S', 'C', 'A', 'N', '\0' };

        // No real scan comes near this many fitted features; more means damage.
        const uint32_t kMaxFeatures = 100000;

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

        std::string WriteMesh(const ScanData& scan, uint32_t version)
        {
            std::string out;
            out.reserve(64 + scan.sourceName.size() + scan.mesh.vertices.size() * 12 +
                        scan.mesh.triangles.size() * 12 + scan.features.size() * 96);
            out.append(kMagic, sizeof(kMagic));
            Put<uint32_t>(out, version);
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

        ScanDataStatus Damaged(ScanData& scan, std::string& message, const char* why)
        {
            scan = ScanData();
            message = std::string("The part's scan is damaged: ") + why;
            return ScanDataStatus::Damaged;
        }
    }

    std::string WriteScanData(const ScanData& scan)
    {
        std::string out = WriteMesh(scan, kScanDataVersion);
        Put<uint32_t>(out, static_cast<uint32_t>(scan.features.size()));
        for (const Feature& f : scan.features)
        {
            const uint8_t kind = (f.kind == Feature::Kind::Plane) ? 0 : (f.kind == Feature::Kind::Axis) ? 1 : 2;
            Put<uint8_t>(out, kind);
            for (double v : { f.point.x, f.point.y, f.point.z, f.direction.x, f.direction.y, f.direction.z, f.halfWidth })
                Put<double>(out, v);
            Put<uint32_t>(out, static_cast<uint32_t>(f.name.size()));
            out += f.name;
        }
        return out;
    }

    std::string WriteScanDataVersion1(const ScanData& scan)
    {
        return WriteMesh(scan, 1);
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
            return Damaged(scan, message, "it ends early.");
        if (version > kScanDataVersion)
        {
            message = "The part's scan was saved by a newer ScanToAlibre (format " + std::to_string(version) +
                      "); update ScanToAlibre to use it.";
            return ScanDataStatus::Newer;
        }
        if (version < 1)
            return Damaged(scan, message, "its format version is 0.");
        if (!Get(bytes, at, flags) || !Get(bytes, at, vertices) || !Get(bytes, at, triangles) ||
            !Get(bytes, at, nameLength) || at + nameLength > bytes.size())
            return Damaged(scan, message, "it ends early.");
        scan.sourceName.assign(bytes.data() + at, nameLength);
        at += nameLength;

        // The counts must match the size (exactly for version 1, at least for
        // version 2, whose features follow), which also stops a huge count from
        // a damaged header asking for too much memory.
        const uint64_t meshEnd = static_cast<uint64_t>(at) + vertices * 12ull + triangles * 12ull;
        const bool sizeFits = (version == 1) ? meshEnd == bytes.size() : meshEnd + 4 <= bytes.size();
        if (vertices > (1ull << 32) || triangles > (1ull << 32) || !sizeFits)
            return Damaged(scan, message, "its size doesn't match its counts.");

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
                    return Damaged(scan, message, "a triangle refers to a vertex that doesn't exist.");
                t[k] = static_cast<int>(c[k]);
            }
        }
        if (version == 1)
            return ScanDataStatus::Ok;

        uint32_t count = 0;
        if (!Get(bytes, at, count) || count > kMaxFeatures)
            return Damaged(scan, message, "its fitted planes and axes can't be read.");
        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t kind = 0;
            double v[7];
            uint32_t length = 0;
            if (!Get(bytes, at, kind) || kind > 2)
                return Damaged(scan, message, "a fitted plane or axis has an unknown kind.");
            for (double& value : v)
                if (!Get(bytes, at, value) || !std::isfinite(value))
                    return Damaged(scan, message, "a fitted plane or axis isn't a number.");
            if (!Get(bytes, at, length) || at + length > bytes.size())
                return Damaged(scan, message, "it ends early, in its fitted planes and axes.");
            Feature f;
            f.kind = (kind == 0) ? Feature::Kind::Plane : (kind == 1) ? Feature::Kind::Axis : Feature::Kind::Point;
            f.point = { v[0], v[1], v[2] };
            f.direction = { v[3], v[4], v[5] };
            f.halfWidth = v[6];
            f.name.assign(bytes.data() + at, length);
            at += length;
            scan.features.push_back(f);
        }
        if (at != bytes.size())
            return Damaged(scan, message, "it has more after its fitted planes and axes than expected.");
        return ScanDataStatus::Ok;
    }
}
