// Ply.cpp : reads PLY files.

#include "Ply.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace scancore
{
    namespace
    {
        enum class Type { Int8, UInt8, Int16, UInt16, Int32, UInt32, Float32, Float64, Unknown };

        Type TypeOf(const std::string& name)
        {
            if (name == "char" || name == "int8") return Type::Int8;
            if (name == "uchar" || name == "uint8") return Type::UInt8;
            if (name == "short" || name == "int16") return Type::Int16;
            if (name == "ushort" || name == "uint16") return Type::UInt16;
            if (name == "int" || name == "int32") return Type::Int32;
            if (name == "uint" || name == "uint32") return Type::UInt32;
            if (name == "float" || name == "float32") return Type::Float32;
            if (name == "double" || name == "float64") return Type::Float64;
            return Type::Unknown;
        }

        size_t SizeOf(Type type)
        {
            switch (type)
            {
            case Type::Int8: case Type::UInt8: return 1;
            case Type::Int16: case Type::UInt16: return 2;
            case Type::Int32: case Type::UInt32: case Type::Float32: return 4;
            case Type::Float64: return 8;
            default: return 0;
            }
        }

        struct Property
        {
            std::string name;
            bool list = false;
            Type countType = Type::Unknown;     // lists only
            Type type = Type::Unknown;          // the value's, or a list item's
        };

        struct Element
        {
            std::string name;
            size_t count = 0;
            std::vector<Property> properties;
        };

        // Reads values from the body, in whichever format.
        class Reader
        {
        public:
            Reader(const std::string& bytes, size_t start, int format)
                : m_bytes(bytes), m_at(start), m_format(format)
            {
                if (m_format == 0)
                    m_text.str(bytes.substr(start));
            }

            bool Read(Type type, double& value)
            {
                if (m_format == 0)
                    return static_cast<bool>(m_text >> value);

                const size_t size = SizeOf(type);
                if (size == 0 || m_at + size > m_bytes.size())
                    return false;
                unsigned char raw[8];
                std::memcpy(raw, m_bytes.data() + m_at, size);
                m_at += size;
                if (m_format == 2)
                {
                    for (size_t i = 0; i < size / 2; ++i)
                        std::swap(raw[i], raw[size - 1 - i]);
                }
                switch (type)
                {
                case Type::Int8: { int8_t v; std::memcpy(&v, raw, 1); value = v; break; }
                case Type::UInt8: { uint8_t v; std::memcpy(&v, raw, 1); value = v; break; }
                case Type::Int16: { int16_t v; std::memcpy(&v, raw, 2); value = v; break; }
                case Type::UInt16: { uint16_t v; std::memcpy(&v, raw, 2); value = v; break; }
                case Type::Int32: { int32_t v; std::memcpy(&v, raw, 4); value = v; break; }
                case Type::UInt32: { uint32_t v; std::memcpy(&v, raw, 4); value = v; break; }
                case Type::Float32: { float v; std::memcpy(&v, raw, 4); value = v; break; }
                case Type::Float64: { double v; std::memcpy(&v, raw, 8); value = v; break; }
                default: return false;
                }
                return true;
            }

        private:
            const std::string& m_bytes;
            size_t m_at;
            int m_format;           // 0 ASCII, 1 binary little endian, 2 binary big endian
            std::istringstream m_text;
        };
    }

    bool ReadFileBytes(const std::filesystem::path& path, std::string& bytes, std::string& message)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            message = "Could not open " + path.u8string() + ".";
            return false;
        }
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        file.seekg(0, std::ios::beg);
        bytes.assign(static_cast<size_t>(size), '\0');
        if (size > 0 && !file.read(&bytes[0], size))
        {
            message = "Could not read " + path.u8string() + ".";
            return false;
        }
        return true;
    }

    bool ParsePly(const std::string& bytes, Mesh& mesh, std::string& message, PlyReadInfo* pInfo)
    {
        mesh = Mesh();
        PlyReadInfo info;

        // Header: lines up to end_header.
        if (bytes.compare(0, 3, "ply") != 0)
        {
            message = "The file isn't a PLY file: it doesn't start with \"ply\".";
            return false;
        }
        const size_t endHeader = bytes.find("end_header");
        if (endHeader == std::string::npos)
        {
            message = "The PLY file's header has no end_header.";
            return false;
        }
        size_t bodyStart = bytes.find('\n', endHeader);
        bodyStart = (bodyStart == std::string::npos) ? bytes.size() : bodyStart + 1;

        std::istringstream header(bytes.substr(0, endHeader));
        std::string line;
        std::vector<Element> elements;
        int format = -1;
        while (std::getline(header, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            std::istringstream words(line);
            std::string keyword;
            words >> keyword;
            if (keyword == "format")
            {
                words >> info.format;
                if (info.format == "ascii") format = 0;
                else if (info.format == "binary_little_endian") format = 1;
                else if (info.format == "binary_big_endian") format = 2;
            }
            else if (keyword == "element")
            {
                Element element;
                words >> element.name >> element.count;
                elements.push_back(element);
            }
            else if (keyword == "property")
            {
                if (elements.empty())
                {
                    message = "The PLY file's header has a property before any element.";
                    return false;
                }
                Property property;
                std::string first;
                words >> first;
                if (first == "list")
                {
                    std::string countType, itemType;
                    words >> countType >> itemType >> property.name;
                    property.list = true;
                    property.countType = TypeOf(countType);
                    property.type = TypeOf(itemType);
                    if (property.countType == Type::Unknown || property.type == Type::Unknown)
                    {
                        message = "The PLY file has a list property of an unknown type: " + line;
                        return false;
                    }
                }
                else
                {
                    property.type = TypeOf(first);
                    words >> property.name;
                    if (property.type == Type::Unknown)
                    {
                        message = "The PLY file has a property of an unknown type: " + line;
                        return false;
                    }
                }
                elements.back().properties.push_back(property);
            }
        }
        if (format < 0)
        {
            message = "The PLY file's format isn't ascii, binary_little_endian or binary_big_endian.";
            return false;
        }

        Reader reader(bytes, bodyStart, format);
        bool sawVertices = false;
        for (const Element& element : elements)
        {
            const bool isVertex = (element.name == "vertex");
            const bool isFace = (element.name == "face");
            int ix = -1, iy = -1, iz = -1, indices = -1;
            int normals = 0;
            for (size_t p = 0; p < element.properties.size(); ++p)
            {
                const std::string& name = element.properties[p].name;
                if (name == "x") ix = static_cast<int>(p);
                else if (name == "y") iy = static_cast<int>(p);
                else if (name == "z") iz = static_cast<int>(p);
                else if (name == "nx" || name == "ny" || name == "nz") ++normals;
                else if ((name == "vertex_indices" || name == "vertex_index") && element.properties[p].list)
                    indices = static_cast<int>(p);
            }
            if (isVertex)
            {
                if (ix < 0 || iy < 0 || iz < 0)
                {
                    message = "The PLY file's vertices have no x, y and z.";
                    return false;
                }
                sawVertices = true;
                info.vertices = element.count;
                info.hasNormals = (normals == 3);
                mesh.vertices.reserve(element.count);
            }
            if (isFace)
            {
                if (!sawVertices)
                {
                    message = "The PLY file's faces come before its vertices.";
                    return false;
                }
                info.faces = element.count;
                mesh.triangles.reserve(element.count);
            }

            std::vector<double> values(element.properties.size());
            std::vector<int> polygon;
            for (size_t i = 0; i < element.count; ++i)
            {
                polygon.clear();
                for (size_t p = 0; p < element.properties.size(); ++p)
                {
                    const Property& property = element.properties[p];
                    if (!property.list)
                    {
                        if (!reader.Read(property.type, values[p]))
                        {
                            message = "The PLY file ends early, in its " + element.name + " data.";
                            return false;
                        }
                        continue;
                    }
                    double countValue = 0.0;
                    if (!reader.Read(property.countType, countValue) || countValue < 0.0)
                    {
                        message = "The PLY file ends early, in its " + element.name + " data.";
                        return false;
                    }
                    const size_t count = static_cast<size_t>(countValue);
                    for (size_t k = 0; k < count; ++k)
                    {
                        double item = 0.0;
                        if (!reader.Read(property.type, item))
                        {
                            message = "The PLY file ends early, in its " + element.name + " data.";
                            return false;
                        }
                        if (isFace && static_cast<int>(p) == indices)
                            polygon.push_back(static_cast<int>(item));
                    }
                }

                if (isVertex)
                {
                    mesh.vertices.push_back({ values[ix], values[iy], values[iz] });
                }
                else if (isFace)
                {
                    for (int corner : polygon)
                    {
                        if (corner < 0 || static_cast<size_t>(corner) >= mesh.vertices.size())
                        {
                            message = "A face in the PLY file refers to a vertex that doesn't exist.";
                            return false;
                        }
                    }
                    for (size_t k = 2; k < polygon.size(); ++k)
                    {
                        const std::array<int, 3> t{ polygon[0], polygon[k - 1], polygon[k] };
                        if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2])
                            ++info.degenerate;
                        else
                            mesh.triangles.push_back(t);
                    }
                }
            }
        }

        if (!sawVertices || mesh.vertices.empty())
        {
            message = "The PLY file has no vertices.";
            return false;
        }
        if (pInfo != nullptr)
            *pInfo = info;
        return true;
    }

    bool ReadPly(const std::filesystem::path& path, Mesh& mesh, std::string& message, PlyReadInfo* info)
    {
        std::string bytes;
        if (!ReadFileBytes(path, bytes, message))
            return false;
        return ParsePly(bytes, mesh, message, info);
    }
}
