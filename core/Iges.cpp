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

    std::string WriteIgesSurface(const BSplineSurface& surface, const IgesHeader& header)
    {
        const std::string sender = "ScanToAlibre";

        // Start section.
        std::vector<std::string> start = { "B-spline surface written by ScanToAlibre." };

        // Global section.
        double largest = 0.0;
        for (const Point3& p : surface.points)
            largest = std::max({ largest, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z) });

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

        // Parameter data for entity 128.
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
        const std::vector<std::string> parameters = Pack(p, 64);

        // Directory entry: two lines of nine 8-column fields.
        const int directorySequence = 1;
        const int parameterStart = 1;
        auto field = [](const std::string& text) { return RightJustify(text, 8); };
        const std::string directory1 = field("128") + field(std::to_string(parameterStart)) + field("0") +
                                       field("0") + field("0") + field("0") + field("0") + field("0") +
                                       "00000000";
        const std::string directory2 = field("128") + field("0") + field("0") +
                                       field(std::to_string(parameters.size())) + field("0") + field("") +
                                       field("") + field("SURFACE") + field("0");

        std::string out;
        int sequence = 0;
        for (const std::string& line : start)
            out += Pad(line, 72) + Sequence('S', ++sequence) + "\r\n";
        const int startCount = sequence;

        sequence = 0;
        for (const std::string& line : global)
            out += Pad(line, 72) + Sequence('G', ++sequence) + "\r\n";
        const int globalCount = sequence;

        out += directory1 + Sequence('D', directorySequence) + "\r\n";
        out += directory2 + Sequence('D', directorySequence + 1) + "\r\n";
        const int directoryCount = 2;

        sequence = 0;
        for (const std::string& line : parameters)
            out += Pad(line, 64) + " " + RightJustify(std::to_string(directorySequence), 7) +
                   Sequence('P', ++sequence) + "\r\n";
        const int parameterCount = sequence;

        const std::string terminate = Sequence('S', startCount) + Sequence('G', globalCount) +
                                      Sequence('D', directoryCount) + Sequence('P', parameterCount);
        out += Pad(terminate, 72) + Sequence('T', 1) + "\r\n";
        return out;
    }
}
