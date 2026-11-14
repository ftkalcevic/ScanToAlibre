// SectionSeries.h : a series of sections through a scan, for lofting.
//
// The scan is sliced at each offset from a base plane (with Section's own
// code, so each slice matches what Section shows). With Create Loft the
// closed loops are followed from slice to slice, and each loop followed
// through a run of slices (a chain) is fitted so its slices match point for
// point, as a loft needs:
// - Fit Spline: N points per slice by arc length from matching starts, the
//   same N in every slice of the chain, with a point pinned on each corner
//   found in every slice, for a closed interpolating spline per slice;
// - lines and arcs: Section's fit in every slice, started at a corner common
//   to every slice, with segments split (never moved) so every slice has the
//   same number of segments between common corners.
// Guide curves join matching points across the slices: the pinned or common
// corners, or four points evenly spaced by arc length when there are none.
//
// A slice whose loop count changes, or a loop with no partner in the next
// slice, ends a run; each run is matched on its own. Open polylines (where
// the plane leaves an open scan) are never matched, and are written plain.

#pragma once

#include <string>
#include <vector>

#include "Mesh.h"
#include "Section.h"

namespace scancore
{
    struct SeriesOptions
    {
        double tolerance = 0.05;            // mm, as Section's
        bool fitArcs = true;                // Section's Fit arcs (lines and arcs)
        bool loft = true;                   // Create Loft (Optimise for Loft before Frank renamed it)
        bool splines = true;                // Fit Spline; false: Use lines and Arcs
        bool guides = true;                 // Create Guide Curves (needs loft)
        double cornerDegrees = 30.0;        // a turn sharper than this is a corner
        size_t maxSplinePoints = 512;       // per loop; past it the spline is left as it is
    };

    // Offsets strictly increasing and finite, at least one. Otherwise false,
    // with a message saying what's wrong.
    bool ValidateOffsets(const std::vector<double>& offsets, std::string& message);

    // count offsets evenly from first to last (one: first).
    std::vector<double> EvenOffsets(double first, double last, int count);

    // The scan's extent along the base plane's normal (offsets from the base
    // origin), less a margin of 2% of the extent at each end.
    void DefaultSeriesRange(const Mesh& mesh, const SectionFrame& base, double& first, double& last);

    struct SeriesSlice
    {
        double offset = 0.0;
        SectionFrame frame;                 // the base frame moved along its normal by offset
        SectionResult section;              // Section's slice and fit, in frame
        size_t loops = 0;                   // closed loops used for matching
        size_t open = 0;                    // open polylines
        size_t scraps = 0;                  // tiny closed loops left out of matching
    };

    // A run of slices with the same number of loops, each matched to the next.
    struct SeriesRun
    {
        size_t firstSlice = 0, lastSlice = 0;
        size_t loops = 0;
    };

    // One loop followed through a run.
    struct SeriesChain
    {
        size_t run = 0;
        size_t firstSlice = 0;              // slices firstSlice to firstSlice + polyline.size() - 1
        std::vector<size_t> polyline;       // per slice: its index in that slice's section.slices
        bool splines = true;                // fitted as splines (also when lines and arcs fell back)
        bool fellBack = false;              // lines and arcs couldn't be matched; fitted as splines
        // Fit Spline: per slice, N points in that slice's frame, anticlockwise,
        // the first the start (not repeated at the end).
        std::vector<std::vector<Vec2>> spline;
        // Lines and arcs: per slice, the same number of segments in the same order.
        std::vector<Profile> profiles;
        size_t corners = 0;                 // corners matched in every slice
        bool withinTolerance = true;        // splines: every slice within the tolerance
        double largestDeviation = 0.0;      // mm, of the slices' points from the fit (99th percentile for splines)
        // Each guide: a point per slice, in slice order, in world coordinates.
        std::vector<std::vector<Point3>> guides;
    };

    // Two neighbouring sections of a chain whose outlines don't overlap seen
    // along the series: a loft between them swings sideways and is likely to
    // fold through itself (Frank's ACIS invalid body, 15 November 2026).
    struct SeriesGap
    {
        size_t chain = 0;
        size_t first = 0, second = 0;       // slice indices, second = first + 1
        double firstArea = 0.0, secondArea = 0.0;   // mm^2
    };

    struct SeriesResult
    {
        std::vector<SeriesSlice> slices;
        std::vector<SeriesRun> runs;
        std::vector<SeriesChain> chains;
        std::vector<SeriesGap> gaps;        // with loft: neighbouring sections that don't overlap
        size_t openPolylines = 0;
        std::vector<std::string> notes;     // what couldn't be matched, for the status line and log
        double seconds = 0.0;
    };

    // Slices the mesh at each offset along base's normal and, with
    // options.loft, matches the loops for lofting. The offsets must pass
    // ValidateOffsets.
    SeriesResult SliceSeries(const Mesh& mesh, const SectionFrame& base, const std::vector<double>& offsets,
                             const SeriesOptions& options);

    // Whether two closed outlines in the same 2D axes overlap: one's point
    // inside the other, or their edges crossing.
    bool OutlinesOverlap(const std::vector<Vec2>& a, const std::vector<Vec2>& b);

    // The runs in words: "slices 1 to 7: 2 loops; slice 8: 3 loops" (1-based).
    std::string DescribeRuns(const SeriesResult& result);

    // A closed interpolating cubic spline through points (chord length
    // parameters, periodic), sampled steps times a span: for drawing the fit
    // and checking it. The curve isn't closed by repeating its first point.
    std::vector<Vec2> ClosedSplineCurve(const std::vector<Vec2>& points, int steps);

    // Points or a profile in one frame, in another frame whose plane is the
    // same (a sketch's own axes): through world coordinates, so a turn or a
    // mirror between the two is taken care of (a mirrored arc changes direction).
    std::vector<Vec2> MapPoints(const std::vector<Vec2>& points, const SectionFrame& from, const SectionFrame& to);
    Profile MapProfile(const Profile& profile, const SectionFrame& from, const SectionFrame& to);
}
