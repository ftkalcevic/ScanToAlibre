// Iges.cpp : writes surfaces as IGES 5.3 files.

#include "Iges.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace scancore
{
    namespace
    {
        std::string Hollerith(const std::string& text)
        {
            return std::to_string(text.size()) + "H" + text;
        }

        std::string RightJustify(const std::string& text, size_t width)
        {
            return (text.size() >= width) ? text : std::string(width - text.size(), ' ') + text;
        }

        std::string Sequence(char section, int number)
        {
            return std::string(1, section) + RightJustify(std::to_string(number), 7);
        }

        // Packs tokens into lines of at most width characters, never splitting
        // a token. Each token but the last is followed by a comma, the last by
        // a semicolon.
        std::vector<std::string> Pack(const std::vector<std::string>& tokens, size_t width)
        {
            std::vector<std::string> lines;
            std::string line;
            for (size_t i = 0; i < tokens.size(); ++i)
            {
                const std::string piece = tokens[i] + ((i + 1 < tokens.size()) ? "," : ";");
                if (!line.empty() && line.size() + piece.size() > width)
                {
                    lines.push_back(line);
                    line.clear();
                }
                line += piece;
            }
            if (!line.empty())
                lines.push_back(line);
            return lines;
        }

        std::string Pad(const std::string& text, size_t width)
        {
            return text + std::string(width > text.size() ? width - text.size() : 0, ' ');
        }
    }

    std::string IgesReal(double value)
    {
        if (value == 0.0)
            return "0.0";

        char buffer[48] = {};
        std::snprintf(buffer, sizeof(buffer), "%.15G", value);
        std::string text(buffer);
        if (text.find('.') == std::string::npos)
        {
            const size_t exponent = text.find('E');
            if (exponent == std::string::npos)
                text += ".0";
            else
                text.insert(exponent, ".");
        }
        return text;
    }

    namespace
    {
        // One entity: its type and form, the directory status (blank, subordinate,
        // use, hierarchy), a label, and its parameters (the type first).
        struct Entity
        {
            int type = 0;
            int form = 0;
            std::string status = "00000000";
            std::string label;
            std::vector<std::string> tokens;
        };

        std::vector<std::string> SurfaceTokens(const BSplineSurface& surface)
        {
            std::vector<std::string> p;
            p.push_back("128");
            p.push_back(std::to_string(surface.countU - 1));
            p.push_back(std::to_string(surface.countV - 1));
            p.push_back(std::to_string(surface.degreeU));
            p.push_back(std::to_string(surface.degreeV));
            p.push_back("0");                                           // not closed in U
            p.push_back("0");                                           // not closed in V
            p.push_back(surface.weights.empty() ? "1" : "0");           // polynomial
            p.push_back("0");                                           // not periodic in U
            p.push_back("0");                                           // not periodic in V
            for (double k : surface.knotsU)
                p.push_back(IgesReal(k));
            for (double k : surface.knotsV)
                p.push_back(IgesReal(k));
            const size_t count = surface.points.size();
            for (size_t i = 0; i < count; ++i)
                p.push_back(IgesReal(surface.weights.empty() ? 1.0 : surface.weights[i]));
            for (const Point3& point : surface.points)
            {
                p.push_back(IgesReal(point.x));
                p.push_back(IgesReal(point.y));
                p.push_back(IgesReal(point.z));
            }
            p.push_back(IgesReal(surface.knotsU[surface.degreeU]));
            p.push_back(IgesReal(surface.knotsU[surface.countU]));
            p.push_back(IgesReal(surface.knotsV[surface.degreeV]));
            p.push_back(IgesReal(surface.knotsV[surface.countV]));
            return p;
        }

        // A closed polyline as a degree 1 B-spline curve (entity 126); the first
        // point is repeated at the end. planar: the curve lies in z = 0.
        std::vector<std::string> PolylineTokens(const std::vector<Point3>& loop, bool planar)
        {
            std::vector<Point3> points = loop;
            points.push_back(loop.front());
            const int n = static_cast<int>(points.size());
            std::vector<std::string> p;
            p.push_back("126");
            p.push_back(std::to_string(n - 1));
            p.push_back("1");                                           // degree 1
            p.push_back(planar ? "1" : "0");
            p.push_back("1");                                           // closed
            p.push_back("1");                                           // polynomial
            p.push_back("0");                                           // not periodic
            p.push_back(IgesReal(0.0));
            for (int i = 0; i < n; ++i)
                p.push_back(IgesReal(static_cast<double>(i)));
            p.push_back(IgesReal(static_cast<double>(n - 1)));
            for (int i = 0; i < n; ++i)
                p.push_back(IgesReal(1.0));
            for (const Point3& point : points)
            {
                p.push_back(IgesReal(point.x));
                p.push_back(IgesReal(point.y));
                p.push_back(IgesReal(point.z));
            }
            p.push_back(IgesReal(0.0));
            p.push_back(IgesReal(static_cast<double>(n - 1)));
            p.push_back(IgesReal(0.0));
            p.push_back(IgesReal(0.0));
            p.push_back(IgesReal(planar ? 1.0 : 0.0));
            return p;
        }

        std::string WriteEntities(const std::vector<Entity>& entities, const IgesHeader& header, double largest,
                                  const std::string& description)
        {
            const std::string sender = "ScanToAlibre";

            // Start section.
            std::vector<std::string> start = { description };

            // Global section.
            const std::vector<std::string> globalTokens = {
                "1H,", "1H;",
                Hollerith(sender),                  // product identification from the sender
                Hollerith(header.fileName),
                Hollerith(sender),                  // native system
                Hollerith("1.0"),                   // preprocessor version
                "32", "38", "6", "308", "15",       // integer bits, single and double precision
                Hollerith(sender),                  // product identification for the receiver
                "1.0",                              // model space scale
                "2", Hollerith("MM"),               // units: millimetres
                "1", "1.0",                         // line weight gradations, maximum line width
                Hollerith(header.timestamp),
                "0.0001",                           // minimum resolution
                IgesReal(largest),                  // largest coordinate
                Hollerith(header.author),
                Hollerith(header.organisation),
                "11",                               // IGES 5.3
                "0",                                // no drafting standard
                Hollerith(header.timestamp),
            };
            const std::vector<std::string> global = Pack(globalTokens, 72);

            // Parameter data, and each entity's directory entry: two lines of
            // nine 8-column fields. Entity k's directory entry starts at line 2k + 1.
            auto field = [](const std::string& text) { return RightJustify(text, 8); };
            std::vector<std::string> directory;
            std::vector<std::string> parameterLines;
            for (size_t k = 0; k < entities.size(); ++k)
            {
                const Entity& e = entities[k];
                const int directorySequence = static_cast<int>(2 * k + 1);
                const int parameterStart = static_cast<int>(parameterLines.size()) + 1;
                const std::vector<std::string> lines = Pack(e.tokens, 64);
                for (const std::string& line : lines)
                    parameterLines.push_back(Pad(line, 64) + " " + RightJustify(std::to_string(directorySequence), 7));
                const std::string type = std::to_string(e.type);
                directory.push_back(field(type) + field(std::to_string(parameterStart)) + field("0") + field("0") +
                                    field("0") + field("0") + field("0") + field("0") + e.status);
                directory.push_back(field(type) + field("0") + field("0") + field(std::to_string(lines.size())) +
                                    field(std::to_string(e.form)) + field("") + field("") + field(e.label) +
                                    field("0"));
            }

            std::string out;
            int sequence = 0;
            for (const std::string& line : start)
                out += Pad(line, 72) + Sequence('S', ++sequence) + "\r\n";
            const int startCount = sequence;

            sequence = 0;
            for (const std::string& line : global)
                out += Pad(line, 72) + Sequence('G', ++sequence) + "\r\n";
            const int globalCount = sequence;

            sequence = 0;
            for (const std::string& line : directory)
                out += line + Sequence('D', ++sequence) + "\r\n";
            const int directoryCount = sequence;

            sequence = 0;
            for (const std::string& line : parameterLines)
                out += line + Sequence('P', ++sequence) + "\r\n";
            const int parameterCount = sequence;

            const std::string terminate = Sequence('S', startCount) + Sequence('G', globalCount) +
                                          Sequence('D', directoryCount) + Sequence('P', parameterCount);
            out += Pad(terminate, 72) + Sequence('T', 1) + "\r\n";
            return out;
        }

        double Largest(const std::vector<Point3>& points, double largest)
        {
            for (const Point3& p : points)
                largest = std::max({ largest, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z) });
            return largest;
        }
    }

    std::string WriteIgesSurface(const BSplineSurface& surface, const IgesHeader& header)
    {
        Entity e;
        e.type = 128;
        e.label = "SURFACE";
        e.tokens = SurfaceTokens(surface);
        return WriteEntities({ e }, header, Largest(surface.points, 0.0), "B-spline surface written by ScanToAlibre.");
    }

    std::string WriteIgesTrimmedSurface(const BSplineSurface& surface,
                                        const std::vector<std::array<double, 2>>& outlineUV,
                                        const std::vector<Point3>& outline, const IgesHeader& header)
    {
        // Directory entries 1, 3, 5, 7 and 9: the surface, the outline in its
        // parameters, the outline in space, the curve on the surface, and the
        // trimmed surface.
        Entity base;
        base.type = 128;
        base.status = "00010000";       // physically dependent: part of the trimmed surface
        base.label = "SURFACE";
        base.tokens = SurfaceTokens(surface);

        std::vector<Point3> uv;
        for (const auto& p : outlineUV)
            uv.push_back({ p[0], p[1], 0.0 });
        Entity parameterCurve;
        parameterCurve.type = 126;
        parameterCurve.status = "00010500";     // dependent, in parameter space
        parameterCurve.label = "TRIMUV";
        parameterCurve.tokens = PolylineTokens(uv, true);

        Entity modelCurve;
        modelCurve.type = 126;
        modelCurve.status = "00010000";
        modelCurve.label = "TRIM";
        modelCurve.tokens = PolylineTokens(outline, false);

        Entity onSurface;
        onSurface.type = 142;
        onSurface.status = "00010000";
        onSurface.label = "BOUNDARY";
        onSurface.tokens = { "142", "0", "1", "3", "5", "1" };     // the parameter curve preferred

        Entity trimmed;
        trimmed.type = 144;
        trimmed.label = "TRIMSURF";
        trimmed.tokens = { "144", "1", "1", "0", "7" };            // an outer boundary, no holes

        return WriteEntities({ base, parameterCurve, modelCurve, onSurface, trimmed }, header,
                             Largest(outline, Largest(surface.points, 0.0)),
                             "Trimmed B-spline surface written by ScanToAlibre.");
    }
}
