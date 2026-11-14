// SketchWriter.h : makes an Alibre sketch from a section through the scan.
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

#pragma once

#include <string>

struct ScanDocument;

struct SketchReport
{
    std::wstring sketchName;
    std::wstring planeName;         // what the sketch is on
    size_t lines = 0, arcs = 0, circles = 0, reference = 0;
    size_t constraints = 0, constraintsRefused = 0;
    size_t dimensions = 0, dimensionsRefused = 0;
    double largestDeviation = 0.0;  // mm
    double seconds = 0.0;
};

// Makes the sketch for the scan's section. Returns false with a message if it
// couldn't be made (anything already made stays, and is logged).
bool CreateSectionSketch(const std::wstring& sessionId, ScanDocument& scan, SketchReport& report,
                         std::wstring& message);
