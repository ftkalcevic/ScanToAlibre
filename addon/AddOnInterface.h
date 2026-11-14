// AddOnInterface.h : the IAlibreAddOn implementation for ScanToAlibre.
//
// Menu tree exposed to Alibre:
//
//   ScanToAlibre            (root, id 100)  -> shown as a dedicated ribbon tab, a workflow:
//     1 Load                (leaf, id 103)  -> parts only: reads a scan, starts the scan view
//     2 Refine              (leaf, id 105)  -> parts with a scan: decimate and smooth (a modeless dialog)
//     3 Orient              (leaf, id 106)  -> parts with a scan: move, rotate, scale, fit planes, align (a modeless dialog)
//     4 Section             (leaf, id 107)  -> parts with a scan: slice it with a plane, fit lines and arcs, make a sketch (a modeless dialog)
//     Show Scan             (leaf, id 104)  -> parts with a scan: a two-way toggle that draws the scan through native commands
//     About                 (leaf, id 101)  -> stateless command: shows About box
// Insert Test Surface (id 102) proved surface insertion and is no longer on the ribbon.

#pragma once

// Menu ids (any non-zero values unique within this add-on).
const long MENU_ID_ROOT         = 100;
const long MENU_ID_ABOUT        = 101;
const long MENU_ID_TEST_SURFACE = 102;
const long MENU_ID_LOAD         = 103;
const long MENU_ID_SHOW_SCAN    = 104;
const long MENU_ID_REFINE       = 105;
const long MENU_ID_ORIENT       = 106;
const long MENU_ID_SECTION      = 107;

// Oldest Alibre build this add-on is written against (27.0.1.27039).
const long MINIMUM_ALIBRE_BUILD = 27039;

class CAddOnInterface : public IAlibreAddOn
{
public:
    CAddOnInterface();
    virtual ~CAddOnInterface();

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void** ppObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // IDispatch - Alibre only uses the vtable, so these are stubs.
    STDMETHOD(GetTypeInfoCount)(UINT* pctinfo) override;
    STDMETHOD(GetTypeInfo)(UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo) override;
    STDMETHOD(GetIDsOfNames)(REFIID riid, LPOLESTR* rgszNames, UINT cNames,
                             LCID lcid, DISPID* rgDispId) override;
    STDMETHOD(Invoke)(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags,
                      DISPPARAMS* pDispParams, VARIANT* pVarResult,
                      EXCEPINFO* pExcepInfo, UINT* puArgErr) override;

    // IAlibreAddOn
    STDMETHOD(get_RootMenuItem)(long* pRootMenuID) override;
    STDMETHOD(HasSubMenus)(long menuID, VARIANT_BOOL* pHasSubMenus) override;
    STDMETHOD(SubMenuItems)(long menuID, SAFEARRAY** pSubMenuIDs) override;
    STDMETHOD(MenuItemText)(long menuID, BSTR* pMenuDisplayText) override;
    STDMETHOD(MenuItemState)(long menuID, BSTR sessionIdentifier,
                             enum ADDONMenuStates* pType) override;
    STDMETHOD(MenuItemToolTip)(long menuID, BSTR* pToolTip) override;
    STDMETHOD(PopupMenu)(long menuID, VARIANT_BOOL* pIsPopup) override;
    STDMETHOD(HasPersistentDataToSave)(BSTR sessionIdentifier,
                                       VARIANT_BOOL* pHasDataToSave) override;
    STDMETHOD(InvokeCommand)(long menuID, BSTR sessionIdentifier,
                             struct IAlibreAddOnCommand** pCommand) override;
    STDMETHOD(LoadData)(struct IStream* pCustomData, BSTR sessionIdentifier) override;
    STDMETHOD(SaveData)(struct IStream* pCustomData, BSTR sessionIdentifier) override;
    STDMETHOD(setIsAddOnLicensed)(VARIANT_BOOL isLicensed) override;

    // Spec version 2 additions (need specificationVersion="2" in the .adc)
    STDMETHOD(MenuIcon)(long menuID, BSTR* pMenuIconPath) override;
    STDMETHOD(UseDedicatedRibbonTab)(VARIANT_BOOL* pFlag) override;

private:
    void ShowAboutBox();
    static HRESULT StartScanView(BSTR sessionIdentifier, struct IAlibreAddOnCommand** pCommand);
    static CString GetModuleFolder();
    static bool IsPartSession(BSTR sessionIdentifier);
    static bool IsAlibreVersionSupported();

    long m_nRefCount;
};
