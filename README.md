# ScanToAlibre: Alibre Design add-on (C++/MFC)

An add-on for Alibre Design that helps turn a 3D scan (an STL mesh or a point cloud) into an editable Alibre part. You pick out features on the scan (planes, cylinders, sections), and it creates Alibre sketches and features from them. Freeform areas get fitted B-spline surfaces inserted into the part.

Status: early. The add-on gets its own **ScanToAlibre** ribbon tab. First is **Show Scan**, which shows or hides the scan and a Scan tab in the Design Explorer listing its fitted planes and axes (click one to show it; right-click to rename or delete it). Then the workflow, in order. In a part:

1. **Load** reads a scan (STL, or PLY: a mesh, or a point cloud with normals, which it makes into a mesh) and shows it in the view. The scan is saved inside the part when you save the part. Left-click on it to pick points. F switches between full detail and simplified, and C clears the picked points.
2. **Refine** decimates the scan to fewer triangles and smooths it (Laplacian, Taubin or Bilateral), in a dialog that stays open while you work. Changes apply straight away; Undo takes them back.
3. **Orient** moves, rotates and scales the scan, fits planes to flat areas, and lines it up with the part's planes, axes and origin in 3-2-1 steps, using a box fitted to the scan with its centre planes and axes.

4. **Section** slices the scan with an Alibre plane or face, or one of the scan's planes, fits lines and arcs to the slice, and makes an Alibre sketch of them, constrained (horizontal, vertical, tangent, concentric) with rounded radii, and the slice itself as construction lines. Extrude or revolve it with Alibre's own commands.
5. **Surface** fits a B-spline surface to areas of the scan you select (smooth areas or a brush), as heights over a base plane, optionally symmetric across XY, YZ or ZX and trimmed to the whole scan's outline or the selection's, or the rectangle round the selection or round the scan's box, made bigger by an offset if asked, and inserts it into the part to extrude up to.
6. **Silhouette** makes a sketch of the scan's outline seen along a plane's normal, with every separate piece and through-hole, fitted and constrained as Section's are. It catches tabs and flanges that a slice at one height misses.
7. **Compare** colours the scan by its distance from the part as built so far (blues inside, green within a tolerance, yellow to red outside, grey where the part isn't modelled yet), with statistics, and Probe reads the distance where you click.

Then **About**.

See `docs\SPEC.md` for the plan.

It targets Alibre Design 27.0.1 and follows the layout of CAMForAlibre:

| Folder | Purpose |
|---|---|
| `addon\` | `ScanToAlibre.dll`, the only code that calls Alibre |
| `core\` | `ScanCore` library: surfaces, IGES writing and file helpers, with no Alibre or MFC code |
| `tests\` | `ScanCoreTests.exe`, which runs without Alibre |
| `third_party\` | Copies of external libraries, with their licences |
| `deploy\` | Install and uninstall scripts |

The log is `%LOCALAPPDATA%\ScanToAlibre\ScanToAlibre.log`.

## Requirements

- Visual Studio 2022 with the **Desktop development with C++** workload, the **C++ MFC for latest v143 build tools (x86 & x64)** component, and a Windows 10/11 SDK.
- Alibre Design (64-bit) installed under `C:\Program Files\Alibre Design <version>\`.

## Build

Open `ScanToAlibre.sln`, choose **Release | x64** (the only platform), and build. Output goes to `bin\x64\Release\`: `ScanToAlibre.dll`, `ScanToAlibre.adc`, `ScanToAlibre.ico` and `ScanCoreTests.exe`.

`addon\Alibre.props` finds the Alibre `Program` folder holding `AlibreX_64.tlb` and `AlibreAddOn_64.tlb`. To pin one, create `addon\ScanToAlibre.user.props` setting `AlibreProgramDir`, as in CAMForAlibre. The warnings C4278 (`SendMessage`) and C4192 (`IStream`) from the type library imports are expected.

## Install

Close Alibre Design, then run:

```
powershell -ExecutionPolicy Bypass -File deploy\install.ps1 [-Configuration Debug]
```

It asks for administrator rights, copies the add-on to `C:\ProgramData\Alibre AddOns\ScanToAlibre\` and registers it. Start Alibre again afterwards. `deploy\uninstall.ps1` removes it.
