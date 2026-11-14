// SketchWriter.h : makes Alibre sketches from sections through the scan.
//
// The sketch goes on the chosen plane: Alibre's design plane or flat face as
// it is, a design plane made at the offset from it, or, for one of the scan's
// planes, a design plane made through three points on it (a sketch needs
// Alibre geometry). The sketch's own axes are read back with
// MapFromSketchToWorld and the scan is sliced and fitted again in them, so
// horizontal and vertical are the sketch's.
//
// Three edits, each inside BeginChange and EndChange (MCPForAlibre's pattern):
// the fitted lines, arcs and circles; then their ends made coincident, the
// design intent as constraints and the rounded radii as dimensions, finding the
// figures again by ID; then the slice itself as construction lines.
//
// Section Series writes a sketch per slice the same way, on a design plane per
// offset, with the matched splines or lines and arcs moved into the sketch's
// axes, then a 3D sketch per guide curve.

#pragma once

#include <string>

struct ScanDocument;

struct SketchReport
{
    std::wstring sketchName;
    std::wstring planeName;         // what the sketch is on
    size_t lines = 0, arcs = 0, circles = 0, reference = 0;
    size_t rawLines = 0;            // the raw slice as ordinary lines (Create Raw Sketch without Fitted)
    size_t constraints = 0, constraintsRefused = 0;
    size_t dimensions = 0, dimensionsRefused = 0;
    double largestDeviation = 0.0;  // mm
    double seconds = 0.0;
};

// Makes the sketch for the scan's section. Returns false with a message if it
// couldn't be made (anything already made stays, and is logged).
bool CreateSectionSketch(const std::wstring& sessionId, ScanDocument& scan, SketchReport& report,
                         std::wstring& message);

struct SeriesReport
{
    int series = 0;                 // "Scan Series n"
    std::wstring firstSketch, lastSketch;
    size_t sketches = 0, empty = 0; // empty: planes that cut nothing, so no sketch
    size_t splines = 0, splinesRefused = 0;
    size_t lines = 0, arcs = 0, circles = 0, reference = 0, rawLines = 0;
    size_t constraints = 0, constraintsRefused = 0;
    size_t dimensions = 0, dimensionsRefused = 0;
    size_t guides = 0, guidesRefused = 0;
    size_t lofts = 0, loftsWithoutGuides = 0, loftsRefused = 0;
    std::wstring loftError;         // the first loft's refusal
    double seconds = 0.0;
};

// Makes Section Series' sketches: "Scan Series n.k" on "Scan Series n.k plane"
// for each plane that cuts the scan, and with guide curves a 3D sketch
// "Scan Series n guide c.g" for each. With Create Loft (options.loft), then a
// loft boss "Scan Series n loft r" through each run's sketches in order with
// its guides (tried again without them if Alibre refuses). Returns false with a message if it
// stopped (anything already made stays, and is logged).
bool CreateSeriesSketches(const std::wstring& sessionId, ScanDocument& scan, SeriesReport& report,
                          std::wstring& message);
