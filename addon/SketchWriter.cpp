// SketchWriter.cpp : makes Alibre sketches from sections through the scan.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "SketchWriter.h"
#include "ScanDocument.h"
#include "Log.h"

#include "Section.h"
#include "SectionSeries.h"

#include <algorithm>
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

    // A 0-based array of doubles for Alibre (the caller destroys it).
    SAFEARRAY* MakeDoubles(const std::vector<double>& values)
    {
        SAFEARRAY* array = SafeArrayCreateVector(VT_R8, 0, static_cast<ULONG>(values.size()));
        if (array == nullptr)
            return nullptr;
        double* data = nullptr;
        if (FAILED(SafeArrayAccessData(array, reinterpret_cast<void**>(&data))))
        {
            SafeArrayDestroy(array);
            return nullptr;
        }
        std::copy(values.begin(), values.end(), data);
        SafeArrayUnaccessData(array);
        return array;
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

    // The plane to sketch on, offset along the section's normal: Alibre's
    // plane or face itself at no offset, a design plane at the offset from it,
    // or for a scan plane a design plane through three points on it.
    IDispatchPtr SketchPlaneAt(IADDesignSessionPtr design, IADSessionPtr session,
                               const ScanDocument::Section& section, double offset, double unit,
                               const std::wstring& baseName, std::wstring& planeName, const wchar_t*& step)
    {
        const Point3 normal = scancore::Normalised(section.normal);
        const Point3 origin = section.point + normal * offset;
        if (section.alibrePlane != nullptr && std::fabs(offset) < 1e-9)
        {
            planeName = Wide(section.planeName);
            return section.alibrePlane;
        }
        const std::wstring name = baseName + L" plane";
        planeName = name;
        if (section.alibrePlane != nullptr)
        {
            step = L"CreateAtOffsetToPlane";
            IADDesignPlanesPtr planes = design->GetDesignPlanes();
            IADDesignPlanePtr plane =
                planes->CreateAtOffsetToPlane(nullptr, section.alibrePlane, _variant_t(offset / unit),
                                              _bstr_t(name.c_str()));
            // Which way Alibre's offset goes is unproven: check, and go the other way if it's wrong.
            double distance = 0.0;
            if (plane != nullptr && PlaneDistance(plane, origin, normal, unit, distance) && std::fabs(distance) > 0.01)
            {
                Log(L"[section] the offset plane is %.4f mm from where it should be; making it the other way",
                    distance);
                plane->Delete();
                plane = planes->CreateAtOffsetToPlane(nullptr, section.alibrePlane, _variant_t(-offset / unit),
                                                      _bstr_t(name.c_str()));
            }
            return plane;
        }
        step = L"making a design plane for the scan's plane";
        const SectionFrame guess = scancore::FrameForPlane(origin, normal);
        return PlaneThroughPoints(design, session, origin, guess.xAxis, guess.yAxis, unit, name);
    }

    // Deletes the part feature with this name, if there is one (a loft Alibre
    // made with an error while refusing it). Logged.
    void DeleteFeatureNamed(IADPartFeaturesPtr features, const std::wstring& name)
    {
        try
        {
            const long count = features->GetCount();
            for (long i = 0; i < count; ++i)
            {
                IADPartFeaturePtr feature = features->GetItem(_variant_t(i));
                if (feature != nullptr && std::wstring(static_cast<LPCWSTR>(feature->GetName())) == name)
                {
                    feature->Delete();
                    Log(L"[series] deleted %s, which Alibre made with an error", name.c_str());
                    return;
                }
            }
        }
        catch (const _com_error& error)
        {
            Log(L"[series] deleting %s failed: %s", name.c_str(), HrText(error).c_str());
        }
    }

    // The sketch's own axes, from where its (0, 0), (1, 0) and (0, 1) are.
    SectionFrame ReadSketchFrame(IADSketchPtr sketch, double unit)
    {
        IADPointPtr p0, px, py;
        sketch->MapFromSketchToWorld(0.0, 0.0, &p0);
        sketch->MapFromSketchToWorld(1.0, 0.0, &px);
        sketch->MapFromSketchToWorld(0.0, 1.0, &py);
        SectionFrame frame;
        frame.origin = ReadPoint(p0) * unit;
        frame.xAxis = scancore::Normalised(ReadPoint(px) - ReadPoint(p0));
        frame.yAxis = scancore::Normalised(ReadPoint(py) - ReadPoint(p0));
        return frame;
    }

    // Adds the fitted profiles, then their joined ends, the design intent and
    // rounded radii, then the raw slice (reference, one polyline each, closed per
    // referenceClosed) as construction lines beside the fit, or as ordinary lines
    // without it. symmetry: the symmetry line, when the constraints include
    // Symmetric ones. Three edits, each inside BeginChange and EndChange; an edit
    // with nothing to add is left out, as Alibre seems to delete a new sketch whose
    // edit ends empty (Section Series' sketches with only splines vanished, while
    // every later call on them still succeeded).
    void WriteProfiles(IADSketchPtr sketch, const std::vector<Profile>& profiles,
                       const std::vector<ProfileConstraint>& constraints,
                       const std::vector<std::vector<Vec2>>& reference, const std::vector<bool>& referenceClosed,
                       bool fitted, bool raw, const scancore::SnapOptions* symmetry, double unit,
                       SketchReport& report, const wchar_t*& step)
    {
        bool anySymmetric = false;
        for (const ProfileConstraint& c : constraints)
            anySymmetric = anySymmetric || (fitted && symmetry != nullptr && c.kind == ProfileConstraint::Kind::Symmetric);

        size_t segments = 0;
        for (const Profile& profile : profiles)
            segments += profile.segments.size();
        const bool anyFigures = fitted && segments > 0;
        size_t rawSegments = 0;
        for (size_t pi = 0; raw && pi < reference.size(); ++pi)
            if (reference[pi].size() >= 2)
                rawSegments += reference[pi].size();

        // 1. The fitted figures.
        step = L"adding the figures";
        IADSketchFiguresPtr figures = sketch->GetFigures();
        std::vector<std::vector<Made>> made(profiles.size());
        IADSketchFigurePtr symmetryLine;
        if (anyFigures)
        {
        sketch->BeginChange();
        try
        {
            for (size_t pi = 0; fitted && pi < profiles.size(); ++pi)
            {
                for (const ProfileSegment& seg : profiles[pi].segments)
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
            // The symmetry line, a construction line a little past the profiles each way.
            if (anySymmetric)
            {
                const Vec2 p = symmetry->symmetryPoint;
                const Vec2 d = symmetry->symmetryDirection;
                double low = 1e300, high = -1e300;
                for (const scancore::Profile& profile : profiles)
                    for (const ProfileSegment& seg : profile.segments)
                        for (const Vec2& q : { seg.start, seg.end, seg.centre })
                        {
                            const double along = (q.x - p.x) * d.x + (q.y - p.y) * d.y;
                            low = (std::min)(low, along);
                            high = (std::max)(high, along);
                        }
                low -= 5.0;
                high += 5.0;
                IADSketchLinePtr line = figures->AddLine((p.x + d.x * low) / unit, (p.y + d.y * low) / unit,
                                                         (p.x + d.x * high) / unit, (p.y + d.y * high) / unit);
                symmetryLine = line;
                if (symmetryLine != nullptr)
                {
                    symmetryLine->PutIsReference(VARIANT_TRUE);
                    ++report.reference;
                }
            }
        }
        catch (...)
        {
            sketch->EndChange();
            throw;
        }
        sketch->EndChange();
        }

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
        // Whether a constraint was made is judged by the count going up: AddConstraint
        // returned false for coincident ends that Alibre joined anyway (proven with
        // the Alibre MCP tools, 14 November 2026: the count went up by 2), and a
        // coincident whose ends Alibre had already joined itself leaves the count
        // as it was, which isn't a refusal.
        size_t alreadyJoined = 0;
        auto add = [&](const wchar_t* what, IObjectCollectorPtr targets, ADSketchConstraintType type) {
            try
            {
                const long before = sketchConstraints->GetCount();
                const bool said = sketchConstraints->AddConstraint(targets, type) != VARIANT_FALSE;
                const long after = sketchConstraints->GetCount();
                if (said || after > before)
                {
                    ++report.constraints;
                }
                else if (type == ADSketchConstraintType_AD_CONSTRAINT_COINCIDENT)
                {
                    ++alreadyJoined;
                }
                else
                {
                    ++report.constraintsRefused;
                    if (refusalsLogged++ < 10)
                        Log(L"[section] %s refused: AddConstraint returned false and made nothing", what);
                }
            }
            catch (const _com_error& error)
            {
                ++report.constraintsRefused;
                if (refusalsLogged++ < 10)
                    Log(L"[section] %s refused: %s", what, HrText(error).c_str());
            }
        };

        if (anyFigures)
        {
        sketch->BeginChange();
        try
        {
            // The symmetry line stays where the scan's symmetry plane is.
            if (symmetryLine != nullptr)
                add(L"fixed symmetry line", Collect({ symmetryLine }), ADSketchConstraintType_AD_CONSTRAINT_FIX);
            for (size_t pi = 0; fitted && pi < profiles.size(); ++pi)
            {
                const Profile& profile = profiles[pi];
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
                if (!fitted)
                    break;
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
                case ProfileConstraint::Kind::Symmetric:
                {
                    // Two points mirrored about the symmetry line.
                    auto point = [&](size_t pi, size_t s, int which) -> IADSketchPointPtr {
                        return which == 2 ? centre(pi, s) : endPoint(pi, s, which == 1);
                    };
                    IADSketchPointPtr a = point(c.profile, c.segment, c.point);
                    IADSketchPointPtr b = point(c.otherProfile, c.otherSegment, c.otherPoint);
                    // The line of symmetry first: given last, Alibre mirrors the
                    // others about the first (proven in MCPForAlibre, 10 November
                    // 2026), which moved Frank's sketches off the scan.
                    if (a != nullptr && b != nullptr && symmetryLine != nullptr)
                        add(L"symmetric", Collect({ symmetryLine, a, b }), ADSketchConstraintType_AD_CONSTRAINT_SYMMETRIC);
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
                if (!fitted || c.kind != ProfileConstraint::Kind::Radius)
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
        }

        // 3. The slice itself: construction lines beside the fitted sketch, or
        // ordinary lines when it's the raw sketch alone.
        step = L"adding the raw slice";
        if (rawSegments > 0)
        {
        sketch->BeginChange();
        try
        {
            for (size_t pi = 0; raw && pi < reference.size(); ++pi)
            {
                const std::vector<Vec2>& points = reference[pi];
                const bool closed = pi < referenceClosed.size() && referenceClosed[pi];
                const size_t count = closed ? points.size() : points.size() - 1;
                for (size_t k = 0; k < count && points.size() >= 2; ++k)
                {
                    const Vec2& a = points[k];
                    const Vec2& b = points[(k + 1) % points.size()];
                    IADSketchLinePtr line = figures->AddLine(a.x / unit, a.y / unit, b.x / unit, b.y / unit);
                    if (IADSketchFigurePtr f = line)
                    {
                        if (fitted)
                        {
                            f->PutIsReference(VARIANT_TRUE);
                            ++report.reference;
                        }
                        else
                        {
                            ++report.rawLines;
                        }
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
        }

        Log(L"[section] %zu coincident ends Alibre had already joined itself; %zu figure lookups by ID failed",
            alreadyJoined, lookupsFailed);
    }
}

bool CreateSectionSketch(const std::wstring& sessionId, ScanDocument& scan, SketchReport& report,
                         std::wstring& message)
{
    const auto start = std::chrono::steady_clock::now();
    ScanDocument::Section* pSection = scan.ActiveSection();
    if (pSection == nullptr || scan.seriesing)
    {
        message = L"Open Section or Silhouette first.";
        return false;
    }
    ScanDocument::Section& section = *pSection;
    const bool silhouette = section.kind == ScanDocument::Section::Kind::Silhouette;
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
        swprintf_s(baseName, silhouette ? L"Scan Silhouette %d" : L"Scan Section %d", section.sketches);
        IDispatchPtr sketchPlane =
            SketchPlaneAt(design, session, section, section.offset, unit, baseName, report.planeName, step);
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

        step = L"MapFromSketchToWorld";
        const SectionFrame frame = ReadSketchFrame(sketch, unit);
        const Point3 sketchNormal = frame.Normal();
        const double offPlane = scancore::Dot(frame.origin - origin, normal);
        Log(L"[section] %s on %s: origin (%.3f, %.3f, %.3f) mm, x (%.4f, %.4f, %.4f), y (%.4f, %.4f, %.4f); "
            L"normal . section normal %.5f; origin %.4f mm off the section plane",
            report.sketchName.c_str(), report.planeName.c_str(), frame.origin.x, frame.origin.y, frame.origin.z,
            frame.xAxis.x, frame.xAxis.y, frame.xAxis.z, frame.yAxis.x, frame.yAxis.y, frame.yAxis.z,
            scancore::Dot(sketchNormal, normal), offPlane);
        if (std::fabs(scancore::Dot(sketchNormal, normal)) < 0.999 || std::fabs(offPlane) > 0.01)
            Log(L"[section] the sketch isn't on the section's plane; the scan is sliced on the sketch's plane");

        // Slice (or outline) and fit again in the sketch's axes.
        scan.SetSymmetryLine(section, frame);
        scancore::SectionResult result = SectionOf(scan.mesh, section, frame);
        const std::vector<ProfileConstraint> constraints =
            scancore::ApplyDesignIntent(result, frame, section.tolerance, section.snap);
        // Sketch Options: the fitted lines and arcs, the raw slice, or both (the
        // raw slice then as construction lines).
        const bool fitted = section.createFitted;
        const bool raw = section.createRaw;
        if (!fitted && !raw)
        {
            message = L"Tick Create Raw Sketch or Create Fitted Sketch.";
            return false;
        }
        report.largestDeviation = result.largestDeviation;
        if (result.profiles.empty())
        {
            message = silhouette ? L"The scan has no outline seen along the sketch's plane, so the sketch is empty."
                                 : L"The sketch's plane doesn't cut the scan, so the sketch is empty.";
            return false;
        }

        std::vector<bool> closed;
        for (const scancore::SlicePolyline& slice : result.slices)
            closed.push_back(slice.closed);
        WriteProfiles(sketch, result.profiles, constraints, result.reference, closed, fitted, raw, &section.snap, unit,
                      report, step);

        report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Log(L"[section] %s: %zu lines, %zu arcs, %zu circles (largest deviation %.4f mm); %zu constraints (%zu "
            L"refused), %zu dimensions (%zu refused); %zu construction lines, %zu raw lines; %.2f s",
            report.sketchName.c_str(), report.lines, report.arcs, report.circles, report.largestDeviation,
            report.constraints, report.constraintsRefused, report.dimensions, report.dimensionsRefused,
            report.reference, report.rawLines, report.seconds);
        return true;
    }
    catch (const _com_error& error)
    {
        Log(L"[section] making the sketch failed while %s: %s", step, HrText(error).c_str());
        message = std::wstring(L"Alibre refused while ") + step + L": " + HrText(error);
        return false;
    }
}

bool CreateSeriesSketches(const std::wstring& sessionId, ScanDocument& scan, SeriesReport& report,
                          std::wstring& message)
{
    const auto start = std::chrono::steady_clock::now();
    ScanDocument::Series& series = scan.series;
    const ScanDocument::Section& base = series.plane;
    const scancore::SeriesResult& result = series.result;
    if (!scan.seriesing || !base.havePlane)
    {
        message = L"Choose a plane first.";
        return false;
    }
    if (result.slices.empty())
    {
        message = series.invalid.empty() ? std::wstring(L"There are no planes. Press Make.") : Wide(series.invalid);
        return false;
    }
    const bool fitted = base.createFitted;
    const bool raw = base.createRaw;
    if (!fitted && !raw)
    {
        message = L"Tick Create Raw Sketch or Create Fitted Sketch.";
        return false;
    }
    const double unit = scan.MillimetresPerUnit();
    const wchar_t* step = L"finding the part";
    const bool loft = series.options.loft;
    // What the lofts are made from: each slice's sketch, and each chain's guides.
    std::vector<IADSketchPtr> sliceSketches(result.slices.size());
    std::vector<std::vector<IAD3DSketchPtr>> chainGuides(result.chains.size());

    // Per slice, the chains crossing it (and which of their slices it is).
    std::vector<std::vector<std::pair<size_t, size_t>>> chainsAt(result.slices.size());
    if (loft)
        for (size_t c = 0; c < result.chains.size(); ++c)
            for (size_t k = 0; k < result.chains[c].polyline.size(); ++k)
                chainsAt[result.chains[c].firstSlice + k].push_back({ c, k });

    try
    {
        IADSessionPtr session = theApp.m_pRoot->GetSessions()->GetItem(_variant_t(sessionId.c_str()));
        IADDesignSessionPtr design = session;
        if (design == nullptr)
        {
            message = L"This part couldn't be found.";
            return false;
        }
        report.series = ++series.made;

        for (size_t s = 0; s < result.slices.size(); ++s)
        {
            const scancore::SeriesSlice& slice = result.slices[s];
            size_t segments = 0;
            for (const Profile& p : slice.section.profiles)
                segments += p.segments.size();
            const bool anything = (fitted && (segments > 0 || !chainsAt[s].empty())) ||
                                  (raw && !slice.section.reference.empty());
            if (slice.section.slices.empty() || !anything)
            {
                ++report.empty;
                continue;
            }
            wchar_t baseName[64];
            swprintf_s(baseName, L"Scan Series %d.%zu", report.series, s + 1);
            std::wstring planeName;
            IDispatchPtr sketchPlane = SketchPlaneAt(design, session, base, slice.offset, unit, baseName, planeName, step);
            if (sketchPlane == nullptr)
            {
                message = std::wstring(L"Alibre didn't make the plane for ") + baseName + L".";
                return false;
            }
            step = L"AddSketch";
            IADSketchPtr sketch = design->GetSketches()->AddSketch(nullptr, sketchPlane, _bstr_t(baseName));
            if (sketch == nullptr)
            {
                message = std::wstring(L"Alibre didn't make ") + baseName + L".";
                return false;
            }
            sliceSketches[s] = sketch;
            const std::wstring sketchName = static_cast<LPCWSTR>(sketch->GetName());
            if (report.firstSketch.empty())
                report.firstSketch = sketchName;
            report.lastSketch = sketchName;
            ++report.sketches;

            step = L"MapFromSketchToWorld";
            const SectionFrame frame = ReadSketchFrame(sketch, unit);
            const double offPlane = scancore::Dot(frame.origin - slice.frame.origin, slice.frame.Normal());
            if (std::fabs(std::fabs(scancore::Dot(frame.Normal(), slice.frame.Normal())) - 1.0) > 1e-3 ||
                std::fabs(offPlane) > 0.01)
                Log(L"[series] %s isn't on its slice's plane (normals' dot %.5f, %.4f mm off); written as it is",
                    sketchName.c_str(), scancore::Dot(frame.Normal(), slice.frame.Normal()), offPlane);

            // Which of the slice's polylines are lofted, and how.
            std::vector<bool> inChain(slice.section.slices.size(), false);
            std::vector<Profile> profiles;
            std::vector<std::vector<Vec2>> splines;
            std::vector<Profile> lofted;
            for (const auto& at : chainsAt[s])
            {
                const scancore::SeriesChain& chain = result.chains[at.first];
                inChain[chain.polyline[at.second]] = true;
                if (chain.splines)
                    splines.push_back(scancore::MapPoints(chain.spline[at.second], slice.frame, frame));
                else
                    lofted.push_back(scancore::MapProfile(chain.profiles[at.second], slice.frame, frame));
            }
            // Everything else (open polylines, scraps, and every polyline without
            // Create Loft) as Section fits it, with Section's design intent.
            for (size_t k = 0; k < slice.section.slices.size(); ++k)
                if (!inChain[k])
                    profiles.push_back(scancore::MapProfile(slice.section.profiles[k], slice.frame, frame));
            std::vector<std::vector<Vec2>> reference;
            std::vector<bool> closed;
            for (size_t k = 0; k < slice.section.reference.size(); ++k)
            {
                reference.push_back(scancore::MapPoints(slice.section.reference[k], slice.frame, frame));
                closed.push_back(slice.section.slices[k].closed);
            }
            // Horizontal and vertical are judged in the sketch's own axes.
            scancore::SnapOptions snap = base.snap;
            snap.symmetric = false;
            const std::vector<ProfileConstraint> constraints = scancore::FindConstraints(profiles, snap);
            // The matched loft sections after them, with their ends joined but no
            // design intent or rounded radii: Alibre's solver, fitting the split
            // pieces of a 219 mm arc to a rounded radius with tangents between them,
            // shrank a 0.1 degree piece to nothing, drawn as a full circle, and the
            // loft refused the sketch as having several loops (Frank's test, 15
            // November 2026). Loft sections must stay as fitted, to match each other.
            profiles.insert(profiles.end(), lofted.begin(), lofted.end());

            // Closed splines through the matched points, the first repeated at
            // the end to close it. First, so the sketch's first edit has something
            // in it (see WriteProfiles).
            if (fitted && !splines.empty())
            {
                step = L"adding the splines";
                IADSketchFiguresPtr figures = sketch->GetFigures();
                sketch->BeginChange();
                try
                {
                    for (const std::vector<Vec2>& points : splines)
                    {
                        std::vector<double> xy;
                        for (size_t k = 0; k <= points.size(); ++k)
                        {
                            const Vec2& p = points[k % points.size()];
                            xy.push_back(p.x / unit);
                            xy.push_back(p.y / unit);
                        }
                        SAFEARRAY* array = MakeDoubles(xy);
                        try
                        {
                            IADSketchBsplinePtr spline = figures->AddBsplineByInterpolation(&array);
                            if (spline != nullptr)
                                ++report.splines;
                            else
                                ++report.splinesRefused;
                        }
                        catch (const _com_error& error)
                        {
                            ++report.splinesRefused;
                            if (report.splinesRefused <= 5)
                                Log(L"[series] %s: a spline of %zu points refused: %s", sketchName.c_str(),
                                    points.size(), HrText(error).c_str());
                        }
                        if (array != nullptr)
                            SafeArrayDestroy(array);
                    }
                }
                catch (...)
                {
                    sketch->EndChange();
                    throw;
                }
                sketch->EndChange();
            }

            SketchReport part;
            WriteProfiles(sketch, profiles, constraints, reference, closed, fitted, raw, nullptr, unit, part, step);
            report.lines += part.lines;
            report.arcs += part.arcs;
            report.circles += part.circles;
            report.reference += part.reference;
            report.rawLines += part.rawLines;
            report.constraints += part.constraints;
            report.constraintsRefused += part.constraintsRefused;
            report.dimensions += part.dimensions;
            report.dimensionsRefused += part.dimensionsRefused;
        }

        // Guide curves: a 3D sketch for each, as a loft takes its guide curves
        // as sketches (IADPartSession::Features' AddLoftBoss, "Guide curves must
        // be of type IAD3DSketch"), so one sketch holding them all would be one guide.
        if (loft && series.options.guides && fitted)
        {
            step = L"adding the guide curves";
            IADPartSessionPtr part = session;
            if (part == nullptr)
            {
                message = L"The sketches are made, but this isn't a part, so there are no guide curves.";
                return false;
            }
            IAD3DSketchesPtr sketches3d = part->GetSketches3D();
            for (size_t c = 0; c < result.chains.size(); ++c)
            {
                const scancore::SeriesChain& chain = result.chains[c];
                for (size_t g = 0; g < chain.guides.size(); ++g)
                {
                    const std::vector<Point3>& guide = chain.guides[g];
                    if (guide.size() < 2)
                        continue;
                    wchar_t name[96];
                    swprintf_s(name, L"Scan Series %d guide %zu.%zu", report.series, c + 1, g + 1);
                    try
                    {
                        IAD3DSketchPtr sketch3d = sketches3d->Add3DSketch(_bstr_t(name));
                        if (sketch3d == nullptr)
                        {
                            ++report.guidesRefused;
                            continue;
                        }
                        IAD3DSketchFiguresPtr figures = sketch3d->GetFigures();
                        sketch3d->BeginChange();
                        bool made = false;
                        try
                        {
                            if (guide.size() == 2)
                            {
                                made = figures->AddLine(guide[0].x / unit, guide[0].y / unit, guide[0].z / unit,
                                                        guide[1].x / unit, guide[1].y / unit,
                                                        guide[1].z / unit) != nullptr;
                            }
                            else
                            {
                                std::vector<double> xyz;
                                for (const Point3& p : guide)
                                {
                                    xyz.push_back(p.x / unit);
                                    xyz.push_back(p.y / unit);
                                    xyz.push_back(p.z / unit);
                                }
                                SAFEARRAY* array = MakeDoubles(xyz);
                                made = figures->AddBsplineByInterpolation(&array) != nullptr;
                                if (array != nullptr)
                                    SafeArrayDestroy(array);
                            }
                        }
                        catch (...)
                        {
                            sketch3d->EndChange();
                            throw;
                        }
                        sketch3d->EndChange();
                        (made ? report.guides : report.guidesRefused)++;
                        if (made)
                            chainGuides[c].push_back(sketch3d);
                    }
                    catch (const _com_error& error)
                    {
                        ++report.guidesRefused;
                        if (report.guidesRefused <= 5)
                            Log(L"[series] guide %s refused: %s", name, HrText(error).c_str());
                    }
                }
            }
        }

        // Create Loft: a loft boss through each run's sketches, in slice order,
        // with its chains' guide curves. A run of one slice has nothing to loft.
        if (loft && fitted)
        {
            step = L"making the lofts";
            IADPartSessionPtr part = session;
            IADPartFeaturesPtr features = part != nullptr ? part->GetFeatures() : nullptr;
            for (size_t r = 0; features != nullptr && r < result.runs.size(); ++r)
            {
                const scancore::SeriesRun& run = result.runs[r];
                if (run.loops == 0)
                    continue;
                IObjectCollectorPtr sections = theApp.m_pRoot->NewObjectCollector();
                size_t count = 0;
                for (size_t s = run.firstSlice; s <= run.lastSlice; ++s)
                    if (sliceSketches[s] != nullptr)
                    {
                        sections->Add(sliceSketches[s]);
                        ++count;
                    }
                if (count < 2)
                    continue;
                IObjectCollectorPtr guides = theApp.m_pRoot->NewObjectCollector();
                size_t guideCount = 0;
                for (size_t c = 0; c < result.chains.size(); ++c)
                    if (result.chains[c].run == r)
                        for (IAD3DSketchPtr& g : chainGuides[c])
                        {
                            guides->Add(g);
                            ++guideCount;
                        }
                wchar_t name[64];
                swprintf_s(name, L"Scan Series %d loft %zu", report.series, r + 1);
                auto empty = [] { return theApp.m_pRoot->NewObjectCollector(); };
                IADLoftFeaturePtr feature;
                std::wstring firstError;
                bool withoutGuides = false;
                try
                {
                    // Global guides (Claude's choice; Alibre also has Local and Tangent).
                    feature = features->AddLoftBoss(sections, empty(), empty(), empty(),
                                                    guideCount > 0 ? guides : empty(),
                                                    guideCount > 0 ? ADLoftGuideType_AD_GLOBAL : ADLoftGuideType_AD_NONE,
                                                    VARIANT_FALSE, VARIANT_FALSE, VARIANT_FALSE, VARIANT_FALSE,
                                                    _bstr_t(name));
                }
                catch (const _com_error& error)
                {
                    firstError = HrText(error);
                }
                // Alibre may make a refused loft anyway, with an error ("Feature is
                // created and is available in features list. This feature has error"),
                // which uses up its sketches: delete it before trying again without
                // the guides. (With nothing left to try, it stays, showing Alibre's error.)
                if (feature == nullptr && guideCount > 0)
                    DeleteFeatureNamed(features, name);
                // Refused with guides: try again without them, and say so.
                if (feature == nullptr && guideCount > 0)
                {
                    Log(L"[series] %s with %zu sections and %zu guides refused (%s); trying without the guides", name,
                        count, guideCount, firstError.empty() ? L"no feature returned" : firstError.c_str());
                    try
                    {
                        feature = features->AddLoftBoss(sections, empty(), empty(), empty(), empty(),
                                                        ADLoftGuideType_AD_NONE, VARIANT_FALSE, VARIANT_FALSE,
                                                        VARIANT_FALSE, VARIANT_FALSE, _bstr_t(name));
                        withoutGuides = feature != nullptr;
                        if (withoutGuides)
                            ++report.loftsWithoutGuides;
                    }
                    catch (const _com_error& error)
                    {
                        firstError = HrText(error);
                    }
                }
                if (feature != nullptr)
                {
                    ++report.lofts;
                    Log(L"[series] %s: %zu sections, %zu guides", name, count,
                        withoutGuides ? size_t{ 0 } : guideCount);
                }
                else
                {
                    ++report.loftsRefused;
                    if (report.loftError.empty())
                        report.loftError = firstError.empty() ? L"Alibre made no loft" : firstError;
                    Log(L"[series] %s refused: %s", name, report.loftError.c_str());
                }
            }
        }

        report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Log(L"[series] Scan Series %d: %zu sketches (%zu planes cut nothing), %zu splines (%zu refused), %zu lines, "
            L"%zu arcs, %zu circles, %zu constraints (%zu refused), %zu dimensions (%zu refused), %zu construction "
            L"lines, %zu raw lines, %zu guide curves (%zu refused), %zu lofts (%zu without guides, %zu refused); %.2f s",
            report.series, report.sketches, report.empty, report.splines, report.splinesRefused, report.lines,
            report.arcs, report.circles, report.constraints, report.constraintsRefused, report.dimensions,
            report.dimensionsRefused, report.reference, report.rawLines, report.guides, report.guidesRefused,
            report.lofts, report.loftsWithoutGuides, report.loftsRefused, report.seconds);
        return true;
    }
    catch (const _com_error& error)
    {
        Log(L"[series] making the sketches failed while %s: %s", step, HrText(error).c_str());
        message = std::wstring(L"Alibre refused while ") + step + L": " + HrText(error);
        return false;
    }
}
