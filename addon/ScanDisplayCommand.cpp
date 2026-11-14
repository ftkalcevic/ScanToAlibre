// ScanDisplayCommand.cpp : Show Scan, a toggle that draws the scan while it's on,
// and its Scan tab: a tree of the scan's fitted geometry.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "ScanDisplayCommand.h"
#include "ScanDocument.h"
#include "ScanImport.h"
#include "ScanViewCommand.h"
#include "OrientDialog.h"
#include "Log.h"
#include "resource.h"

#include <map>
#include <vector>

using scancore::Feature;

namespace
{
    std::map<std::wstring, CScanDisplayCommand*> g_displays;       // each holds a reference

    const UINT_PTR kResizeTimer = 1;
    const UINT kResizeMilliseconds = 250;

    __int64 AddSegment(IADAddOnCanvasDisplay* pCanvas, const wchar_t* name, long red, long green, long blue)
    {
        __int64 segment = 0;
        pCanvas->AddSubSegment(0, _bstr_t(name), &segment);
        pCanvas->SetSegmentColor(segment, red, green, blue, 255);
        pCanvas->ToggleForegroundRendering(segment, VARIANT_FALSE);
        return segment;
    }

    std::wstring WideText(const std::string& text)
    {
        if (text.empty())
            return std::wstring();
        const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(static_cast<size_t>(length), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
        return wide;
    }

    std::string Utf8Text(const std::wstring& text)
    {
        if (text.empty())
            return std::string();
        const int length =
            ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string narrow(static_cast<size_t>(length), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), length, nullptr,
                              nullptr);
        return narrow;
    }

    // What the tree shows of each fitted feature, to tell when it must be filled again.
    std::vector<std::string> TreeKey(const ScanDocument& scan)
    {
        std::vector<std::string> key;
        for (const Feature& f : scan.fitted)
            key.push_back(std::to_string(static_cast<int>(f.kind)) + f.name);
        return key;
    }
}

// The Scan tab's window: a tree filling it. Item data is the fitted index + 1,
// 0 for the "Scan Geometry" node.
class CScanTabWnd : public CWnd
{
public:
    explicit CScanTabWnd(const std::wstring& sessionId)
        : m_sessionId(sessionId)
    {
    }

    bool CreateInTab(HWND hParent)
    {
        CRect client;
        ::GetClientRect(hParent, &client);
        const CString windowClass = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW),
                                                        reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE + 1)));
        return CreateEx(0, windowClass, L"Scan", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, client,
                        CWnd::FromHandle(hParent), 1) != FALSE;
    }

    // Fills the tree again if the fitted geometry changed, and selects the shown one.
    void Fill(bool force = false)
    {
        if (m_tree.GetSafeHwnd() == nullptr)
            return;
        const ScanDocument* pScan = FindScan(m_sessionId);
        const std::vector<std::string> key = pScan != nullptr ? TreeKey(*pScan) : std::vector<std::string>();
        m_filling = true;
        if (force || key != m_key || m_root == nullptr)
        {
            m_key = key;
            m_tree.DeleteAllItems();
            m_root = m_tree.InsertItem(L"Scan Geometry");
            m_tree.SetItemData(m_root, 0);
            if (pScan != nullptr)
            {
                for (size_t i = 0; i < pScan->fitted.size(); ++i)
                {
                    const HTREEITEM hItem = m_tree.InsertItem(WideText(pScan->fitted[i].name).c_str(), m_root);
                    m_tree.SetItemData(hItem, static_cast<DWORD_PTR>(i + 1));
                }
            }
            m_tree.Expand(m_root, TVE_EXPAND);
        }
        // The shown feature selected (or the node when none), without redrawing.
        HTREEITEM hShown = m_root;
        if (pScan != nullptr && pScan->shownFitted >= 0)
            for (HTREEITEM h = m_tree.GetChildItem(m_root); h != nullptr; h = m_tree.GetNextSiblingItem(h))
                if (static_cast<int>(m_tree.GetItemData(h)) == pScan->shownFitted + 1)
                    hShown = h;
        if (m_tree.GetSelectedItem() != hShown)
            m_tree.SelectItem(hShown);
        m_filling = false;
    }

protected:
    int OnCreate(LPCREATESTRUCT pCreate)
    {
        if (CWnd::OnCreate(pCreate) == -1)
            return -1;
        m_tree.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
                          TVS_SHOWSELALWAYS | TVS_EDITLABELS,
                      CRect(0, 0, 10, 10), this, IDC_SCAN_TREE);
        m_tree.SetFont(CFont::FromHandle(static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT))));
        Fill(true);
        // Alibre sizes the tab's window, not ours, so ours follows it.
        SetTimer(kResizeTimer, kResizeMilliseconds, nullptr);
        return 0;
    }

    void OnDestroy()
    {
        KillTimer(kResizeTimer);
        CWnd::OnDestroy();
    }

    void PostNcDestroy() override
    {
        delete this;
    }

    void OnSize(UINT type, int cx, int cy)
    {
        CWnd::OnSize(type, cx, cy);
        if (m_tree.GetSafeHwnd() != nullptr)
            m_tree.MoveWindow(0, 0, cx, cy);
    }

    void OnTimer(UINT_PTR id)
    {
        if (id != kResizeTimer)
        {
            CWnd::OnTimer(id);
            return;
        }
        CWnd* pParent = GetParent();
        if (pParent == nullptr)
            return;
        CRect parentClient, own;
        pParent->GetClientRect(&parentClient);
        GetClientRect(&own);
        if (parentClient.Width() != own.Width() || parentClient.Height() != own.Height())
            MoveWindow(0, 0, parentClient.Width(), parentClient.Height());
    }

    // The fitted index of an item, or -1 for the node (or nothing).
    int FittedOf(HTREEITEM hItem) const
    {
        return hItem != nullptr ? static_cast<int>(m_tree.GetItemData(hItem)) - 1 : -1;
    }

    void OnSelectionChanged(NMHDR* pNotify, LRESULT* pResult)
    {
        *pResult = 0;
        if (m_filling)
            return;
        ScanDocument* pScan = FindScan(m_sessionId);
        if (pScan == nullptr)
            return;
        const int index = FittedOf(reinterpret_cast<NMTREEVIEW*>(pNotify)->itemNew.hItem);
        pScan->shownFitted = (index >= 0 && index < static_cast<int>(pScan->fitted.size())) ? index : -1;
        RefreshScanDisplays();
    }

    void OnRightClick(NMHDR* /*pNotify*/, LRESULT* pResult)
    {
        *pResult = 1;
        CPoint at;
        ::GetCursorPos(&at);
        CPoint inTree = at;
        m_tree.ScreenToClient(&inTree);
        const HTREEITEM hItem = m_tree.HitTest(inTree);
        if (hItem == nullptr || FittedOf(hItem) < 0)
            return;
        m_tree.SelectItem(hItem);
        CMenu menu;
        menu.CreatePopupMenu();
        menu.AppendMenu(MF_STRING, ID_SCAN_RENAME, L"Rename");
        menu.AppendMenu(MF_STRING, ID_SCAN_DELETE, L"Delete");
        const UINT command =
            menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, at.x, at.y, this);
        if (command == ID_SCAN_RENAME)
            m_tree.EditLabel(hItem);
        else if (command == ID_SCAN_DELETE)
            DeleteItem(hItem);
    }

    void OnKeyDown(NMHDR* pNotify, LRESULT* pResult)
    {
        *pResult = 0;
        const HTREEITEM hItem = m_tree.GetSelectedItem();
        if (FittedOf(hItem) < 0)
            return;
        const WORD key = reinterpret_cast<NMTVKEYDOWN*>(pNotify)->wVKey;
        if (key == VK_DELETE)
            DeleteItem(hItem);
        else if (key == VK_F2)
            m_tree.EditLabel(hItem);
    }

    void DeleteItem(HTREEITEM hItem)
    {
        ScanDocument* pScan = FindScan(m_sessionId);
        const int index = FittedOf(hItem);
        if (pScan == nullptr || index < 0 || index >= static_cast<int>(pScan->fitted.size()))
            return;
        const std::string name = pScan->fitted[index].name;
        pScan->DeleteFitted(static_cast<size_t>(index));
        MarkPartChanged(m_sessionId, L"Plane or axis deleted. Changed by ScanToAlibre so the part saves it.");
        Log(L"[scan tab] deleted %S", name.c_str());
        Changed();
    }

    void OnBeginRename(NMHDR* pNotify, LRESULT* pResult)
    {
        // Non-zero cancels the edit: only the fitted geometry is renamed.
        *pResult = FittedOf(reinterpret_cast<NMTVDISPINFO*>(pNotify)->item.hItem) >= 0 ? 0 : 1;
    }

    void OnEndRename(NMHDR* pNotify, LRESULT* pResult)
    {
        // Zero leaves the label as it was; it's set below once the name is taken.
        *pResult = 0;
        const NMTVDISPINFO* pInfo = reinterpret_cast<NMTVDISPINFO*>(pNotify);
        ScanDocument* pScan = FindScan(m_sessionId);
        const int index = FittedOf(pInfo->item.hItem);
        if (pInfo->item.pszText == nullptr || pScan == nullptr || index < 0 ||
            index >= static_cast<int>(pScan->fitted.size()))
            return;     // cancelled
        CString typed(pInfo->item.pszText);
        typed.Trim();
        const std::string name = Utf8Text(static_cast<LPCWSTR>(typed));
        const std::string old = pScan->fitted[index].name;
        if (!pScan->RenameFitted(static_cast<size_t>(index), name))
            return;     // empty or unchanged
        MarkPartChanged(m_sessionId, L"Plane or axis renamed. Changed by ScanToAlibre so the part saves it.");
        Log(L"[scan tab] renamed %S to %S", old.c_str(), name.c_str());
        // The label set here and the key matched, so the tree isn't filled again
        // inside the tree's own notice.
        m_tree.SetItemText(pInfo->item.hItem, typed);
        m_key = TreeKey(*pScan);
        RefreshScanViews();
        OrientViewChanged();
    }

    // After a delete or rename: the tree, the view and Orient's dialog show it.
    void Changed()
    {
        Fill();
        RefreshScanViews();
        OrientViewChanged();
    }

    DECLARE_MESSAGE_MAP()

private:
    std::wstring m_sessionId;
    CTreeCtrl m_tree;
    HTREEITEM m_root = nullptr;
    std::vector<std::string> m_key;
    bool m_filling = false;     // selection changes made while filling don't redraw
};

BEGIN_MESSAGE_MAP(CScanTabWnd, CWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_SIZE()
    ON_WM_TIMER()
    ON_NOTIFY(TVN_SELCHANGED, IDC_SCAN_TREE, &CScanTabWnd::OnSelectionChanged)
    ON_NOTIFY(NM_RCLICK, IDC_SCAN_TREE, &CScanTabWnd::OnRightClick)
    ON_NOTIFY(TVN_KEYDOWN, IDC_SCAN_TREE, &CScanTabWnd::OnKeyDown)
    ON_NOTIFY(TVN_BEGINLABELEDIT, IDC_SCAN_TREE, &CScanTabWnd::OnBeginRename)
    ON_NOTIFY(TVN_ENDLABELEDIT, IDC_SCAN_TREE, &CScanTabWnd::OnEndRename)
END_MESSAGE_MAP()

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
    if (m_pWnd == nullptr && parent != nullptr)
    {
        m_pWnd = new CScanTabWnd(m_sessionId);
        if (!m_pWnd->CreateInTab(parent))
        {
            Log(L"[display] couldn't make the Scan tab's window");
            // If creation got far enough, PostNcDestroy deleted it.
            if (m_pWnd->GetSafeHwnd() == nullptr)
                delete m_pWnd;
            m_pWnd = nullptr;
        }
    }
    Log(L"[display] OnShowUI: tab window %p, tree window %p", parent,
        m_pWnd != nullptr ? m_pWnd->GetSafeHwnd() : nullptr);
    return S_OK;
}

void CScanDisplayCommand::UpdateTree()
{
    if (m_pWnd != nullptr && ::IsWindow(m_pWnd->GetSafeHwnd()))
        m_pWnd->Fill();
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
                // The geometry chosen in the Scan tab's tree, yellow over the model.
                if (pScan->shownFitted >= 0 && pScan->shownFitted < static_cast<int>(pScan->fitted.size()))
                {
                    static const long kShown[3] = { 255, 215, 0 };
                    DrawScanFeature(canvas, *pScan, pScan->fitted[pScan->shownFitted], kShown, true);
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
    if (m_pWnd != nullptr && ::IsWindow(m_pWnd->GetSafeHwnd()))
        m_pWnd->DestroyWindow();       // deletes itself
    m_pWnd = nullptr;

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
    {
        entry.second->UpdateTree();
        if (entry.second->Site() != nullptr)
            entry.second->Site()->InvalidateCanvas();
    }
}

void ReleaseScanDisplays()
{
    std::map<std::wstring, CScanDisplayCommand*> displays;
    displays.swap(g_displays);
    for (auto& entry : displays)
        entry.second->Release();
}
