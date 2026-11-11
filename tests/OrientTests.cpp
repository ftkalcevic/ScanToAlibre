// OrientTests.cpp : transforms, the fitted box, flat areas and 3-2-1 alignment.

#include <doctest/doctest.h>

#include <cmath>
#include <map>
#include <tuple>

#include "Orient.h"

using namespace scancore;

namespace
{
    const double kPi = 3.14159265358979323846;

    // A closed box from -half to +half along each axis, each face split n by n.
    Mesh BoxMesh(double hx, double hy, double hz, int n)
    {
        Mesh mesh;
        std::map<std::tuple<int, int, int>, int> index;
        auto vertex = [&](int x, int y, int z) {
            const auto key = std::make_tuple(x, y, z);
            const auto found = index.find(key);
            if (found != index.end())
                return found->second;
            const int i = static_cast<int>(mesh.vertices.size());
            mesh.vertices.push_back({ hx * (2.0 * x / n - 1.0), hy * (2.0 * y / n - 1.0), hz * (2.0 * z / n - 1.0) });
            index[key] = i;
            return i;
        };
        struct Face { int o[3]; int u[3]; int v[3]; };
        const Face faces[6] = {
            { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 } }, { { 0, 0, n }, { 1, 0, 0 }, { 0, 1, 0 } },
            { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 } }, { { 0, n, 0 }, { 0, 0, 1 }, { 1, 0, 0 } },
            { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } }, { { n, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
        };
        for (const Face& f : faces)
            for (int a = 0; a < n; ++a)
                for (int b = 0; b < n; ++b)
                {
                    auto corner = [&](int da, int db) {
                        return vertex(f.o[0] + (a + da) * f.u[0] + (b + db) * f.v[0],
                                      f.o[1] + (a + da) * f.u[1] + (b + db) * f.v[1],
                                      f.o[2] + (a + da) * f.u[2] + (b + db) * f.v[2]);
                    };
                    const int p00 = corner(0, 0), p10 = corner(1, 0), p11 = corner(1, 1), p01 = corner(0, 1);
                    mesh.triangles.push_back({ p00, p10, p11 });
                    mesh.triangles.push_back({ p00, p11, p01 });
                }
        return mesh;
    }

    // A turn and move that leaves nothing lined up.
    Transform Skew()
    {
        return Transform::Rotation({ 1, 0, 0 }, 0.4, {})
            .Then(Transform::Rotation({ 0, 1, 0 }, -0.7, {}))
            .Then(Transform::Rotation({ 0, 0, 1 }, 1.1, {}))
            .Then(Transform::Translation({ 17.0, -42.0, 230.0 }));
    }

    bool Parallel(const Point3& a, const Point3& b)
    {
        return std::fabs(std::fabs(Dot(Normalised(a), Normalised(b))) - 1.0) < 1e-9;
    }

    Feature Plane(const Point3& point, const Point3& normal)
    {
        Feature f;
        f.kind = Feature::Kind::Plane;
        f.point = point;
        f.direction = normal;
        return f;
    }

    const Feature& Named(const std::vector<Feature>& features, const std::string& name)
    {
        for (const Feature& f : features)
            if (f.name == name)
                return f;
        FAIL("no feature " << name);
        return features.front();
    }
}

TEST_CASE("transforms turn, move and scale points, and compose in order")
{
    const Transform turn = Transform::Rotation({ 0, 0, 1 }, kPi / 2.0, { 1, 0, 0 });
    const Point3 p = turn.Apply({ 2, 0, 0 });
    CHECK(p.x == doctest::Approx(1.0));
    CHECK(p.y == doctest::Approx(1.0));

    const Transform both = Transform::Translation({ 5, 0, 0 }).Then(Transform::Scaling(2.0, {}));
    const Point3 q = both.Apply({ 1, 1, 1 });
    CHECK(q.x == doctest::Approx(12.0));
    CHECK(q.y == doctest::Approx(2.0));

    const Transform half = Transform::RotationBetween({ 1, 0, 0 }, { -1, 0, 0 }, {});
    const Point3 r = half.Apply({ 3, 0, 0 });
    CHECK(r.x == doctest::Approx(-3.0));
    const Transform tilt = Transform::RotationBetween({ 0, 0, 1 }, Normalised({ 1, 1, 1 }), {});
    const Point3 d = tilt.ApplyDirection({ 0, 0, 1 });
    CHECK(d.x == doctest::Approx(1.0 / std::sqrt(3.0)));
}

TEST_CASE("the symmetric eigen solver finds a known basis")
{
    // diag(1, 4, 9) turned by Skew's rotation.
    const Transform r = Skew();
    double m[3][3] = {};
    const double values[3] = { 1, 4, 9 };
    for (int k = 0; k < 3; ++k)
    {
        Point3 e{ k == 0 ? 1.0 : 0.0, k == 1 ? 1.0 : 0.0, k == 2 ? 1.0 : 0.0 };
        const Point3 v = r.ApplyDirection(e);
        const double pv[3] = { v.x, v.y, v.z };
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                m[i][j] += values[k] * pv[i] * pv[j];
    }
    double found[3];
    Point3 vectors[3];
    SymmetricEigen(m, found, vectors);
    CHECK(found[0] == doctest::Approx(1.0));
    CHECK(found[1] == doctest::Approx(4.0));
    CHECK(found[2] == doctest::Approx(9.0));
    CHECK(Parallel(vectors[2], r.ApplyDirection({ 0, 0, 1 })));
}

TEST_CASE("the fitted box follows a turned box's own axes and size")
{
    Mesh mesh = BoxMesh(40.0, 20.0, 10.0, 8);
    const Transform skew = Skew();
    TransformMesh(mesh, skew);
    const OrientedBox box = FitOrientedBox(mesh);
    CHECK(box.halfSize[0] == doctest::Approx(40.0));
    CHECK(box.halfSize[1] == doctest::Approx(20.0));
    CHECK(box.halfSize[2] == doctest::Approx(10.0));
    CHECK(Parallel(box.axes[0], skew.ApplyDirection({ 1, 0, 0 })));
    CHECK(Parallel(box.axes[1], skew.ApplyDirection({ 0, 1, 0 })));
    CHECK(Parallel(box.axes[2], skew.ApplyDirection({ 0, 0, 1 })));
    const Point3 centre = skew.Apply({});
    CHECK(Length(box.centre - centre) < 1e-9);
    CHECK(Dot(Cross(box.axes[0], box.axes[1]), box.axes[2]) == doctest::Approx(1.0));

    const std::vector<Feature> features = BoxFeatures(box);
    CHECK(features.size() == 7);
    CHECK(Named(features, "Box plane C").kind == Feature::Kind::Plane);
    CHECK(Named(features, "Box centre").kind == Feature::Kind::Point);
}

TEST_CASE("growing from a click picks one face of a box, and the fitted plane is that face")
{
    Mesh mesh = BoxMesh(40.0, 20.0, 10.0, 8);
    const auto neighbours = TriangleNeighbours(mesh);
    for (const auto& n : neighbours)
        for (int k : n)
            REQUIRE(k >= 0);

    // A triangle on the top face (z = +10).
    int seed = -1;
    for (size_t t = 0; t < mesh.triangles.size() && seed < 0; ++t)
    {
        const auto& tri = mesh.triangles[t];
        if (mesh.vertices[tri[0]].z == 10.0 && mesh.vertices[tri[1]].z == 10.0 && mesh.vertices[tri[2]].z == 10.0)
            seed = static_cast<int>(t);
    }
    REQUIRE(seed >= 0);
    const std::vector<int> area = GrowFlatArea(mesh, neighbours, seed, 10.0, 0.5);
    CHECK(area.size() == 8 * 8 * 2);

    Feature plane;
    double rms = 1.0;
    REQUIRE(FitPlane(mesh, area, plane, &rms));
    CHECK(plane.direction.z == doctest::Approx(1.0));
    CHECK(plane.point.z == doctest::Approx(10.0));
    CHECK(rms < 1e-9);
    CHECK(plane.halfWidth > 40.0);
}

TEST_CASE("3-2-1 alignment squares a turned, moved box onto the origin")
{
    Mesh mesh = BoxMesh(40.0, 20.0, 10.0, 6);
    TransformMesh(mesh, Skew());
    OrientedBox box = FitOrientedBox(mesh);

    const Feature xy = Plane({}, { 0, 0, 1 });
    const Feature yz = Plane({}, { 1, 0, 0 });
    const Feature zx = Plane({}, { 0, 1, 0 });
    AlignState state;
    std::string message;

    // Step 1: the box's thin direction's centre plane onto XY.
    Transform move;
    REQUIRE(AlignStep(Named(BoxFeatures(box), "Box plane C"), xy, false, state, move, message));
    TransformMesh(mesh, move);
    box.ApplyTransform(move);
    CHECK(state.step == 1);

    // Step 2: the long direction's centre plane onto YZ, turning only about Z.
    REQUIRE(AlignStep(Named(BoxFeatures(box), "Box plane A"), yz, false, state, move, message));
    TransformMesh(mesh, move);
    box.ApplyTransform(move);
    CHECK(state.step == 2);

    // Step 3: the middle direction's centre plane onto ZX, sliding only.
    REQUIRE(AlignStep(Named(BoxFeatures(box), "Box plane B"), zx, false, state, move, message));
    TransformMesh(mesh, move);
    box.ApplyTransform(move);
    CHECK(state.step == 3);

    const Box3 bounds = Bounds(mesh);
    CHECK(bounds.low.x == doctest::Approx(-40.0));
    CHECK(bounds.high.x == doctest::Approx(40.0));
    CHECK(bounds.low.y == doctest::Approx(-20.0));
    CHECK(bounds.high.y == doctest::Approx(20.0));
    CHECK(bounds.low.z == doctest::Approx(-10.0));
    CHECK(bounds.high.z == doctest::Approx(10.0));

    // A fourth step is refused.
    CHECK_FALSE(AlignStep(Named(BoxFeatures(box), "Box plane B"), zx, false, state, move, message));
}

TEST_CASE("alignment refuses a mismatch and a second plane parallel to the first")
{
    Mesh mesh = BoxMesh(40.0, 20.0, 10.0, 2);
    const OrientedBox box = FitOrientedBox(mesh);
    AlignState state;
    std::string message;
    Transform move;

    Feature axis;
    axis.kind = Feature::Kind::Axis;
    axis.direction = { 0, 0, 1 };
    CHECK_FALSE(AlignStep(Named(BoxFeatures(box), "Box plane C"), axis, false, state, move, message));
    CHECK(state.step == 0);

    REQUIRE(AlignStep(Named(BoxFeatures(box), "Box plane C"), Plane({}, { 0, 0, 1 }), false, state, move, message));
    CHECK_FALSE(AlignStep(Named(BoxFeatures(box), "Box plane C"), Plane({}, { 0, 0, 1 }), false, state, move,
                          message));
    CHECK(state.step == 1);
}

TEST_CASE("flipping turns the scan over instead of the short way")
{
    Mesh mesh = BoxMesh(40.0, 20.0, 10.0, 2);
    const OrientedBox box = FitOrientedBox(mesh);
    AlignState state;
    std::string message;
    Transform move;
    // Box plane C's normal is +Z or -Z; onto XY flipped, it must end the other way.
    const Feature c = Named(BoxFeatures(box), "Box plane C");
    REQUIRE(AlignStep(c, Plane({}, { 0, 0, 1 }), true, state, move, message));
    const Point3 after = move.ApplyDirection(c.direction);
    CHECK(Dot(after, c.direction) == doctest::Approx(-1.0));
}
