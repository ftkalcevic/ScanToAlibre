// ScanImport.h : Load, the first step of the workflow. It reads a scan file
// (STL, or PLY with faces) into the part's workspace; the scan is saved inside
// the part when the part is saved.

#pragma once

#include <string>

// Asks for a scan file and loads it for this part session, asking first if the
// part already has a scan. Returns false if nothing was loaded (cancelled, or
// the file couldn't be used, which is shown).
bool LoadScan(const std::wstring& sessionId);

// Millimetres in one of the part's model units (cm unless the part says otherwise).
double MillimetresPerModelUnit(const std::wstring& sessionId);

// Makes Alibre see the part as changed, so it saves the scan with it: the API
// can't mark a part changed, so this changes a count parameter named
// ScanToAlibre. Returns false if that failed (logged).
bool MarkPartChanged(const std::wstring& sessionId, const std::wstring& comment);

// The file name, from a UTF-8 path, for messages.
std::wstring ScanDisplayName(const std::string& sourceName);
