// SurfaceTest.cpp : the Insert Test Surface command.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "SurfaceTest.h"
#include "Log.h"

#include "BSplineSurface.h"
#include "FileIO.h"
#include "Iges.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    std::string Timestamp()
    {
        SYSTEMTIME now = {};
        GetLocalTime(&now);
        char text[32] = {};
        sprintf_s(text, "%04u%02u%02u.%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour,
                  now.wMinute, now.wSecond);
        return text;
    }

    std::wstring Widen(const std::string& text)
    {
        if (text.empty())
            return std::wstring();
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &wide[0], length);
        return wide;
    }

    void Report(const CString& text, UINT icon)
    {
        CWnd* pParent = (theApp.m_hAlibreWnd != nullptr) ? CWnd::FromHandle(theApp.m_hAlibreWnd) : nullptr;
        ::MessageBoxW(pParent != nullptr ? pParent->GetSafeHwnd() : nullptr, text, L"Insert Test Surface",
                      MB_OK | icon);
    }
}

void InsertTestSurface(const std::wstring& sessionId)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    // Write the surface.
    const scancore::BSplineSurface surface = scancore::TestHumpSurface();
    std::string message;
    if (!scancore::ValidateSurface(surface, message))
    {
        Log(L"[surface] test surface invalid: %s", Widen(message).c_str());
        Report(L"The test surface is invalid: " + CString(Widen(message).c_str()), MB_ICONERROR);
        return;
    }

    const std::wstring path = LogFolderPath() + L"test-surface.igs";
    scancore::IgesHeader header;
    header.fileName = "test-surface.igs";
    header.timestamp = Timestamp();
    if (!scancore::WriteFileAtomically(path, scancore::WriteIgesSurface(surface, header), message))
    {
        Log(L"[surface] %s", Widen(message).c_str());
        Report(CString(Widen(message).c_str()), MB_ICONERROR);
        return;
    }
    Log(L"[surface] wrote %s", path.c_str());

    // Insert it.
    try
    {
        if (theApp.m_pRoot == nullptr)
            return;

        IADSessionPtr session = theApp.m_pRoot->GetSessions()->GetItem(_variant_t(sessionId.c_str()));
        IADPartSessionPtr part = session;
        if (part == nullptr)
        {
            Report(L"Open a part to insert the test surface into.", MB_ICONINFORMATION);
            return;
        }

        IADDesignSurfacesPtr surfaces = part->GetDesignSurfaces();
        const long before = surfaces->GetCount();
        Log(L"[surface] design surfaces before: %ld", before);

        IObjectCollectorPtr inserted = surfaces->InsertFromFile(
            _bstr_t(path.c_str()), ADFaceProcessingType_AD_STITCH_ADJOINING_FACES,
            VARIANT_TRUE /* heal */, VARIANT_FALSE /* make tolerant */, ADUnits_AD_MILLIMETERS);

        const long returned = (inserted != nullptr) ? inserted->GetCount() : -1;
        const long after = part->GetDesignSurfaces()->GetCount();
        Log(L"[surface] InsertFromFile returned %ld objects; design surfaces after: %ld", returned, after);

        CString details;
        for (long i = 0; i < after; ++i)
        {
            IADDesignSurfacePtr designSurface = part->GetDesignSurfaces()->GetItem(_variant_t(i));
            if (designSurface == nullptr)
                continue;
            const _bstr_t name = designSurface->GetName();
            const long faces = designSurface->GetFaces()->GetCount();
            const long edges = designSurface->GetEdges()->GetCount();
            Log(L"[surface]   %ld: %s, %ld faces, %ld edges", i, static_cast<LPCWSTR>(name), faces, edges);

            CString line;
            line.Format(L"\n  %s: %ld face(s), %ld edge(s)", static_cast<LPCWSTR>(name), faces, edges);
            details += line;
        }

        CString text;
        text.Format(L"InsertFromFile returned %ld object(s).\nDesign surfaces: %ld before, %ld after.%s\n\n"
                    L"File: %s",
                    returned, before, after, static_cast<LPCWSTR>(details), path.c_str());
        Report(text, (after > before) ? MB_ICONINFORMATION : MB_ICONWARNING);
    }
    catch (const _com_error& error)
    {
        const _bstr_t description = error.Description();
        Log(L"[surface] failed: hr=0x%08X %s", static_cast<unsigned>(error.Error()),
            description.length() > 0 ? static_cast<LPCWSTR>(description) : L"");

        CString text;
        text.Format(L"Inserting the surface failed (hr=0x%08X).\n%s\n\nFile: %s",
                    static_cast<unsigned>(error.Error()),
                    description.length() > 0 ? static_cast<LPCWSTR>(description) : L"", path.c_str());
        Report(text, MB_ICONERROR);
    }
}
