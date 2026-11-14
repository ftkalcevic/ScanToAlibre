# Saved plan (14 November 2026, approved, not started): 6 Silhouette, 7 Compare, symmetry planes and deleting fitted geometry

Build order: Delete first, then Find Symmetry and Mid Plane, then 6 Silhouette, then 7 Compare, then Detect in 5 Surface and the Symmetric intent in Section. Compare's distance search is built with Find Symmetry, which needs it.

## Context

Frank asked what else would help turn a scan into an Alibre part. He chose two of the suggestions (14 November 2026):

- **Silhouette sketch.** The scan's outline seen along a plane's normal, fitted with lines and arcs and written as an Alibre sketch for Extrude. A slice at one height misses tabs and flanges that sit at other heights; the silhouette catches them. Frank's choices:
  - It gets its own ribbon button.
  - It includes every separate piece and every through-hole.
- **Compare.** The scan coloured by its signed distance from the part as built so far in Alibre. Frank's choices:
  - Colour bands.
  - Statistics.
  - Click to probe the distance at a point.
  - No Update button: the part is read when the dialog opens.

## 6 Silhouette

### Core: `core\Silhouette.h/.cpp`, with tests in `tests\SilhouetteTests.cpp`

`SectionResult SilhouetteMesh(const Mesh&, const SectionFrame&, double tolerance, double referenceTolerance, double smallestHole)` returns Section's own result type. That way FitProfile, FindConstraints, the drawing and the sketch writer all work on it unchanged.

1. **Project** every triangle onto the frame with `SectionFrame::ToPlane`.
2. **Rasterise** onto a grid. The cell is the larger of the tolerance and the extent divided by 4000.
   - Take the rasteriser, the one-cell gap closing and the loop chaining out of `ProjectionOutline` (`core\SurfaceFit.cpp:837`) as shared helpers, so 5 Surface's behaviour stays the same.
3. **Find the pieces and holes.** Every piece is kept. An empty region the outside can't reach is a through-hole. It's kept if its area is at least that of a circle of `smallestHole` (default 1 mm diameter); smaller ones (pinholes, scanner gaps) are filled.
4. **Trace every boundary loop** (outer loops and hole loops) with the chaining code.
5. **Snap the loops to sub-cell accuracy.** The contour edges are:
   - the mesh's edges where the two triangles face opposite ways along the normal;
   - open edges.

   They're projected into 2D and put in buckets. Each loop point moves to the nearest projected contour edge within 1.5 cells, and keeps its raster position if there's none. A light smoothing pass follows.
6. **Fill the result** for each loop:
   - `slices`: the loop in 3D, as `ToWorld` points on the plane.
   - `profiles`: `FitProfile(loop, closed=true, tolerance)`.
   - `reference`: `SimplifyPolyline`.
   - `behind` and `ahead`: the extent along the normal, as Section reports.

### Add-on

**`ScanDocument`**
- Add `bool silhouetting` and a second `Section silhouette` beside `section`. Each dialog keeps its own plane and settings.
- Add a `kind` (Slice or Silhouette) and `smallestHole` to `Section`.
- `ScanDocument::ActiveSection()` returns whichever is open.
- `UpdateSection` calls `SilhouetteMesh` for the Silhouette kind.

**`CSectionDlg`** (`addon\SectionDialog`)
- It takes the kind and a template ID, so the Silhouette button opens the same class on `IDD_SILHOUETTE`.
- `IDD_SILHOUETTE` is Section's template plus a "Smallest hole" mm field.

**`ScanViewCommand`**
- `ReadSectionPlane`, the Scan-plane click (`ScanViewCommand.cpp:279`, `:338`, `:476`) and `DrawSection` use `ActiveSection()`.
- The silhouette loops are drawn red and the fit green, as now.

**`SketchWriter`**
- `CreateSectionSketch` takes the `Section&` instead of `scan.section`.
- It calls `SilhouetteMesh` for that kind where it now calls `SectionMesh` (`SketchWriter.cpp:215`).
- Sketches are named "Scan Silhouette n".
- Holes go in as inner loops of the same sketch.

**Ribbon:** "6 Silhouette", menu ID 109, after 5 Surface. It needs a scan.

## 7 Compare

### Core: `core\Compare.h/.cpp`, with tests in `tests\CompareTests.cpp`

**`MeshFromFacets(values, mmPerUnit)`**
- Builds a triangle soup from the facets: 9 doubles a triangle, in model units, converted to mm.
- Corners are merged within 1e-6 mm.
- The winding is made outward by the signed volume, because Alibre's facet winding is unproven. CAMForAlibre does the same.

**`Deviations(scanPoints, part, range) -> std::vector<float>`**
- Each value is the signed distance from a scan point to the nearest point on the part's triangles. Positive means outside the part.
- Triangles go into a uniform grid with cells about the range. A point searches only within the range; past it the value is NaN, meaning "not modelled".
- Sign: where several triangles are about equally near (an edge or corner), take the triangle with the largest |dot| of `(p − closest)` with its normal.
- Runs on all cores with `std::execution::par`.

**`DeviationStats`**
- Counts of compared and not-compared points.
- The largest + and the largest −.
- The RMS.
- The percent within the tolerance.

**`DeviationBand(value, tolerance, range)`** picks one of 8 bands:

| Band | Values |
|---|---|
| Dark blue | −range to −range/2 |
| Blue | −range/2 to −tol |
| Green | ±tol |
| Yellow | tol to range/3 |
| Orange | range/3 to 2·range/3 |
| Red | 2·range/3 to range |
| Grey | beyond the range, or NaN |

### Add-on

**Reading the part**
- `IADPartSession::GetFacetData()` (`AlibreX_64.tlh:11882`, unproven on the live session) is read with `ReadNumbers`, as CAMForAlibre does.
- It's logged as `[compare] part facets: n triangles`.
- With no solid, the dialog says "Nothing to compare: the part has no solid yet".

**`ScanDocument::Compare`**
- `tolerance` (0.1 mm) and `range` (1.0 mm).
- `part` (Mesh), and per-vertex deviations for `mesh` (statistics and probe) and for `display` (colours).
- `stats`, and the band draw arrays (one `DrawArrays` per band).
- `bool comparing`.
- Changing the tolerance or range only re-bands the triangles, without searching again.
- Re-searching happens when the dialog opens, and when Refine or Orient changes the scan (`DrawingChanged` clears it).

**Drawing**
- While comparing, `Refresh` draws the band meshes in place of the grey scan. Each band is its own segment, coloured with `SetSegmentColor` (the proven way, as the orange selection is drawn).
- A triangle's band comes from the mean of its three corners.
- The probe point is drawn as a marker.

**`addon\CompareDialog.h/.cpp`** (`CCompareDlg`, a `CStepDialog`, `IDD_COMPARE`)
- Tolerance and Range fields.
- A legend: an owner-drawn strip of the band colours with their values.
- The statistics, for example: "Compared 812,344 points (93%); largest +0.412 / −0.287 mm; RMS 0.061 mm; 88.4% within ±0.1 mm".
- A **Probe** push-down button (`ClickMode::Probe`). A click on the scan uses `IntersectLine` to find the triangle, interpolates the full mesh's deviations at the hit point, and shows "At (x, y, z): +0.134 mm" or "not modelled".
- Close.

**Logging:** `[compare]` lines with the counts, the statistics and the time.

**Ribbon:** "7 Compare", menu ID 110, after 6 Silhouette. It needs a scan.

## Orient: symmetry planes, and deleting fitted planes and axes

Frank asked for these on 14 November 2026. Four pieces, all in 3 Orient, with what they feed further down.

**1. Delete.** A **Delete** button in Orient removes the chosen fitted plane or axis (`scan.chosen`, picked with Scan down) from `ScanDocument::fitted`.
- The box's planes, axes and centre can't be deleted; the button is greyed for them.
- Delete is kept for Undo (`Remember()`), clears `chosen`, marks the scan unsaved, and is logged as `[orient] deleted <name>`.
- A plane or axis already used by an alignment step stays aligned. The step moved the scan, not the feature.
- The other dialogs that list planes (Section's Scan plane click, and the new Silhouette) see it gone.

**2. Auto symmetry plane.** A **Find Symmetry** button in Orient.

`FindSymmetryPlane(mesh, box, fitted) -> std::vector<SymmetryCandidate>` goes in `core\Symmetry.h/.cpp`. Each candidate holds a plane, its RMS, the percent of points within 0.2 mm and the percent of points matched.
1. **Candidates:** the box's three centre planes, the planes through the scan's centroid square to the principal directions, and the planes through each fitted plane's and axis's direction.
2. **Scoring:** 20,000 vertices, area weighted, are mirrored. Each mirror's distance to the mesh comes from Compare's `Deviations` grid search, run against the scan itself.
3. **Refining:** the best few are refined by mirror ICP. Pair each point with its mirror's nearest point, then refit the plane through the pairs' midpoints, with its normal along the mean of `p − q`. This repeats until the plane moves less than 0.001 mm and 0.001 degrees, at most 30 times. Pairs farther than three times the RMS are dropped.

The best candidate is added as a fitted plane named "Symmetry plane n". It's then an ordinary plane: Align can put it on Alibre's YZ (for example), Section can slice on it, and Delete can remove it. The dialog shows its RMS and match percent. A score worse than 30% within 0.2 mm is refused with "No symmetry found".

**3. Manual symmetry: Mid Plane.** A **Mid Plane** button in Orient makes the plane halfway between two scan features.
- With one feature chosen (Scan down), push Mid Plane, then click a second.
- **Two planes**, for example a part's two side faces fitted with Fit Plane: the plane bisecting them, square to the mean of their normals (flipped to agree), through the midpoint of their points.
- **Two axes**, for example two matching holes: the plane square to the line joining them, through its middle.
- **Two points:** the same, through the middle.
- A plane and an axis are refused.
- The result is added as "Mid plane n", ready for Align.

This, with Move and Rotate (which already exist), is the manual way.

**4. Using symmetry later.**
- **5 Surface:** a **Detect** button beside the symmetry boxes. It scores the three world planes (XY, YZ, ZX) on the whole scan with the same mirror score and ticks the ones with at least 80% within 0.2 mm. Each score is shown, for example "YZ: 94% within 0.2 mm".
- **4 Section and 6 Silhouette:** a design intent check box, **Symmetric**, used when the sketch plane is square to a symmetry plane (any fitted plane named Symmetry or Mid plane, or one of the world planes Detect found).
  - Pairs of segments that mirror each other across the symmetry plane's line are found: lines within 2 degrees and the tolerance, and arcs with centres within the tolerance and equal radii.
  - Each pair is averaged so it's exactly symmetric.
  - A construction line goes on the symmetry line, and each pair's matching ends get a Symmetric constraint (`ADSketchConstraintType_AD_CONSTRAINT_SYMMETRIC`, which is in the 27.0.1 headers but unproven) about it.
  - This happens in `FindConstraints` (`core\Section`), as a new `ProfileConstraint::Kind::Symmetric`, and in SketchWriter.

**Resources:** `IDC_OR_DELETE`, `IDC_OR_FIND_SYMMETRY`, `IDC_OR_MID_PLANE`, `IDC_SURF_DETECT`, `IDC_SURF_SYMMETRY_SCORE`, `IDC_SEC_SYMMETRIC`. The Orient template grows to fit them.

**Tests** (`tests\SymmetryTests.cpp`, `tests\SectionTests.cpp`)
- A mesh mirrored about a tilted plane is found within 0.01 mm and 0.05 degrees.
- A part with no symmetry is refused.
- Mid Plane from two parallel planes 20 mm apart lies 10 mm from each.
- Mid Plane from two axes bisects them.
- Detect ticks YZ for a scan symmetric about YZ.
- The Symmetric intent pairs a mirrored profile's lines and arcs, and makes them exactly symmetric.

**Frank in Alibre**
1. Fit a plane and Delete it, then Undo.
2. Find Symmetry on a symmetric part, Align the symmetry plane to YZ, then Detect in 5 Surface.
3. Fit the two sides, make a Mid Plane and Align it.
4. Section on XY with Symmetric ticked: check the Symmetric constraints in the sketch.

## Shared

**Resource IDs from 13115**
- `IDD_SILHOUETTE`, `IDC_SIL_SMALLEST_HOLE`.
- `IDD_COMPARE`, `IDC_CMP_TOLERANCE`, `IDC_CMP_RANGE`, `IDC_CMP_LEGEND`, `IDC_CMP_STATS`, `IDC_CMP_PROBE`, `IDC_CMP_STATUS`.

**Project files:** the new files go in `core\ScanCore.vcxproj`, `tests\ScanCoreTests.vcxproj`, `addon\ScanToAlibre.vcxproj` and its filters. All are CRLF with no BOM, and non-ASCII characters are written as escapes (for example `L"−"` and `L"±"`).

**Docs**
- `docs\SPEC.md`: 6 Silhouette and 7 Compare sections with Frank's choices; Compare comes off the "later buttons" list.
- `CLAUDE.md`: current state and the ribbon line (menu IDs 103, 105, 106, 107, 108, 109, 110, 104, 101).
- `TODO.md` and `README.md`: updated to match.

## Unproven, to watch in the log

- That `IADPartSession::GetFacetData` on the open part gives the solid's facets, in model units, and whether inserted surfaces are included.
- Alibre's facet winding. It's made outward by the signed volume, so this matters only for the log.
- How fast 8 band meshes redraw on a scan of 1 million triangles. Only the simplified display mesh is drawn, as now.

## Verification

1. **Core tests (`ScanCoreTests.exe`), no Alibre needed:**
   - The silhouette of an L-bracket mesh along each axis gives the expected outline within 0.05 mm, and its tab, which a slice misses, appears.
   - A plate with a 5 mm through-hole keeps the hole, while a 0.5 mm pinhole is filled.
   - Two separate blocks give two loops.
   - A cylinder seen end-on fits one circle.
   - The deviations of a box scan against a box part 0.2 mm smaller are +0.2 everywhere; against a larger one they're −0.2.
   - Points beyond the range are NaN.
   - A part mesh with inward winding gets the same signs.
   - The band edges are as in the table.
   - The statistics are checked on known values.
   - Timing (skipped): 1 million points against a 50,000-triangle part.
   - The existing 74 cases still pass.
2. **Build:** Release x64 via MSBuild. **Install:** Alibre closed, UAC accepted.
3. **Frank in Alibre:**
   1. 6 Silhouette: pick XY and click Create Sketch. Check that every piece and through-hole is in the sketch, with its constraints, and extrude it.
   2. 6 Silhouette: pick one of the scan's planes, change Smallest hole and the offset, and watch the preview.
   3. 7 Compare on a partly modelled part: check the colours, that unmodelled areas are grey, and the statistics. Change the tolerance and range.
   4. 7 Compare: push Probe and click a few places; compare the values with Alibre's Measure.
   5. Read `[compare] part facets` in the log.
