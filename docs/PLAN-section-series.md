# Plan (14 November 2026, approved by Frank, not started): Section Series, for lofting organic shapes

Code waits until the Scan tab tree (the other session's work in `ScanDocument`, `ScanViewCommand`, `ScanDisplayCommand`, `ScanData` version 3 and `resource.h`) is built, so the two don't edit the same files at once. Frank chose this.

## Context

Frank wants to rebuild organic shapes such as `mt12` in Alibre, using lofts through sections. Lofts need every section to have the same number of points and a similar shape. Surface patches are hard to trim unless one lies wholly inside the other, so lofting comes first. A patch layout (fitted patches whose shared edges meet exactly) may come later.

Frank's choices (14 November 2026):

- **A new dialog, "Section Series", next to "Section" on the ribbon.** Every ribbon button loses its number: Show Scan, Load, Refine, Orient, Section, Section Series, Surface, Silhouette, Compare, About.
- **Planes:** start from a chosen plane (picked as in Section: an Alibre plane or face, or a scan plane), then make offset planes automatically. The offsets are in a list that the user can edit. Offsets can be negative but must always be in order.
- **Optimise for Loft** (check box, default on). When it's on, a radio pair:
  - **Fit Spline:** one closed spline per slice. The control points are the same in number in every slice, and the start and direction are matched.
  - **Use lines and Arcs:** the same number of segments in the same order in every slice.
- **Create Guide Curves** (check box, default on): a 3D sketch with a spline through each set of matching points across the slices.

## Dialog: `addon\SectionSeriesDialog.h/.cpp`, `IDD_SECTION_SERIES`

It reuses Section's code wherever it can: plane picking (one-shot Pick buttons), tolerance, Sketch Options (Create Raw Sketch, Create Fitted Sketch, Fit arcs) and the Symmetric intent, which applies to each slice.

1. **Plane:** Pick Alibre Plane, Pick Scan Plane, and the chosen plane's name.
2. **Series:**
   - **First** and **Last** offsets (mm), defaulting to the scan's extent along the normal less a margin of 2% of the extent at each end.
   - **Count** (default 10).
   - A **Make** button that fills the list evenly from First to Last.
3. **Offsets list** (a list control, one row per plane: offset in mm, and the slice's loops and points):
   - Double-click a row, or press F2, to edit its offset. An offset that breaks the order is refused with a status message, and the old value stays.
   - **Add** puts a new plane halfway between the chosen row and the next. **Delete** removes the chosen rows.
   - Choosing a row draws its slice highlighted in the view. All slices are drawn thin.
4. **Optimise for Loft** check box, with the Fit Spline / Use lines and Arcs radios greyed while it's off.
5. **Create Guide Curves** check box, greyed unless Optimise for Loft is on, since guides need matched points.
6. **Sketch Options** group, as in Section.
7. **Create Sketches:** one 2D sketch per row, named "Scan Series n.k" on a design plane "Scan Series n.k plane". With guides on, also a 3D sketch "Scan Series n guides". The status line reports counts and time, and any slice that couldn't be matched (see below).

The plane, offsets and options are kept per part in `ScanDocument::Series` (not saved with the part, like Section's settings).

## Core: `core\SectionSeries.h/.cpp`, tests in `tests\SectionSeriesTests.cpp`

`SeriesResult SliceSeries(const Mesh&, const SectionFrame& base, const std::vector<double>& offsets, const SeriesOptions&)`

1. **Slice:** each offset is sliced with Section's own code (`SectionMesh` on the frame moved along its normal), so slices match what Section shows.
2. **Match the loops across slices:**
   - Each slice's closed loops are paired with the next slice's by the centroid and area of the outlines in the frame.
   - A loop with no partner, or a slice whose loop count changes (a boss appearing, a hole closing), splits the series into runs. Each run is matched on its own and reported ("slices 4 to 7: 2 loops; slice 8: 3 loops"). Lofting across the change is Frank's choice in Alibre, with a sketch per slice either way.
   - Open polylines (where the plane leaves the scan) are reported and written plain, never matched.
3. **Fit Spline** (per run of matched loops):
   - Every loop runs anticlockwise in the frame.
   - **Start point:** the loop's sharpest corner, matched across slices to the corner nearest the previous slice's start (by angle round the centroid, then distance). With no corner (a round loop), the point at the angle of the previous slice's start, so the seam stays straight.
   - **Resampling:** N points by arc length from the start. N is the most any loop in the run needs to stay within the tolerance (doubling from 16 until the interpolating spline through them is within tolerance of the slice), then used for every loop in the run.
   - **Corners:** where a corner matched in every slice of the run lies, the arc-length sampling is pinned so a sample falls exactly on it. Guides then run along real edges.
   - Result: per loop, N points for `AddBsplineByInterpolation` (closed, the first point repeated at the end).
4. **Use lines and Arcs** (per run):
   - Fit each loop with `FitProfile`, then find the corners (segment joins with a turn over the corner angle) common to every slice, by arc-length position and angle round the centroid.
   - Between consecutive common corners, every slice must have the same number of segments. Where a slice has fewer, its longest segment in that stretch is split: a line in two lines, an arc in two arcs on the same circle. Where it has more than the target, the target becomes its count and the others are split to match. Splitting never moves the curve, so the tolerance holds.
   - Loops are started at the first common corner and run anticlockwise, so segment k matches segment k in every slice.
   - If the stretches can't be matched (a slice has no common corners and different numbers of segments), fall back to Fit Spline for that run and say so.
5. **Guide curves:** for each matched point set (the pinned corners; with none, 4 points evenly spaced by arc length), the points in every slice of the run, in 3D, in slice order.
6. **SeriesOptions:** tolerance, `loft` (bool), `splines` (bool, Fit Spline over lines and arcs), `guides` (bool), corner angle (default 30 degrees), and Section's `SnapOptions` for the lines-and-arcs fit.

## Addon: writing to Alibre (`addon\SketchWriter`)

- **Planes:** per row, a design plane at the offset, as Section makes one for a scan plane or an offset.
- **2D sketches:**
  - Fit Spline: `IADSketchFigures::AddBsplineByInterpolation` with the N points (2D, in sketch units), closed by repeating the first point.
  - Lines and arcs: Section's existing writer, with the constraints and dimensions it makes now, and the raw slice per Sketch Options.
- **Guides:** `IAD3DSketches::Add3DSketch`, then `IAD3DSketchFigures::AddBsplineByInterpolation` per guide (3D points in model units).
- **Unproven in 27.0.1** and checked first on a throwaway part with the Alibre MCP tools, closed without saving:
  - whether the 2D interpolating spline closes smoothly (or needs `AddBspline` with a periodic knot vector);
  - the point array's layout and units;
  - whether a 3D sketch spline can be a loft guide.
- Lofting itself stays Frank's job in Alibre. `AddLoftBoss` exists, but which sketches to loft and how is a design choice.

## Ribbon

- Remove the numbers from the menu texts in `AddOnInterface.cpp`.
- Add `MENU_ID_SECTION_SERIES` (next free ID 111) after Section in the children list, enabled for parts with a scan, opening the dialog and the scan view as Section does.
- Update the spec, README and `CLAUDE.md` where they name buttons by number ("4 Section", "6 Silhouette", ...).

## Drawing (scan view)

While the dialog is open: every row's slice in thin red, the chosen row's slice and fit thicker. With Fit Spline, the spline's points as small markers, and the guide curves in green.

## Tests (`tests\SectionSeriesTests.cpp`)

1. A tapered box (square at the bottom, a smaller rotated square at the top, made in the test): one run, every slice the same number of spline points and lines, starts at matching corners, and guides along the 4 edges.
2. An ellipse-to-circle loft shape (no corners): the same N, starts on one straight seam, 4 guides.
3. A shape whose loop count changes (a cylinder with a boss on part of its length): two runs, reported.
4. Lines and arcs: a slot that gets longer along the series. Same segment counts, with splits that keep every slice within the tolerance.
5. Offsets out of order are refused by the dialog's checker (`ValidateOffsets` in core, tested).
6. Replay (skipped, `SCANTOALIBRE_STL`): `mt12cover` along its long axis. Report the runs, N, and time.

## Test in Alibre (Frank)

1. The ribbon has no numbers, and Section Series sits next to Section.
2. Pick a plane, press Make, edit offsets (including a negative one and one out of order, which should be refused), then Add and Delete.
3. Create Sketches with Fit Spline and guides, then loft through the sketches with the guides in Alibre.
4. The same with Use lines and Arcs.
5. Optimise for Loft off: plain Section sketches at each offset.
6. On `mt12`, check the reported runs where the shape changes.
