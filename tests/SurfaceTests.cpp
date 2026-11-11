// SurfaceTests.cpp : B-spline surfaces and the IGES writer.

#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "BSplineSurface.h"
#include "Iges.h"

using namespace scancore;

namespace
{
    std::vector<std::string> Lines(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
        }
        return lines;
    }

    // The parameter data of the file's one entity, split at the commas.
    std::vector<std::string> ParameterTokens(const std::vector<std::string>& lines)
    {
        std::string data;
        for (const std::string& line : lines)
            if (line[72] == 'P')
                data += line.substr(0, 64);

        std::vector<std::string> tokens;
        std::string token;
        for (char c : data)
        {
            if (c == ' ')
                continue;
            if (c == ',' || c == ';')
            {
                tokens.push_back(token);
                token.clear();
                if (c == ';')
                    break;
            }
            else
            {
                token += c;
            }
        }
        return tokens;
    }
}

TEST_CASE("the test hump surface is valid and has the expected shape")
{
    const BSplineSurface surface = TestHumpSurface();
    std::string message;
    REQUIRE(ValidateSurface(surface, message));

    const Point3 corner = Evaluate(surface, 0.0, 0.0);
    CHECK(corner.x == doctest::Approx(0.0));
    CHECK(corner.y == doctest::Approx(0.0));
    CHECK(corner.z == doctest::Approx(20.0));

    const Point3 far = Evaluate(surface, 1.0, 1.0);
    CHECK(far.x == doctest::Approx(100.0));
    CHECK(far.y == doctest::Approx(80.0));
    CHECK(far.z == doctest::Approx(21.5));

    const Point3 middle = Evaluate(surface, 0.5, 0.5);
    CHECK(middle.z > 26.0);
    CHECK(middle.z < 34.0);
}

TEST_CASE("surfaces with the wrong number of knots, points or weights are refused")
{
    std::string message;

    BSplineSurface surface = TestHumpSurface();
    surface.knotsU.pop_back();
    CHECK_FALSE(ValidateSurface(surface, message));

    surface = TestHumpSurface();
    surface.points.pop_back();
    CHECK_FALSE(ValidateSurface(surface, message));

    surface = TestHumpSurface();
    surface.weights.assign(surface.points.size() - 1, 1.0);
    CHECK_FALSE(ValidateSurface(surface, message));

    surface = TestHumpSurface();
    surface.weights.assign(surface.points.size(), 1.0);
    surface.weights[3] = 0.0;
    CHECK_FALSE(ValidateSurface(surface, message));
}

TEST_CASE("IGES reals always have a decimal point")
{
    CHECK(IgesReal(0.0) == "0.0");
    CHECK(IgesReal(1.0) == "1.0");
    CHECK(IgesReal(-25.0) == "-25.0");
    CHECK(IgesReal(0.25) == "0.25");
    CHECK(IgesReal(1e-5) == "1.E-05");
    CHECK(std::strtod(IgesReal(1.0 / 3.0).c_str(), nullptr) == doctest::Approx(1.0 / 3.0).epsilon(1e-14));
}

TEST_CASE("an IGES surface file has 80-column lines in its sections, and its data reads back")
{
    const BSplineSurface surface = TestHumpSurface();
    IgesHeader header;
    header.fileName = "test-surface.igs";
    header.timestamp = "20261111.120000";
    const std::vector<std::string> lines = Lines(WriteIgesSurface(surface, header));
    REQUIRE(lines.size() > 5);

    // Every line is 80 columns, the sections come in order S G D P T, and each
    // section's sequence numbers count up from 1.
    const std::string order = "SGDPT";
    size_t section = 0;
    int expected = 1;
    int counts[5] = {};
    for (const std::string& line : lines)
    {
        REQUIRE(line.size() == 80);
        const char letter = line[72];
        const size_t at = order.find(letter);
        REQUIRE(at != std::string::npos);
        REQUIRE(at >= section);
        if (at != section)
        {
            section = at;
            expected = 1;
        }
        CHECK(std::atoi(line.substr(73).c_str()) == expected);
        ++expected;
        ++counts[at];
    }
    CHECK(counts[0] >= 1);
    CHECK(counts[2] == 2);
    CHECK(counts[4] == 1);

    // The terminate line counts each section.
    const std::string& last = lines.back();
    CHECK(std::atoi(last.substr(1, 7).c_str()) == counts[0]);
    CHECK(std::atoi(last.substr(9, 7).c_str()) == counts[1]);
    CHECK(std::atoi(last.substr(17, 7).c_str()) == counts[2]);
    CHECK(std::atoi(last.substr(25, 7).c_str()) == counts[3]);

    // The directory entry is a 128, points at parameter line 1 and gives the
    // parameter line count; every parameter line points back at directory line 1.
    const std::string d1 = lines[counts[0] + counts[1]];
    const std::string d2 = lines[counts[0] + counts[1] + 1];
    CHECK(std::atoi(d1.substr(0, 8).c_str()) == 128);
    CHECK(std::atoi(d1.substr(8, 8).c_str()) == 1);
    CHECK(std::atoi(d2.substr(24, 8).c_str()) == counts[3]);
    for (const std::string& line : lines)
        if (line[72] == 'P')
            CHECK(std::atoi(line.substr(64, 8).c_str()) == 1);

    // The parameters read back as the surface.
    const std::vector<std::string> tokens = ParameterTokens(lines);
    const size_t knotsU = surface.knotsU.size();
    const size_t knotsV = surface.knotsV.size();
    const size_t points = surface.points.size();
    REQUIRE(tokens.size() == 10 + knotsU + knotsV + points + 3 * points + 4);
    CHECK(tokens[0] == "128");
    CHECK(std::atoi(tokens[1].c_str()) == surface.countU - 1);
    CHECK(std::atoi(tokens[2].c_str()) == surface.countV - 1);
    CHECK(std::atoi(tokens[3].c_str()) == surface.degreeU);
    CHECK(std::atoi(tokens[4].c_str()) == surface.degreeV);
    CHECK(tokens[7] == "1");

    size_t at = 10;
    for (size_t i = 0; i < knotsU; ++i)
        CHECK(std::strtod(tokens[at++].c_str(), nullptr) == doctest::Approx(surface.knotsU[i]));
    for (size_t i = 0; i < knotsV; ++i)
        CHECK(std::strtod(tokens[at++].c_str(), nullptr) == doctest::Approx(surface.knotsV[i]));
    at += points;   // weights
    for (size_t i = 0; i < points; ++i)
    {
        CHECK(std::strtod(tokens[at++].c_str(), nullptr) == doctest::Approx(surface.points[i].x));
        CHECK(std::strtod(tokens[at++].c_str(), nullptr) == doctest::Approx(surface.points[i].y));
        CHECK(std::strtod(tokens[at++].c_str(), nullptr) == doctest::Approx(surface.points[i].z));
    }
    CHECK(std::strtod(tokens[at++].c_str(), nullptr) == doctest::Approx(0.0));
    CHECK(std::strtod(tokens[at++].c_str(), nullptr) == doctest::Approx(1.0));
    CHECK(std::strtod(tokens[at++].c_str(), nullptr) == doctest::Approx(0.0));
    CHECK(std::strtod(tokens[at++].c_str(), nullptr) == doctest::Approx(1.0));
}
