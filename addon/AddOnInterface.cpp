// AddOnInterface.cpp : IAlibreAddOn implementation for ScanToAlibre.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "AddOnInterface.h"
#include "AboutDlg.h"
#include "ScanDocument.h"
#include "ScanImport.h"
#include "ScanViewCommand.h"
#include "ScanData.h"
#include "RefineDialog.h"
#include "OrientDialog.h"
#include "ScanDisplayCommand.h"
#include "SurfaceTest.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CAddOnInterface::CAddOnInterface()
    : m_nRefCount(0)
{
}

CAddOnInterface::~CAddOnInterface()
{
}

/////////////////////////////////////////////////////////////////////////////
// IUnknown

STDMETHODIMP CAddOnInterface::QueryInterface(REFIID riid, void** ppObj)
{
    if (ppObj == nullptr)
        return E_POINTER;

    if (riid == IID_IUnknown || riid == IID_IDispatch || riid == __uuidof(IAlibreAddOn))
    {
        *ppObj = static_cast<IAlibreAddOn*>(this);
        AddRef();
        return S_OK;
    }

    *ppObj = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CAddOnInterface::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&m_nRefCount));
}

STDMETHODIMP_(ULONG) CAddOnInterface::Release()
{
    long count = InterlockedDecrement(&m_nRefCount);
    if (count == 0)
        delete this;
    return static_cast<ULONG>(count);
}

/////////////////////////////////////////////////////////////////////////////
// IDispatch (not used by Alibre - vtable only)

STDMETHODIMP CAddOnInterface::GetTypeInfoCount(UINT* pctinfo)
{
    if (pctinfo == nullptr)
        return E_POINTER;
    *pctinfo = 0;
    return S_OK;
}

STDMETHODIMP CAddOnInterface::GetTypeInfo(UINT /*iTInfo*/, LCID /*lcid*/, ITypeInfo** ppTInfo)
{
    if (ppTInfo != nullptr)
        *ppTInfo = nullptr;
    return E_NOTIMPL;
}

STDMETHODIMP CAddOnInterface::GetIDsOfNames(REFIID /*riid*/, LPOLESTR* /*rgszNames*/,
                                            UINT /*cNames*/, LCID /*lcid*/, DISPID* /*rgDispId*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP CAddOnInterface::Invoke(DISPID /*dispIdMember*/, REFIID /*riid*/, LCID /*lcid*/,
                                     WORD /*wFlags*/, DISPPARAMS* /*pDispParams*/,
                                     VARIANT* /*pVarResult*/, EXCEPINFO* /*pExcepInfo*/,
                                     UINT* /*puArgErr*/)
{
    return E_NOTIMPL;
}

/////////////////////////////////////////////////////////////////////////////
// IAlibreAddOn - menu structure
//
// Alibre also queries ids it does not own, so every [out] parameter is
// initialised before anything else.

STDMETHODIMP CAddOnInterface::get_RootMenuItem(long* pRootMenuID)
{
    if (pRootMenuID == nullptr)
        return E_POINTER;
    *pRootMenuID = MENU_ID_ROOT;
    return S_OK;
}

STDMETHODIMP CAddOnInterface::HasSubMenus(long menuID, VARIANT_BOOL* pHasSubMenus)
{
    if (pHasSubMenus == nullptr)
        return E_POINTER;
    *pHasSubMenus = (menuID == MENU_ID_ROOT) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

STDMETHODIMP CAddOnInterface::SubMenuItems(long menuID, SAFEARRAY** pSubMenuIDs)
{
    if (pSubMenuIDs == nullptr)
        return E_POINTER;
    *pSubMenuIDs = nullptr;

    if (menuID != MENU_ID_ROOT)
        return S_OK;

    // The workflow, in order, then Show Scan and About. Insert Test Surface proved
    // surface insertion and is left off (SurfaceTest still has it).
    const long children[] = { MENU_ID_LOAD, MENU_ID_REFINE, MENU_ID_ORIENT, MENU_ID_SHOW_SCAN, MENU_ID_ABOUT };
    const ULONG count = static_cast<ULONG>(sizeof(children) / sizeof(children[0]));

    // The caller takes ownership of the array.
    SAFEARRAY* psa = SafeArrayCreateVector(VT_I4, 0, count);
    if (psa == nullptr)
        return E_OUTOFMEMORY;

    long* pData = nullptr;
    HRESULT hr = SafeArrayAccessData(psa, reinterpret_cast<void**>(&pData));
    if (FAILED(hr))
    {
        SafeArrayDestroy(psa);
        return hr;
    }
    for (ULONG i = 0; i < count; ++i)
        pData[i] = children[i];
    SafeArrayUnaccessData(psa);

    *pSubMenuIDs = psa;
    return S_OK;
}

STDMETHODIMP CAddOnInterface::MenuItemText(long menuID, BSTR* pMenuDisplayText)
{
    if (pMenuDisplayText == nullptr)
        return E_POINTER;
    *pMenuDisplayText = nullptr;

    switch (menuID)
    {
    case MENU_ID_ROOT:         *pMenuDisplayText = _bstr_t(L"ScanToAlibre").Detach(); break;
    case MENU_ID_LOAD:         *pMenuDisplayText = _bstr_t(L"1 Load").Detach(); break;
    case MENU_ID_REFINE:          *pMenuDisplayText = _bstr_t(L"2 Refine").Detach(); break;
    case MENU_ID_ORIENT:       *pMenuDisplayText = _bstr_t(L"3 Orient").Detach(); break;
    case MENU_ID_SHOW_SCAN:    *pMenuDisplayText = _bstr_t(L"Show Scan").Detach(); break;
    case MENU_ID_TEST_SURFACE: *pMenuDisplayText = _bstr_t(L"Insert Test Surface").Detach(); break;
    case MENU_ID_ABOUT:  *pMenuDisplayText = _bstr_t(L"About").Detach();    break;
    default: break;
    }
    return S_OK;
}

STDMETHODIMP CAddOnInterface::MenuItemState(long menuID, BSTR sessionIdentifier,
                                            enum ADDONMenuStates* pType)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if (pType == nullptr)
        return E_POINTER;

    // The root must be enabled too, or the whole drop-down/tab greys out.
    // The About box works in every workspace (including Home, which passes an
    // empty session id). The rest work on parts only, on a supported Alibre, and
    // Refine, Orient and Show Scan once the part has a scan.
    const bool part = IsAlibreVersionSupported() && IsPartSession(sessionIdentifier);
    if (menuID == MENU_ID_ROOT || menuID == MENU_ID_ABOUT)
        *pType = ADDONMenuStates_ADDON_MENU_ENABLED;
    else if ((menuID == MENU_ID_TEST_SURFACE || menuID == MENU_ID_LOAD) && part)
        *pType = ADDONMenuStates_ADDON_MENU_ENABLED;
    else if (menuID == MENU_ID_SHOW_SCAN && part && FindScan(sessionIdentifier) != nullptr)
        *pType = ScanDisplayOn(sessionIdentifier) ? ADDONMenuStates_ADDON_MENU_CHECKED
                                                  : ADDONMenuStates_ADDON_MENU_UNCHECKED;
    else if ((menuID == MENU_ID_REFINE || menuID == MENU_ID_ORIENT) && part && FindScan(sessionIdentifier) != nullptr)
        *pType = ADDONMenuStates_ADDON_MENU_ENABLED;
    else
        *pType = ADDONMenuStates_ADDON_MENU_GRAYED;

    return S_OK;
}

STDMETHODIMP CAddOnInterface::MenuItemToolTip(long menuID, BSTR* pToolTip)
{
    if (pToolTip == nullptr)
        return E_POINTER;
    *pToolTip = nullptr;

    switch (menuID)
    {
    case MENU_ID_ROOT:
        *pToolTip = _bstr_t(L"ScanToAlibre add-on").Detach();
        break;
    case MENU_ID_LOAD:
        *pToolTip = _bstr_t(L"Step 1: load a scan (STL, or PLY: a mesh, or a point cloud with normals, which is made into a mesh) into this part and show it. It's saved "
                            L"inside the part when you save the part. Left-click on the scan to pick points; "
                            L"F switches full detail, C clears the points. Parts only.").Detach();
        break;
    case MENU_ID_REFINE:
        *pToolTip = _bstr_t(L"Step 2: refine the scan: decimate it to fewer triangles, and smooth its surface "
                            L"(Laplacian, Taubin or Bilateral). Changes apply straight away; Undo takes them "
                            L"back.").Detach();
        break;
    case MENU_ID_ORIENT:
        *pToolTip = _bstr_t(L"Step 3: orient the scan: move, rotate and scale it, fit planes to flat areas, and "
                            L"line it up with the part's planes, axes and origin in 3-2-1 steps.").Detach();
        break;
    case MENU_ID_SHOW_SCAN:
        *pToolTip = _bstr_t(L"Show this part's scan, and keep it showing through other commands such as Measure. "
                            L"Click again to hide it.").Detach();
        break;
    case MENU_ID_TEST_SURFACE:
        *pToolTip = IsAlibreVersionSupported()
                        ? _bstr_t(L"Insert a test B-spline surface into this part, to prove surfaces can be added. Parts only.").Detach()
                        : _bstr_t(L"ScanToAlibre needs Alibre Design 27.0.1.27039 or later.").Detach();
        break;
    case MENU_ID_ABOUT:
        *pToolTip = _bstr_t(L"Show information about the ScanToAlibre add-on").Detach();
        break;
    default:
        break;
    }
    return S_OK;
}

STDMETHODIMP CAddOnInterface::PopupMenu(long /*menuID*/, VARIANT_BOOL* pIsPopup)
{
    if (pIsPopup == nullptr)
        return E_POINTER;
    *pIsPopup = VARIANT_FALSE;
    return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// IAlibreAddOn - commands

STDMETHODIMP CAddOnInterface::InvokeCommand(long menuID, BSTR sessionIdentifier,
                                            struct IAlibreAddOnCommand** pCommand)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if (pCommand == nullptr)
        return E_POINTER;

    // Stateless commands do the work here and hand Alibre no command object.
    *pCommand = nullptr;

    if (menuID == MENU_ID_ABOUT)
    {
        ShowAboutBox();
        return S_OK;
    }

    if (menuID == MENU_ID_TEST_SURFACE)
    {
        if (IsAlibreVersionSupported() && IsPartSession(sessionIdentifier))
            InsertTestSurface(sessionIdentifier != nullptr ? sessionIdentifier : L"");
        return S_OK;
    }

    if (menuID == MENU_ID_LOAD)
    {
        if (IsAlibreVersionSupported() && IsPartSession(sessionIdentifier) &&
            LoadScan(sessionIdentifier != nullptr ? sessionIdentifier : L""))
            return StartScanView(sessionIdentifier, pCommand);
        return S_OK;
    }

    if (menuID == MENU_ID_REFINE)
    {
        const std::wstring sessionId = (sessionIdentifier != nullptr) ? sessionIdentifier : L"";
        if (!IsAlibreVersionSupported() || !IsPartSession(sessionIdentifier) || FindScan(sessionId) == nullptr)
            return S_OK;
        // The dialog, and the scan view so the changes show.
        CRefineDlg* pDialog = new CRefineDlg(sessionId);
        pDialog->Open();
        return StartScanView(sessionIdentifier, pCommand);
    }

    if (menuID == MENU_ID_ORIENT)
    {
        const std::wstring sessionId = (sessionIdentifier != nullptr) ? sessionIdentifier : L"";
        if (!IsAlibreVersionSupported() || !IsPartSession(sessionIdentifier) || FindScan(sessionId) == nullptr)
            return S_OK;
        // The view starts after the dialog, so it draws the orient geometry from the start.
        COrientDlg* pDialog = new COrientDlg(sessionId);
        pDialog->Open();
        return StartScanView(sessionIdentifier, pCommand);
    }

    if (menuID == MENU_ID_SHOW_SCAN)
    {
        // A two-way toggle: a new one turns Show Scan on; returning the running
        // one again turns it off.
        const std::wstring sessionId = (sessionIdentifier != nullptr) ? sessionIdentifier : L"";
        ScanDocument* pScan = FindScan(sessionId);
        if (!IsAlibreVersionSupported() || !IsPartSession(sessionIdentifier) || pScan == nullptr)
            return S_OK;
        pScan->SetMillimetresPerUnit(MillimetresPerModelUnit(sessionId));
        CScanDisplayCommand* pDisplay = ToggleScanDisplay(sessionId);
        const HRESULT hr =
            pDisplay->QueryInterface(__uuidof(IAlibreAddOnCommand), reinterpret_cast<void**>(pCommand));
        Log(L"[display] Show Scan clicked (%s) hr=0x%08X", pDisplay->Site() != nullptr ? L"turning off" : L"turning on",
            static_cast<unsigned>(hr));
        return hr;
    }

    return S_OK;
}

HRESULT CAddOnInterface::StartScanView(BSTR sessionIdentifier, struct IAlibreAddOnCommand** pCommand)
{
    // The part's units are read here rather than when the scan is read from the
    // part, as the part may not be ready then.
    const std::wstring sessionId = (sessionIdentifier != nullptr) ? sessionIdentifier : L"";
    if (ScanDocument* pScan = FindScan(sessionId))
        pScan->SetMillimetresPerUnit(MillimetresPerModelUnit(sessionId));

    // Returned to Alibre as a regular command; Alibre holds the reference.
    CScanViewCommand* pView = new CScanViewCommand(sessionId);
    const HRESULT hr = pView->QueryInterface(__uuidof(IAlibreAddOnCommand), reinterpret_cast<void**>(pCommand));
    Log(L"[view] scan view started hr=0x%08X", static_cast<unsigned>(hr));
    return hr;
}

bool CAddOnInterface::IsPartSession(BSTR sessionIdentifier)
{
    if (sessionIdentifier == nullptr || SysStringLen(sessionIdentifier) == 0 || theApp.m_pRoot == nullptr)
        return false;

    try
    {
        // An unknown identifier throws out of GetItem.
        IADSessionPtr session = theApp.m_pRoot->GetSessions()->GetItem(_variant_t(sessionIdentifier));
        return session != nullptr && session->GetSessionType() == ADObjectSubType_AD_PART;
    }
    catch (const _com_error&)
    {
        return false;
    }
}

bool CAddOnInterface::IsAlibreVersionSupported()
{
    // The build number is the largest number in the version string, such as
    // "PRODUCTVERSION 27,0,1,27039". If there's no number at all, allow the
    // add-on rather than blocking a working version.
    const CString& version = theApp.m_strAlibreVersion;

    long build = -1;
    long current = -1;
    for (int i = 0; i <= version.GetLength(); ++i)
    {
        if (i < version.GetLength() && iswdigit(version[i]))
        {
            current = ((current < 0) ? 0 : current * 10) + (version[i] - L'0');
        }
        else
        {
            if (current > build)
                build = current;
            current = -1;
        }
    }

    return build < 0 || build >= MINIMUM_ALIBRE_BUILD;
}

void CAddOnInterface::ShowAboutBox()
{
    CWnd* pParent = (theApp.m_hAlibreWnd != nullptr)
                        ? CWnd::FromHandle(theApp.m_hAlibreWnd)
                        : nullptr;

    CAboutDlg dlg(pParent);
    dlg.DoModal();
}

/////////////////////////////////////////////////////////////////////////////
// IAlibreAddOn - persistence: the part's scan, in the part file (ScanData)

STDMETHODIMP CAddOnInterface::HasPersistentDataToSave(BSTR sessionIdentifier, VARIANT_BOOL* pHasDataToSave)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if (pHasDataToSave == nullptr)
        return E_POINTER;
    const std::wstring sessionId = (sessionIdentifier != nullptr) ? sessionIdentifier : L"";
    // Alibre asks this often (it lights Save), so say yes only when the scan
    // changed since the part was saved or opened, and log only a change of answer.
    const ScanDocument* pScan = FindScan(sessionId);
    const bool has = pScan != nullptr && pScan->unsaved;
    *pHasDataToSave = has ? VARIANT_TRUE : VARIANT_FALSE;

    static std::map<std::wstring, bool> lastAnswers;
    const auto last = lastAnswers.find(sessionId);
    if (last == lastAnswers.end() || last->second != has)
    {
        lastAnswers[sessionId] = has;
        Log(L"[save] HasPersistentDataToSave(%s): %s%s", sessionId.c_str(), has ? L"yes" : L"no",
            (pScan != nullptr || FindUnreadData(sessionId) != nullptr) ? L" (the part has scan data)" : L"");
    }
    return S_OK;
}

STDMETHODIMP CAddOnInterface::LoadData(struct IStream* pCustomData, BSTR sessionIdentifier)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    const std::wstring sessionId = (sessionIdentifier != nullptr) ? sessionIdentifier : L"";
    if (pCustomData == nullptr)
    {
        Log(L"[load] LoadData(%s): no stream", sessionId.c_str());
        return S_OK;
    }

    const DWORD start = GetTickCount();
    std::string bytes;
    char buffer[1 << 16];
    for (;;)
    {
        ULONG read = 0;
        const HRESULT hr = pCustomData->Read(buffer, sizeof(buffer), &read);
        if (read > 0)
            bytes.append(buffer, read);
        if (FAILED(hr))
        {
            Log(L"[load] LoadData(%s): Read failed hr=0x%08X after %zu bytes", sessionId.c_str(),
                static_cast<unsigned>(hr), bytes.size());
            break;
        }
        if (read == 0 || hr == S_FALSE)
            break;
    }
    Log(L"[load] LoadData(%s): %zu bytes in %lu ms", sessionId.c_str(), bytes.size(), GetTickCount() - start);
    if (bytes.empty())
        return S_OK;

    scancore::ScanData scan;
    std::string message;
    const scancore::ScanDataStatus status = scancore::ReadScanData(bytes, scan, message);
    if (status != scancore::ScanDataStatus::Ok)
    {
        // Kept as it was, and written back unchanged, so nothing is lost.
        Log(L"[load] the part's scan data can't be used (%d): %S; it's kept unchanged", static_cast<int>(status),
            message.c_str());
        KeepUnreadData(sessionId, std::move(bytes));
        return S_OK;
    }

    ScanDocument* pLoaded = AddScan(sessionId, MakeScanDocument(std::move(scan.mesh), scan.sourceName));
    pLoaded->fitted = std::move(scan.features);
    pLoaded->unsaved = false;
    ForgetUnreadData(sessionId);
    Log(L"[load] the part's scan is %s, with %zu fitted planes and axes; Show Scan draws it",
        ScanDisplayName(scan.sourceName).c_str(), pLoaded->fitted.size());
    return S_OK;
}

STDMETHODIMP CAddOnInterface::SaveData(struct IStream* pCustomData, BSTR sessionIdentifier)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    const std::wstring sessionId = (sessionIdentifier != nullptr) ? sessionIdentifier : L"";
    if (pCustomData == nullptr)
    {
        Log(L"[save] SaveData(%s): no stream", sessionId.c_str());
        return S_OK;
    }

    const DWORD start = GetTickCount();
    std::string bytes;
    if (const ScanDocument* pScan = FindScan(sessionId))
    {
        scancore::ScanData scan;
        scan.mesh = pScan->mesh;
        scan.sourceName = pScan->sourceName;
        scan.features = pScan->fitted;
        bytes = scancore::WriteScanData(scan);
    }
    else if (const std::string* pUnread = FindUnreadData(sessionId))
    {
        bytes = *pUnread;
    }
    else
    {
        Log(L"[save] SaveData(%s): nothing to save", sessionId.c_str());
        return S_OK;
    }

    size_t written = 0;
    while (written < bytes.size())
    {
        const ULONG chunk = static_cast<ULONG>(std::min<size_t>(bytes.size() - written, 1u << 24));
        ULONG done = 0;
        const HRESULT hr = pCustomData->Write(bytes.data() + written, chunk, &done);
        if (FAILED(hr) || done == 0)
        {
            Log(L"[save] SaveData(%s): Write failed hr=0x%08X after %zu of %zu bytes", sessionId.c_str(),
                static_cast<unsigned>(hr), written, bytes.size());
            return FAILED(hr) ? hr : E_FAIL;
        }
        written += done;
    }
    if (ScanDocument* pScan = FindScan(sessionId))
        pScan->unsaved = false;
    Log(L"[save] SaveData(%s): wrote %zu bytes in %lu ms", sessionId.c_str(), bytes.size(), GetTickCount() - start);
    return S_OK;
}

STDMETHODIMP CAddOnInterface::setIsAddOnLicensed(VARIANT_BOOL /*isLicensed*/)
{
    return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// IAlibreAddOn - spec v2: icons and the dedicated ribbon tab

STDMETHODIMP CAddOnInterface::MenuIcon(long menuID, BSTR* pMenuIconPath)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if (pMenuIconPath == nullptr)
        return E_POINTER;
    *pMenuIconPath = nullptr;

    if (menuID != MENU_ID_ROOT && menuID != MENU_ID_ABOUT && menuID != MENU_ID_TEST_SURFACE &&
        menuID != MENU_ID_LOAD && menuID != MENU_ID_SHOW_SCAN &&
        menuID != MENU_ID_REFINE && menuID != MENU_ID_ORIENT)
        return S_OK;

    // The icon is deployed beside the DLL. Resolve it from our own module,
    // never from the current working directory.
    CString iconPath = GetModuleFolder() + L"ScanToAlibre.ico";
    if (GetFileAttributesW(iconPath) != INVALID_FILE_ATTRIBUTES)
        *pMenuIconPath = _bstr_t(static_cast<LPCWSTR>(iconPath)).Detach();

    return S_OK;
}

STDMETHODIMP CAddOnInterface::UseDedicatedRibbonTab(VARIANT_BOOL* pFlag)
{
    if (pFlag == nullptr)
        return E_POINTER;
    *pFlag = VARIANT_TRUE;      // give ScanToAlibre its own ribbon tab
    return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// Helpers

// Folder containing this DLL, with a trailing backslash.
CString CAddOnInterface::GetModuleFolder()
{
    HMODULE hModule = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&CAddOnInterface::GetModuleFolder),
                            &hModule))
    {
        return CString();
    }

    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(hModule, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return CString();

    CString folder(path);
    int slash = folder.ReverseFind(L'\\');
    return (slash >= 0) ? folder.Left(slash + 1) : CString();
}
