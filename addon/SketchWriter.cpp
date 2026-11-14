// SketchWriter.cpp : makes an Alibre sketch from a section through the scan.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "SketchWriter.h"
#include "ScanDocument.h"
#include "Log.h"

#include "Section.h"

#include <chrono>
#include <cmath>
#include <vector>

using scancore::Point3;
using scancore::Profile;
using scancore::ProfileConstraint;
using scancore::ProfileSegment;
using scancore::SectionFrame;
using scancore::Vec2;

namespace
{
    std::wstring Wide(const std::string& text)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring HrText(const _com_error& error)
    {
        wchar_t text[256];
        const _bstr_t description = error.Description();
        swprintf_s(text, L"hr=0x%08X %s", static_cast<unsigned>(error.Error()),
                   description.length() > 0 ? static_cast<LPCWSTR>(description) : L"");
        return text;
    }

    IObjectCollectorPtr Collect(std::initializer_list<IDispatch*> items)
    {
        IObjectCollectorPtr collector = theApp.m_pRoot->NewObjectCollector();
        for (IDispatch* item : items)
            collector->Add(item);
        return collector;
    }

    Point3 ReadPoint(IADPointPtr p)
    {
        return { p->GetX(), p->GetY(), p->GetZ() };
    }

    // What one fitted segment became in the sketch.
    struct Made
    {
        IADSketchFigurePtr figure;  // as added, for when it has no ID
        std::wstring idDuringChange;
        std::wstring id;            // the figure's ID, read after the change, to find it again
        bool reversed = false;      // an arc drawn from its end, as Alibre's go anticlockwise
    };

    // A plane through three points on the scan's plane: tried with plain
    // geometry points first (unproven), then with design points.
    IADDesignPlanePtr PlaneThroughPoints(IADDesignSessionPtr design, IADSessionPtr session, const Point3& origin,
                                         const Point3& u, const Point3& v, double unit, const std::wstring& name)
    {
        const Point3 corners[3] = { origin, origin + u * 10.0, origin + v * 10.0 };
        IADDesignPlanesPtr planes = design->GetDesignPlanes();
        try
        {
            IADGeometryFactoryPtr factory = session->GetGeometryFactory();
            IADPointPtr p[3];
            for (int k = 0; k < 3; ++k)
                p[k] = factory->CreatePoint(corners[k].x / unit, corners[k].y / unit, corners[k].z / unit);
            IADDesignPlanePtr plane = planes->CreateBy3Points(nullptr, p[0], nullptr, p[1], nullptr, p[2],
                                                              _bstr_t(name.c_str()));
            if (plane != nullptr)
            {
                Log(L"[section] design plane made through three geometry points");
                return plane;
            }
        }
        catch (const _com_error& error)
        {
            Log(L"[section] CreateBy3Points with geometry points failed (%s); trying design points",
                HrText(error).c_str());
        }
        IADDesignPointsPtr points = design->GetDesignPoints();
        IADDesignPointPtr p[3];
        for (int k = 0; k < 3; ++k)
        {
            wchar_t pointName[96];
            swprintf_s(pointName, L"%s point %d", name.c_str(), k + 1);
            p[k] = points->CreatePoint(corners[k].x / unit, corners[k].y / unit, corners[k].z / unit,
                                       _bstr_t(pointName));
        }
        IADDesignPlanePtr plane =
            planes->CreateBy3Points(nullptr, p[0], nullptr, p[1], nullptr, p[2], _bstr_t(name.c_str()));
        Log(L"[section] design plane made through three design points");
        return plane;
    }

    // The signed distance of a design plane from a point, along a normal, in mm.
    bool PlaneDistance(IADDesignPlanePtr plane, const Point3& from, const Point3& normal, double unit,
                       double& distance)
    {
        IADPointPtr p1, p2, p3;
        plane->GetGeometry(&p1, &p2, &p3);
        if (p1 == nullptr)
            return false;
        distance = scancore::Dot(ReadPoint(p1) * unit - from, normal);
        return true;
    }
}

bool CreateSectionSketch(const std::wstring& sessionId, ScanDocument& scan, SketchReport& report,
                         std::wstring& message)
{
    const auto start = std::chrono::steady_clock::now();
    ScanDocument::Section& section = scan.section;
    if (!section.havePlane)
    {
        message = L"Choose a plane first.";
        return false;
    }
    const double unit = scan.MillimetresPerUnit();
    const Point3 normal = scancore::Normalised(section.normal);
    const Point3 origin = section.point + normal * section.offset;
    const wchar_t* step = L"finding the part";

    try
    {
        IADSessionPtr session = theApp.m_pRoot->GetSessions()->GetItem(_variant_t(sessionId.c_str()));
        IADDesignSessionPtr design = session;
        if (design == nullptr)
        {
            message = L"This part couldn't be found.";
            return false;
        }

        // The plane to sketch on.
        ++section.sketches;
        wchar_t baseName[64];
        swprintf_s(baseName, L"Scan Section %d", section.sketches);
        IDispatchPtr sketchPlane;
        if (section.alibrePlane != nullptr && std::fabs(section.offset) < 1e-9)
        {
            sketchPlane = section.alibrePlane;
            report.planeName = Wide(section.planeName);
        }
        else if (section.alibrePlane != nullptr)
        {
            step = L"CreateAtOffsetToPlane";
            const std::wstring name = std::wstring(baseName) + L" plane";
            IADDesignPlanesPtr planes = design->GetDesignPlanes();
            IADDesignPlanePtr plane =
                planes->CreateAtOffsetToPlane(nullptr, section.alibrePlane, _variant_t(section.offset / unit),
                                              _bstr_t(name.c_str()));
            // Which way Alibre's offset goes is unproven: check, and go the other way if it's wrong.
            double distance = 0.0;
            if (plane != nullptr && PlaneDistance(plane, origin, normal, unit, distance) && std::fabs(distance) > 0.01)
            {
                Log(L"[section] the offset plane is %.4f mm from where it should be; making it the other way",
                    distance);
                plane->Delete();
                plane = planes->CreateAtOffsetToPlane(nullptr, section.alibrePlane,
                                                      _variant_t(-section.offset / unit), _bstr_t(name.c_str()));
            }
            sketchPlane = plane;
            report.planeName = name;
        }
        else
        {
            step = L"making a design plane for the scan's plane";
            const SectionFrame guess = scancore::FrameForPlane(origin, normal);
            const std::wstring name = std::wstring(baseName) + L" plane";
            sketchPlane = PlaneThroughPoints(design, session, origin, guess.xAxis, guess.yAxis, unit, name);
            report.planeName = name;
        }
        if (sketchPlane == nullptr)
        {
            message = L"Alibre didn't make the plane to sketch on.";
            return false;
        }

        step = L"AddSketch";
        IADSketchPtr sketch = design->GetSketches()->AddSketch(nullptr, sketchPlane, _bstr_t(baseName));
        if (sketch == nullptr)
        {
            message = L"Alibre didn't make the sketch.";
            return false;
        }
        report.sketchName = static_cast<LPCWSTR>(sketch->GetName());

        // The sketch's own axes, from where its (0, 0), (1, 0) and (0, 1) are.
        step = L"MapFromSketchToWorld";
        IADPointPtr p0, px, py;
        sketch->MapFromSketchToWorld(0.0, 0.0, &p0);
        sketch->MapFromSketchToWorld(1.0, 0.0, &px);
        sketch->MapFromSketchToWorld(0.0, 1.0, &py);
        SectionFrame frame;
        frame.origin = ReadPoint(p0) * unit;
        frame.xAxis = scancore::Normalised(ReadPoint(px) - ReadPoint(p0));
        frame.yAxis = scancore::Normalised(ReadPoint(py) - ReadPoint(p0));
        const Point3 sketchNormal = frame.Normal();
        const double offPlane = scancore::Dot(frame.origin - origin, normal);
        Log(L"[section] %s on %s: origin (%.3f, %.3f, %.3f) mm, x (%.4f, %.4f, %.4f), y (%.4f, %.4f, %.4f); "
            L"normal . section normal %.5f; origin %.4f mm off the section plane",
            report.sketchName.c_str(), report.planeName.c_str(), frame.origin.x, frame.origin.y, frame.origin.z,
            frame.xAxis.x, frame.xAxis.y, frame.xAxis.z, frame.yAxis.x, frame.yAxis.y, frame.yAxis.z,
            scancore::Dot(sketchNormal, normal), offPlane);
        if (std::fabs(scancore::Dot(sketchNormal, normal)) < 0.999 || std::fabs(offPlane) > 0.01)
            Log(L"[section] the sketch isn't on the section's plane; the scan is sliced on the sketch's plane");

        // Slice and fit again in the sketch's axes.
        const scancore::SectionResult result =
            scancore::SectionMesh(scan.mesh, frame, section.tolerance, kReferenceTolerance);
        const std::vector<ProfileConstraint> constraints = scancore::FindConstraints(result.profiles, section.snap);
        report.largestDeviation = result.largestDeviation;
        if (result.profiles.empty())
        {
            message = L"The sketch's plane doesn't cut the scan, so the sketch is empty.";
            return false;
        }

        // 1. The fitted figures.
        step = L"adding the figures";
        IADSketchFiguresPtr figures = sketch->GetFigures();
        std::vector<std::vector<Made>> made(result.profiles.size());
        sketch->BeginChange();
        try
        {
            for (size_t pi = 0; pi < result.profiles.size(); ++pi)
            {
                for (const ProfileSegment& seg : result.profiles[pi].segments)
                {
                    Made m;
                    IADSketchFigurePtr figure;
                    if (seg.kind == ProfileSegment::Kind::Line)
                    {
                        figure = figures->AddLine(seg.start.x / unit, seg.start.y / unit, seg.end.x / unit,
                                                  seg.end.y / unit);
                        ++report.lines;
                    }
                    else if (seg.kind == ProfileSegment::Kind::Arc)
                    {
                        // Alibre's arcs go anticlockwise from start to end.
                        m.reversed = !seg.anticlockwise;
                        const Vec2 from = m.reversed ? seg.end : seg.start;
                        const Vec2 to = m.reversed ? seg.start : seg.end;
                        figure = figures->AddCircularArcByCenterStartEnd(seg.centre.x / unit, seg.centre.y / unit,
                                                                         from.x / unit, from.y / unit, to.x / unit,
                                                                         to.y / unit);
                        ++report.arcs;
                    }
                    else
                    {
                        figure = figures->AddCircle(seg.centre.x / unit, seg.centre.y / unit,
                                                    _variant_t(seg.radius / unit));
                        ++report.circles;
                    }
                    m.figure = figure;
                    if (figure != nullptr)
                        m.idDuringChange = static_cast<LPCWSTR>(figure->GetID());
                    made[pi].push_back(m);
                }
            }
        }
        catch (...)
        {
            sketch->EndChange();
            throw;
        }
        sketch->EndChange();

        // Frank's first sketch got no constraints: the IDs read inside the
        // change were empty, it seems. Read them again now, and keep the figures
        // themselves for when there's still none.
        size_t emptyDuring = 0, emptyAfter = 0, figuresKept = 0;
        for (auto& profile : made)
            for (Made& m : profile)
            {
                if (m.figure == nullptr)
                    continue;
                ++figuresKept;
                emptyDuring += m.idDuringChange.empty() ? 1 : 0;
                try
                {
                    m.id = static_cast<LPCWSTR>(m.figure->GetID());
                }
                catch (const _com_error&)
                {
                }
                emptyAfter += m.id.empty() ? 1 : 0;
            }
        Log(L"[section] %zu figures: %zu had no ID inside the change, %zu none after it%s%s", figuresKept, emptyDuring,
            emptyAfter, made.empty() || made[0].empty() ? L"" : L"; the first is ",
            made.empty() || made[0].empty() ? L"" : made[0][0].id.c_str());

        // 2. Joined ends, design intent and rounded radii.
        step = L"adding the constraints";
        size_t lookupsFailed = 0;
        auto find = [&](size_t pi, size_t s) -> IADSketchFigurePtr {
            if (pi >= made.size() || s >= made[pi].size())
                return nullptr;
            const Made& m = made[pi][s];
            if (!m.id.empty())
            {
                try
                {
                    if (IADSketchFigurePtr f = figures->GetFigureByID(_bstr_t(m.id.c_str())))
                        return f;
                }
                catch (const _com_error&)
                {
                }
                ++lookupsFailed;
            }
            return m.figure;
        };
        // A segment's end in the sketch, in the profile's direction.
        auto endPoint = [&](size_t pi, size_t s, bool atEnd) -> IADSketchPointPtr {
            IADSketchFigurePtr f = find(pi, s);
            if (f == nullptr)
                return nullptr;
            const bool end = made[pi][s].reversed ? !atEnd : atEnd;
            if (IADSketchLinePtr line = f)
                return end ? line->GetEnd() : line->GetStart();
            if (IADSketchCircularArcPtr arc = f)
                return end ? arc->GetEnd() : arc->GetStart();
            return nullptr;
        };
        auto centre = [&](size_t pi, size_t s) -> IADSketchPointPtr {
            IADSketchFigurePtr f = find(pi, s);
            if (IADSketchCircularArcPtr arc = f)
                return arc->GetCenter();
            if (IADSketchCirclePtr circle = f)
                return circle->GetCenter();
            return nullptr;
        };
        IADSketchConstraintsPtr sketchConstraints = sketch->GetSketchConstraints();
        size_t refusalsLogged = 0;
        auto add = [&](const wchar_t* what, IObjectCollectorPtr targets, ADSketchConstraintType type) {
            try
            {
                sketchConstraints->AddConstraint(targets, type);
                ++report.constraints;
            }
            catch (const _com_error& error)
            {
                ++report.constraintsRefused;
                if (refusalsLogged++ < 10)
                    Log(L"[section] %s refused: %s", what, HrText(error).c_str());
            }
        };

        sketch->BeginChange();
        try
        {
            for (size_t pi = 0; pi < result.profiles.size(); ++pi)
            {
                const Profile& profile = result.profiles[pi];
                const size_t n = profile.segments.size();
                if (n == 1 && profile.segments[0].kind == ProfileSegment::Kind::Circle)
                    continue;
                for (size_t s = 0; s < n; ++s)
                {
                    if (s + 1 == n && !profile.closed)
                        break;
                    IADSketchPointPtr a = endPoint(pi, s, true), b = endPoint(pi, (s + 1) % n, false);
                    if (a != nullptr && b != nullptr)
                        add(L"coincident", Collect({ a, b }), ADSketchConstraintType_AD_CONSTRAINT_COINCIDENT);
                }
            }
            for (const ProfileConstraint& c : constraints)
            {
                switch (c.kind)
                {
                case ProfileConstraint::Kind::Horizontal:
                case ProfileConstraint::Kind::Vertical:
                    if (IADSketchFigurePtr f = find(c.profile, c.segment))
                        add(c.kind == ProfileConstraint::Kind::Horizontal ? L"horizontal" : L"vertical", Collect({ f }),
                            c.kind == ProfileConstraint::Kind::Horizontal ? ADSketchConstraintType_AD_CONSTRAINT_HORIZONTAL
                                                                          : ADSketchConstraintType_AD_CONSTRAINT_VERTICAL);
                    break;
                case ProfileConstraint::Kind::Tangent:
                {
                    IADSketchFigurePtr f = find(c.profile, c.segment), g = find(c.otherProfile, c.otherSegment);
                    if (f != nullptr && g != nullptr)
                        add(L"tangent", Collect({ f, g }), ADSketchConstraintType_AD_CONSTRAINT_TANGENT);
                    break;
                }
                case ProfileConstraint::Kind::Concentric:
                {
                    IADSketchPointPtr a = centre(c.profile, c.segment), b = centre(c.otherProfile, c.otherSegment);
                    if (a != nullptr && b != nullptr)
                        add(L"concentric", Collect({ a, b }), ADSketchConstraintType_AD_CONSTRAINT_COINCIDENT);
                    break;
                }
                default:
                    break;
                }
            }
            // Rounded sizes as dimensions, which drive the figures to them.
            IADDimensionsPtr dimensions = sketch->GetDimensions();
            for (const ProfileConstraint& c : constraints)
            {
                if (c.kind != ProfileConstraint::Kind::Radius)
                    continue;
                IADSketchFigurePtr f = find(c.profile, c.segment);
                if (f == nullptr)
                    continue;
                try
                {
                    if (IADSketchCirclePtr circle = f)
                        dimensions->PlaceDiametricDimension(circle, _variant_t(2.0 * c.value / unit));
                    else
                        dimensions->PlaceRadialDimension(f, _variant_t(c.value / unit));
                    ++report.dimensions;
                }
                catch (const _com_error& error)
                {
                    ++report.dimensionsRefused;
                    if (refusalsLogged++ < 10)
                        Log(L"[section] radius %.3f refused: %s", c.value, HrText(error).c_str());
                }
            }
        }
        catch (...)
        {
            sketch->EndChange();
            throw;
        }
        sketch->EndChange();

        // 3. The slice itself, as construction lines.
        step = L"adding the reference slice";
        sketch->BeginChange();
        try
        {
            for (size_t pi = 0; pi < result.reference.size(); ++pi)
            {
                const std::vector<Vec2>& points = result.reference[pi];
                const bool closed = result.slices[pi].closed;
                const size_t count = closed ? points.size() : points.size() - 1;
                for (size_t k = 0; k < count && points.size() >= 2; ++k)
                {
                    const Vec2& a = points[k];
                    const Vec2& b = points[(k + 1) % points.size()];
                    IADSketchLinePtr line = figures->AddLine(a.x / unit, a.y / unit, b.x / unit, b.y / unit);
                    if (IADSketchFigurePtr f = line)
                    {
                        f->PutIsReference(VARIANT_TRUE);
                        ++report.reference;
                    }
                }
            }
        }
        catch (...)
        {
            sketch->EndChange();
            throw;
        }
        sketch->EndChange();

        report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Log(L"[section] %s: %zu lines, %zu arcs, %zu circles (largest deviation %.4f mm); %zu constraints (%zu "
            L"refused), %zu dimensions (%zu refused); %zu construction lines; %.2f s; %zu figure lookups by ID failed",
            report.sketchName.c_str(), report.lines, report.arcs, report.circles, report.largestDeviation,
            report.constraints, report.constraintsRefused, report.dimensions, report.dimensionsRefused,
            report.reference, report.seconds, lookupsFailed);
        return true;
    }
    catch (const _com_error& error)
    {
        Log(L"[section] making the sketch failed while %s: %s", step, HrText(error).c_str());
        message = std::wstring(L"Alibre refused while ") + step + L": " + HrText(error);
        return false;
    }
}
