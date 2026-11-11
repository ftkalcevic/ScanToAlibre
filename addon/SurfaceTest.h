// SurfaceTest.h : the Insert Test Surface command, which proves that a surface
// written by the add-on can be inserted into a part.
//
// It writes a bicubic hump (X 0 to 100, Y 0 to 80, Z about 20 to 30 mm) as
// test-surface.igs beside the log and inserts it with the 27.0.1 call
// IADPartSession::DesignSurfaces->InsertFromFile, then reports what came in.

#pragma once

#include <string>

void InsertTestSurface(const std::wstring& sessionId);
