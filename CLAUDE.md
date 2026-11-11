# ScanToAlibre

ScanToAlibre is an Alibre Design add-on, planned in C++/MFC for x64, that turns a 3D scan (STL mesh or point cloud) into an editable Alibre part. The user identifies features on the scan, and the add-on creates Alibre features from them; freeform areas get fitted B-spline surfaces inserted into the part.

The specification is `docs\SPEC.md`. `TODO.md` is Frank's list of work still to do. `README.md` covers building and installing.

## Working with Frank

- Frank commits and pushes himself. Don't commit unless he asks.
- Frank tests in Alibre by hand. End a change with a short numbered list of what to test, and say plainly what hasn't been built or run.
- Before calling something in Alibre, check it against the 27.0.1 headers (see below). Check the log before asking Frank what happened.
- Ask before making a product decision that isn't recorded here or in the spec. Propose options when you do.
- When a decision changes, update the spec too.

## Locations

| What | Where |
|---|---|
| This project | `D:\Projects\alibre\ScanToAlibre` |
| Sister project, the pattern to follow | `D:\Projects\alibre\AlibreCAM` (CAMForAlibre) |
| Proof-of-concept test bed | `D:\Projects\alibre\camtest` |
| Alibre API docs and samples (read-only, never modify) | `C:\AlibreToolBox\MyAddon - Claude Reference\MyAddon - Claude Reference\Reference\` |
| Generated 27.0.1 headers | `obj\x64\<cfg>\ScanToAlibre\AlibreX_64.tlh`, `.tli`, `AlibreAddOn_64.tlh` |
| Log, and the test surface file | `%LOCALAPPDATA%\ScanToAlibre\ScanToAlibre.log`, `test-surface.igs` |
| Installed add-on | `C:\ProgramData\Alibre AddOns\ScanToAlibre\` |

Target Alibre 27.0.1.27039. The API docs describe v29, so a documented call may not exist. The generated headers are the authority.

## Building and testing

- Open or build `ScanToAlibre.sln`, x64 only, with Visual Studio 2022 (v143) and MFC installed. Output goes to `bin\x64\<cfg>\` and intermediates to `obj\x64\<cfg>\<Project>\`.
- Run `bin\x64\<cfg>\ScanCoreTests.exe` after any change to `core\`. It doesn't need Alibre.
- Install with `powershell -ExecutionPolicy Bypass -File deploy\install.ps1 [-Configuration Debug]`. Close Alibre first and restart it afterwards. The script asks for administrator rights.
- Release links with full link-time code generation, as CAMForAlibre does (incremental LTCG once kept stale code there).

## Architecture

- `addon\` builds `ScanToAlibre.dll`. It's the only code that calls Alibre or MFC. The add-on identifier is `{3B8754FC-F18D-4AE9-887D-E48DD9EE5DCB}` (in `ScanToAlibre.adc` and the deploy scripts).
- `core\` builds the `ScanCore` static library (namespace `scancore`) with no Alibre or MFC code, UTF-8 text, millimetres. Put logic here wherever possible so it can be tested.
- `tests\` builds `ScanCoreTests.exe` with doctest.
- `third_party\` holds pinned copies with their licences: doctest 2.5.3 (tests only). Eigen and nanoflann come with fitting.
- New add-on files go in both `addon\ScanToAlibre.vcxproj` and its `.filters` file. New core files go in `core\ScanCore.vcxproj`. In `addon\resource.h`, resources and controls use 13xxx IDs.
- Reuse what CAMForAlibre has proven: the add-on skeleton, view command, `On3DRender` drawing, units, logging, `WriteFileAtomically`, `Nurbs`, `SurfaceGeometry` and `ArcFit`. Its `CLAUDE.md` lists the Alibre API behaviour proven so far (picking, persistent keys, events, units, graphics); read it before working on the add-on side.

## Conventions

- Source files use CRLF line endings, except in `third_party\`.
- Source files have no byte-order mark. Write non-ASCII characters in string literals as escapes, such as `L"\u00B0"`.
- Entry points called from Alibre start with `AFX_MANAGE_STATE(AfxGetStaticModuleState())`, as do functions that open dialogs.
- Files are written atomically (`WriteFileAtomically` in `core\FileIO`). Never overwrite a file that is damaged or from a newer format version.

## Current state

- The skeleton is built, installed and tested by Frank in Alibre (11 November 2026): the ScanToAlibre ribbon tab (`addon\AddOnInterface`), the log and crash catcher (copied from CAMForAlibre), and About.
- Insert Test Surface (`addon\SurfaceTest`, greyed outside parts) is tested by Frank. It writes `TestHumpSurface()` (`core\BSplineSurface`: a bicubic patch over X 0 to 100, Y 0 to 80, Z 20 to about 30 mm) as IGES with `WriteIgesSurface` (`core\Iges`: one entity 128 in mm) to `test-surface.igs` beside the log, then calls `DesignSurfaces->InsertFromFile` (stitching, heal on, mm) and reports and logs (`[surface]`) the design surfaces before and after, with their faces and edges.

- Refine is built, installed and untested in Alibre. `CRefineDlg` (`addon\RefineDialog`, `IDD_REFINE`, IDs 13010 to 13026) is a modeless dialog; `CStepDialog` (`addon\StepDialog`) makes the workflow's dialogs, one at a time (`Open` closes the open one; `CloseStepDialog` at unload; Enter does nothing, Close and the X close it). Opening Refine also starts the scan view. Its choices are remembered for the Alibre session (`g_choices`). Decimate calls `Decimate` (`core\Decimate`: quadric edge collapse with the link condition and a fold check, open edges held by planes square to them, weight 100 when kept; error estimate the square root of the quadric cost, conservative), Smooth calls `Smooth` (`core\Smooth`: Laplacian; Taubin with a pass band of 0.1; Bilateral, which is bilateral normal filtering (Sun et al. 2007, Zheng et al. 2011) then ten vertex updates; a vertex form after Fleishman 2003 rounded a cube's corners and shrank a sphere more than Laplacian, so it was replaced). Both run on all cores where they can, on Alibre's thread with a wait cursor. `ScanDocument::ReplaceMesh` keeps up to `kUndoSteps` (5) earlier meshes for Undo, and remakes the box and the simplified drawing; each change sets `unsaved`, calls `MarkPartChanged` and `RefreshScanViews`. Logged as `[refine]`. On `mt12cover.ply` (1,069,936 triangles): decimate to half 3.5 s, to a tenth 6.5 s (largest change about 0.026 mm); smoothing, 5 passes, 1.0 s (Laplacian, Taubin) and 1.8 s (Bilateral). Tests in `tests\RefineTests.cpp`; skipped `refine timing*`.
- Orient is built, installed and untested in Alibre. `COrientDlg` (`addon\OrientDialog`, `IDD_ORIENT`, IDs 13030 to 13058) is a step dialog (`CStepDialog`, one at a time; Load closes it). Opening it sets `ScanDocument::orienting`, so the scan view draws the fitted box, its centre planes and axes (A red, B green, C blue), its centre, fitted planes (orange), the chosen one (yellow, thicker) and the selected triangles (orange, `SelectionArrays`). Clicks: in Choose mode `PickFeature` takes an axis or point within 8 pixels on screen first, then the nearest plane the click's line meets inside its outline; a click on none is left to Alibre, whose selection `OnSelectionChange` reads (`SelectedObjectsEx`, item or proxy target; `IADDesignPlane::GetGeometry` and `GetNormal`, `IADDesignAxis::GetGeometry` and `GetDirection`, `IADDesignPoint::GetGeometry`; model units to mm) as the target. In Select mode `GrowAt` grows `GrowFlatArea` from the clicked triangle and adds it, or takes it away if the triangle was selected. Core in `core\Orient` (`Transform`, `SymmetricEigen` by Jacobi, `FitOrientedBox` from area-weighted surface moments, `BoxFeatures`, `TriangleNeighbours`, `GrowFlatArea` refitting as it doubles, `FitPlane`, `AlignState` and `AlignStep`), tested in `tests\OrientTests.cpp`, including a turned, moved box squared onto the origin in three steps. `ScanDocument` now keeps the fitted box, fitted planes, selection, alignment state, chosen feature and target; `Move` and `AddFittedPlane` are undoable with the mesh (`Snapshot`), and `ReplaceMesh` fits the box again. Unproven in Alibre: that its built-in planes, axes and origin can be clicked in a part while our command runs and arrive in `SelectedObjectsEx`, and that a click our command doesn't handle still selects them.
- No help page yet.

## Proven Alibre API behaviour (27.0.1)

See also CAMForAlibre's `CLAUDE.md` for what was proven there.

- `IADPartSession::DesignSurfaces->InsertFromFile(path, ADFaceProcessingType_AD_STITCH_ADJOINING_FACES, VARIANT_TRUE, VARIANT_FALSE, ADUnits_AD_MILLIMETERS)` inserts our IGES entity 128 patch into an open part from an add-on, in about 0.25 s. It returned one object, and the part gained one design surface, named `Surface<1>`, with 1 face and 4 edges. The design surfaces collection is 0-based.
- In Alibre's own interface, an extrude **To Geometry** of a sketch on XY stops at the inserted surface (tested by hand, not yet from the API).
- A regular command returned straight from `InvokeCommand` works: Alibre calls `On3DRender`, and `DrawMesh` with arrays made once and reused every render draws 230,700 triangles in 14 to 23 ms and 1.07 million in about 90 ms (the call, not Alibre's own drawing).
- In a regular command Alibre calls `OnClick` (left button is 1) but not `OnMouseDown` or `OnMouseUp`.
- `OnClick`'s coordinates and `WorldToScreen`'s are the same, with no conversion: picks with the click as given landed under the cursor. `WorldToScreen` takes a 0-based array of three doubles in model units. A projection fitted to it on eight box corners had an error of 0 to 0.9 pixels (it seems to round).
- `ScreenToWorld` with two doubles fails with hr=0x80131533 (a .NET SafeArrayTypeMismatchException), so its argument type is something else; it isn't needed.
- `GetCameraPosition` isn't the eye's position in world coordinates: in an orthographic view looking down -Z (eye on the +Z side) it gave (0, 0, -28.35) model units, on the far side, and using it to choose the near side picked the far side. Don't use it for direction; `ViewProjection::IntoScreen` gives it.
- A native command such as Measure ends the regular command (`OnTerminate`), so add-on graphics disappear until it's started again (as in CAMForAlibre, whose tab restarts it).
- The ribbon is the workflow (Frank's decision; see the spec): 1 Load, 2 Refine, 3 Orient, then Show Scan and About (`addon\AddOnInterface`, menu IDs 103, 105, 106, 104, 101). Refine, Orient and Show Scan need the part to have a scan. Insert Test Surface (102) is off the ribbon; `addon\SurfaceTest` still has it.
- Load (`addon\ScanImport`, `LoadScan`) is built, installed and untested: it asks before replacing the part's scan, then reads `.stl` or `.ply` with `ReadScanFile` (`core\ScanFile`). STL: `ReadStl` (`core\Mesh`: binary or ASCII, decided by size, corners joined when exactly equal, degenerate triangles dropped). PLY: `ParsePly` (`core\Ply`: ASCII and binary either endian, any numeric types, polygons split into fans, other properties and lists read past). A PLY with no faces is a point cloud: Load refuses it for now, saying so (making a mesh from it is Frank's choice and still to do). `MakeScanDocument` (`addon\ScanDocument`) keeps the mesh, its box and `SimplifyForDisplay` (vertex clustering on a grid, at most `kDisplayTriangles`, 300,000). Timings: `mt12cover.ply` (1.07 million faces) 0.18 s.
- Saving the scan inside the part is built and tested by Frank (Frank's choice over a file beside it). `HasPersistentDataToSave` says yes when the part has a scan, `SaveData` writes `WriteScanData` (`core\ScanData`, format version 1: "STASCAN", counts, source name, float32 vertices, uint32 triangles; 19 MB for 1.07 million triangles) to Alibre's stream, and `LoadData` reads it back with `ReadScanData` when the part is opened (newer or damaged data is kept as it was with `KeepUnreadData` and written back unchanged; Load asks before replacing it). All logged as `[save]` and `[load]`. The part's units are read when the scan view starts (`ScanDocument::SetMillimetresPerUnit`). Frank's first test: the scan wasn't saved, and Alibre didn't see the part as changed, so there was no Save; the log had no `[save]` lines, as Alibre never saved. The API has no way to mark a part changed.
- Inserting the scan as a native design mesh was tried and removed: `IADDesignMeshes::CreateFromFile` (and `GetDesignMeshes` itself) fail in 27.0.1 with hr=0x80040307, "This method is obsolete and no more supported". `WriteBinaryStl` (`core\Mesh`) stays.
- Load makes Alibre see the part as changed with `MarkPartChanged` (`addon\ScanImport`): a count parameter named `ScanToAlibre` (`IADSession::GetParameters`) goes up by one each time the scan changes, with a comment naming the scan; logged as `[changed]`, naming the step that fails. Frank tested it: it shows in the Equation Editor and Save becomes available. `NewParameter` must be called outside a parameter transaction (inside one it fails with "Parameter transaction already open") and `PutValue` inside one (outside, "No opened transaction found"), so it creates, then opens, sets and closes.
- Alibre only calls `HasPersistentDataToSave`, `SaveData` and `LoadData` for an add-on whose `.adc` declares a stream: `<PersistentData streamName="..."/>` (from `C:\AlibreToolBox\Integrated_Addons_in_Alibre Design.pdf`, section 1.6 and page 8, which spells it `StreamName` there; `ScanToAlibre.adc` has both, `ScanToAlibre_Scan`). Without it, Frank's save wrote nothing and the log had no `[save]` lines. With it, Frank tested saving and reopening: Save As wrote the scan (19,258,752 bytes, 32 ms) and reopening read it back (`LoadData`, 31 ms).
- Alibre calls `HasPersistentDataToSave` many times, not only when saving, and a yes makes the part look unsaved (Frank: "it always says the file needs saving" while it answered yes whenever there was a scan). Now it answers `ScanDocument::unsaved`: set by Load, cleared by `SaveData` and `LoadData`; only a change of answer is logged. Frank tested it (11 November 2026): Save lights after Load and goes off after saving or opening. A save while it answers no still calls `SaveData` (15:37:45 no, 15:38:00 wrote 19,258,752 bytes), so every save writes the scan and none drops it. A new part with only a scan loaded didn't light Save until Save As (the `ScanToAlibre` parameter change didn't light it either).
  - `CScanViewCommand` (`addon\ScanViewCommand`, a regular command returned from `InvokeCommand`) draws the scan with `DrawMesh`, hidden by the model, in light blue. The canvas arrays are made once per level of detail and reused every render. F switches between full detail and simplified, and C clears the picked points. The first five renders are timed in the log (`[view] render n`).
  - Frank tested the drawing: it works, "a bit sluggish" (simplified renders 14 to 23 ms, full detail about 90 ms; fewer points to come), and the scan disappears while a native command such as Measure runs, as that ends the view command.
  - Picking: a left click picks in `OnClick`, which marks the click handled when it hits the scan. Frank's test found it picked the far side: the line was flipped by `GetCameraPosition`. Now `LineThrough` alone sets the direction (its plane normals are the gradients of screen u and v, so their cross product is right x down, into the screen; `ViewProjection::IntoScreen`, tested on a cube from above and below), and the camera position is only logged once (`[probe] camera position`). Frank tested picking in Alibre (11 November 2026): points land under the cursor on the near side, from any side, and clicks on the background add nothing. `ViewProjection` (`core\ViewProjection`) fits a 3 x 4 projection to `WorldToScreen` of the scan box's eight corners, works for orthographic and perspective views, and reports its largest error. `LineThrough` gives the line through the click, oriented away from `GetCameraPosition`. `IntersectLine` finds the nearest hit on the whole scan by brute force (21 ms on 1.07 million triangles). The click is tried as given, then converted screen to viewport and viewport to screen, as which coordinates each call uses is unknown. Picks are logged (`[pick]`, with the conversion that hit and the fit error) and drawn with `DrawMarker` crosshairs. The first click also logs what the undocumented `ScreenToWorld` returns (`[probe]`).
  - Frank's test scan: `D:\3dscanner\mt12cover\mt12cover -cleaned.stl` (binary, 1,069,186 triangles, 534,578 vertices, about 37 x 24 x 66 mm, not at the origin). Read 0.33 s, simplified to 230,700 triangles in 0.14 s (skipped test `real scan timing*` with `SCANTOALIBRE_STL`).
- The icon is CAMForAlibre's for now.
