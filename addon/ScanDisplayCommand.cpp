// ScanDisplayCommand.cpp : Show Scan, a toggle that draws the scan while it's on.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "ScanDisplayCommand.h"
#include "ScanDocument.h"
#include "ScanViewCommand.h"
#include "Log.h"

#include <map>

namespace
{
    std::map<std::wstring, CScanDisplayCommand*> g_displays;       // each holds a reference

    __int64 AddSegment(IADAddOnCanvasDisplay* pCanvas, const wchar_t* name, long red, long green, long blue)
    {
        __int64 segment = 0;
        pCanvas->AddSubSegment(0, _bstr_t(name), &segment);
        pCanvas->SetSegmentColor(segment, red, green, blue, 255);
        pCanvas->ToggleForegroundRendering(segment, VARIANT_FALSE);
        return segment;
    }
}

CScanDisplayCommand::CScanDisplayCommand(const std::wstring& sessionId)
    : CCommandBase(L"scan display")
    , m_sessionId(sessionId)
{
}

STDMETHODIMP CScanDisplayCommand::putref_CommandSite(struct IADAddOnCommandSite* pSite)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    CCommandBase::putref_CommandSite(pSite);
    if (pSite != nullptr)
    {
        const HRESULT hr = pSite->EnableOn3DRenderCallback(VARIANT_TRUE);
        Log(L"[display] Show Scan on; EnableOn3DRenderCallback hr=0x%08X", static_cast<unsigned>(hr));
    }
    return S_OK;
}

STDMETHODIMP CScanDisplayCommand::IsTwoWayToggle(VARIANT_BOOL* pValue)
{
    if (pValue == nullptr)
        return E_POINTER;
    *pValue = VARIANT_TRUE;
    return S_OK;
}

STDMETHODIMP CScanDisplayCommand::AddTab(VARIANT_BOOL* pValue)
{
    if (pValue == nullptr)
        return E_POINTER;
    *pValue = VARIANT_TRUE;
    return S_OK;
}

STDMETHODIMP CScanDisplayCommand::get_TabName(BSTR* pName)
{
    if (pName == nullptr)
        return E_POINTER;
    *pName = _bstr_t(L"Scan").Detach();
    return S_OK;
}

STDMETHODIMP CScanDisplayCommand::OnShowUI(__int64 hWnd)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    const HWND parent = reinterpret_cast<HWND>(hWnd);
    if (m_label == nullptr && parent != nullptr)
    {
        m_label = ::CreateWindowExW(0, L"STATIC",
                                    L"The scan is shown, and stays shown through other commands. Click Show Scan "
                                    L"on the ScanToAlibre ribbon tab to hide it.",
                                    WS_CHILD | WS_VISIBLE, 8, 8, 240, 64, parent, nullptr, nullptr, nullptr);
        if (m_label != nullptr)
            ::SendMessageW(m_label, WM_SETFONT, reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    Log(L"[display] OnShowUI: tab window %p, text %p", parent, m_label);
    return S_OK;
}

STDMETHODIMP CScanDisplayCommand::OnComplete()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    Log(L"[display] OnComplete");
    if (m_site != nullptr)
        m_site->InvalidateCanvas();
    return S_OK;
}

STDMETHODIMP CScanDisplayCommand::On3DRender()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    ScanDocument* pScan = FindScan(m_sessionId);
    if (m_site == nullptr || pScan == nullptr)
        return S_OK;

    IUnknown* pContext = nullptr;
    const HRESULT hr = m_site->Begin3DDisplay(VARIANT_TRUE, &pContext);
    if (++m_renders <= 3)
        Log(L"[display] On3DRender %d: Begin3DDisplay hr=0x%08X", m_renders, static_cast<unsigned>(hr));
    if (FAILED(hr))
        return S_OK;

    if (pContext != nullptr)
    {
        IADAddOnCanvasDisplayPtr canvas;
        pContext->QueryInterface(__uuidof(IADAddOnCanvasDisplay), reinterpret_cast<void**>(&canvas));
        pContext->Release();
        if (canvas != nullptr)
        {
            try
            {
                if (const ScanDocument::DrawArrays* pArrays = pScan->Arrays())
                {
                    // Hidden by the model, like a part, so features built on it show.
                    const __int64 segment = AddSegment(canvas, L"Scan", 150, 175, 205);
                    SAFEARRAY* pVertices = pArrays->vertices;
                    SAFEARRAY* pNormals = pArrays->normals;
                    SAFEARRAY* pFaces = pArrays->faces;
                    __int64 result = 0;
                    canvas->DrawMesh(segment, &pVertices, &pNormals, &pFaces, &result);
                }
            }
            catch (const _com_error& error)
            {
                Log(L"[display] drawing failed: hr=0x%08X", static_cast<unsigned>(error.Error()));
            }
        }
    }
    m_site->End3DDisplay();
    return S_OK;
}

STDMETHODIMP CScanDisplayCommand::OnTerminate()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    Log(L"[display] Show Scan off (OnTerminate after %d renders)", m_renders);
    m_site = nullptr;
    if (m_label != nullptr && ::IsWindow(m_label))
        ::DestroyWindow(m_label);
    m_label = nullptr;

    const auto found = g_displays.find(m_sessionId);
    if (found != g_displays.end() && found->second == this)
    {
        g_displays.erase(found);
        RefreshScanViews();     // the scan view draws the scan again
        Release();
    }
    return S_OK;
}

CScanDisplayCommand* FindScanDisplay(const std::wstring& sessionId)
{
    const auto found = g_displays.find(sessionId);
    return (found != g_displays.end()) ? found->second : nullptr;
}

CScanDisplayCommand* ToggleScanDisplay(const std::wstring& sessionId)
{
    if (CScanDisplayCommand* pRunning = FindScanDisplay(sessionId))
        return pRunning;
    CScanDisplayCommand* pDisplay = new CScanDisplayCommand(sessionId);
    pDisplay->AddRef();     // the map's
    g_displays[sessionId] = pDisplay;
    return pDisplay;
}

bool ScanDisplayOn(const std::wstring& sessionId)
{
    const CScanDisplayCommand* pDisplay = FindScanDisplay(sessionId);
    return pDisplay != nullptr && pDisplay->Site() != nullptr;
}

void RefreshScanDisplays()
{
    for (const auto& entry : g_displays)
        if (entry.second->Site() != nullptr)
            entry.second->Site()->InvalidateCanvas();
}

void ReleaseScanDisplays()
{
    std::map<std::wstring, CScanDisplayCommand*> displays;
    displays.swap(g_displays);
    for (auto& entry : displays)
        entry.second->Release();
}
