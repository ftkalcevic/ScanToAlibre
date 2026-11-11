// Geometry.h : points and vectors in millimetres.

#pragma once

#include <cmath>

namespace scancore
{
    struct Point3
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    inline Point3 operator+(const Point3& a, const Point3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline Point3 operator-(const Point3& a, const Point3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline Point3 operator*(const Point3& a, double s) { return { a.x * s, a.y * s, a.z * s }; }
    inline Point3 operator/(const Point3& a, double s) { return { a.x / s, a.y / s, a.z / s }; }
    inline double Dot(const Point3& a, const Point3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Point3 Cross(const Point3& a, const Point3& b)
    {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }
    inline double Length(const Point3& a) { return std::sqrt(Dot(a, a)); }

    // A unit vector, or zero if a is too short to have a direction.
    inline Point3 Normalised(const Point3& a)
    {
        const double length = Length(a);
        return (length > 1e-300) ? a / length : Point3{};
    }

    struct Box3
    {
        Point3 low{ 1e300, 1e300, 1e300 };
        Point3 high{ -1e300, -1e300, -1e300 };

        bool Empty() const { return low.x > high.x; }
        void Add(const Point3& p)
        {
            low = { std::fmin(low.x, p.x), std::fmin(low.y, p.y), std::fmin(low.z, p.z) };
            high = { std::fmax(high.x, p.x), std::fmax(high.y, p.y), std::fmax(high.z, p.z) };
        }
        Point3 Centre() const { return (low + high) * 0.5; }
        Point3 Size() const { return high - low; }
    };
}
