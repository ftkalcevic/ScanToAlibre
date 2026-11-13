// ScanImport.cpp : Load.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "ScanImport.h"
#include "ScanDocument.h"
#include "StepDialog.h"
#include "ScanViewCommand.h"
#include "Log.h"

#include "ScanFile.h"

#include <chrono>

namespace
{
    std::wstring Widen(const std::string& text)
    {
        if (text.empty())
            return std::wstring();
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &wide[0], length);
        return wide;
    }

    std::string Narrow(const std::wstring& text)
    {
        if (text.empty())
            return std::string();
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                               nullptr, nullptr);
        std::string narrow(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &narrow[0], length, nullptr,
                            nullptr);
        return narrow;
    }

    double Milliseconds(std::chrono::steady_clock::time_point since)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
    }

    std::wstring g_lastFolder;

}

bool MarkPartChanged(const std::wstring& sessionId, const std::wstring& comment)
{
    // Alibre's API can't mark a part changed, so change a parameter of our own:
    // ScanToAlibre, a count that goes up by one each time the scan changes.
    // Found by trying (Frank's log): NewParameter fails inside our own transaction
    // ("Parameter transaction already open"), and PutValue fails outside one
    // ("No opened transaction found"), so create first, then open one to set it.
    IADParametersPtr parameters;
    bool opened = false;
    const wchar_t* step = L"getting the parameters";
    try
    {
        IADSessionPtr session = theApp.m_pRoot->GetSessions()->GetItem(_variant_t(sessionId.c_str()));
        if (session == nullptr)
            return false;
        parameters = session->GetParameters();

        step = L"finding the parameter";
        IADParameterPtr parameter;
        const long count = parameters->GetCount();
        for (long i = 0; i < count && parameter == nullptr; ++i)
        {
            IADParameterPtr item = parameters->GetItem(_variant_t(i));
            if (item != nullptr && wcscmp(static_cast<LPCWSTR>(item->GetName()), L"ScanToAlibre") == 0)
                parameter = item;
        }
        const bool created = (parameter == nullptr);
        if (created)
        {
            step = L"NewParameter";
            parameter = parameters->NewParameter(_bstr_t(L"ScanToAlibre"), ADParameterType_AD_COUNT);
        }

        step = L"OpenParameterTransaction";
        parameters->OpenParameterTransaction();
        opened = true;
        step = L"PutValue";
        const double value = created ? 1.0 : parameter->GetValue() + 1.0;
        parameter->PutValue(value);
        step = L"Putcomment";
        parameter->Putcomment(_bstr_t(comment.c_str()));
        step = L"CloseParameterTransaction";
        parameters->CloseParameterTransaction();
        opened = false;

        Log(L"[changed] %s parameter ScanToAlibre = %g", created ? L"created" : L"updated", value);
        RefreshAlibreButtons();
        return true;
    }
    catch (const _com_error& error)
    {
        const _bstr_t description = error.Description();
        Log(L"[changed] %s failed: hr=0x%08X %s", step, static_cast<unsigned>(error.Error()),
            description.length() > 0 ? static_cast<LPCWSTR>(description) : L"");
        if (opened)
        {
            try
            {
                parameters->CancelParameterTransaction();
            }
            catch (const _com_error&)
            {
            }
        }
        return false;
    }
}

std::wstring ScanDisplayName(const std::string& sourceName)
{
    const std::wstring path = Widen(sourceName);
    const size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

double MillimetresPerModelUnit(const std::wstring& sessionId)
{
    try
    {
        IADSessionPtr session = theApp.m_pRoot->GetSessions()->GetItem(_variant_t(sessionId.c_str()));
        IADDesignSessionPtr design = session;
        if (design != nullptr)
        {
            switch (design->GetDesignProperties()->GetModelUnits())
            {
            case ADUnits_AD_MILLIMETERS: return 1.0;
            case ADUnits_AD_CENTIMETERS: return 10.0;
            case ADUnits_AD_METERS:      return 1000.0;
            case ADUnits_AD_INCHES:      return 25.4;
            case ADUnits_AD_FEET:
            case ADUnits_AD_FEET_INCHES: return 304.8;
            default:                     break;
            }
        }
    }
    catch (const _com_error& error)
    {
        Log(L"[model] model units failed: hr=0x%08X", static_cast<unsigned>(error.Error()));
    }
    Log(L"[model] model units unknown, assuming centimetres");
    return 10.0;
}

bool LoadScan(const std::wstring& sessionId)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    const HWND owner = theApp.m_hAlibreWnd;

    // Replacing what the part has asks first.
    if (const ScanDocument* pExisting = FindScan(sessionId))
    {
        const std::wstring question = L"This part already has a scan, " + ScanDisplayName(pExisting->sourceName) +
                                      L". Replace it?";
        if (::MessageBoxW(owner, question.c_str(), L"Load Scan", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return false;
    }
    else if (FindUnreadData(sessionId) != nullptr)
    {
        if (::MessageBoxW(owner,
                          L"This part has scan data ScanToAlibre couldn't read (damaged, or from a newer "
                          L"version). Loading a scan replaces it when the part is saved. Carry on?",
                          L"Load Scan", MB_YESNO | MB_ICONWARNING) != IDYES)
            return false;
    }

    // A step dialog works on the scan that's being replaced, so it closes.
    CloseStepDialog();

    CWnd* pParent = (owner != nullptr) ? CWnd::FromHandle(owner) : nullptr;
    CFileDialog dialog(TRUE, L"stl", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                       L"Scans (*.stl;*.ply)|*.stl;*.ply|STL meshes (*.stl)|*.stl|PLY meshes and point clouds (*.ply)|*.ply|"
                       L"All files (*.*)|*.*||",
                       pParent);
    dialog.m_ofn.lpstrTitle = L"Load Scan";
    if (!g_lastFolder.empty())
        dialog.m_ofn.lpstrInitialDir = g_lastFolder.c_str();
    if (dialog.DoModal() != IDOK)
        return false;

    const std::wstring path = static_cast<LPCWSTR>(dialog.GetPathName());
    g_lastFolder = std::filesystem::path(path).parent_path().wstring();

    CWaitCursor wait;
    const auto start = std::chrono::steady_clock::now();

    scancore::Mesh mesh;
    scancore::ScanFileInfo info;
    std::string message;
    if (!scancore::ReadScanFile(path, mesh, info, message))
    {
        Log(L"[scan] %s: %s", path.c_str(), Widen(message).c_str());
        ::MessageBoxW(owner, Widen(message).c_str(), L"Load Scan",
                      MB_OK | (info.pointCloud ? MB_ICONINFORMATION : MB_ICONERROR));
        return false;
    }
    if (info.pointCloud)
        Log(L"[scan] read %s: %s, %zu points meshed on a %.4f mm grid into %zu triangles, in %.0f ms", path.c_str(),
            Widen(info.description).c_str(), info.points, info.cellSize, mesh.triangles.size(), Milliseconds(start));
    else
        Log(L"[scan] read %s: %s, %zu triangles or faces in the file, %zu degenerate left out, normals %d, in %.0f ms",
            path.c_str(), Widen(info.description).c_str(), info.fileTriangles, info.degenerate,
            info.hasNormals ? 1 : 0, Milliseconds(start));

    ScanDocument* pScan = AddScan(sessionId, MakeScanDocument(std::move(mesh), Narrow(path)));
    ForgetUnreadData(sessionId);
    pScan->unsaved = true;
    wchar_t comment[400];
    swprintf_s(comment, L"Scan %s, %zu triangles. Changed by ScanToAlibre so the part saves its scan.",
               ScanDisplayName(pScan->sourceName).c_str(), pScan->mesh.triangles.size());
    MarkPartChanged(sessionId, comment);
    Log(L"[scan] loaded; it's saved inside the part when the part is saved");
    return true;
}
