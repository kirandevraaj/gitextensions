// GitExtensionsShellEx.cpp : Implementation of CGitExtensionsShellEx

#include "stdafx.h"
#include <vector>
#include "resource.h"
#include "Generated/GitExtensionsShellEx.h"
#include "GitExtensionsShellEx.h"

#define MIIM_ID          0x00000002
#define MIIM_STRING      0x00000040
#define MIIM_BITMAP      0x00000080

/////////////////////////////////////////////////////////////////////////////
// CGitExtensionsShellEx

bool IsVistaOrLater()
{
    static int version = -1;
    if (version == -1)
    {
        OSVERSIONINFOEX         inf;
        SecureZeroMemory(&inf, sizeof(OSVERSIONINFOEX));
        inf.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
        GetVersionEx((OSVERSIONINFO *)&inf);
        version = MAKEWORD(inf.dwMinorVersion, inf.dwMajorVersion);
    }

    return version >= 0x0600;
}

CGitExtensionsShellEx::CGitExtensionsShellEx()
{
    if (IsVistaOrLater())
    {
        HMODULE hUxTheme = ::GetModuleHandle (_T("UXTHEME.DLL"));

        pfnGetBufferedPaintBits = (FN_GetBufferedPaintBits)::GetProcAddress(hUxTheme, "GetBufferedPaintBits");
        pfnBeginBufferedPaint = (FN_BeginBufferedPaint)::GetProcAddress(hUxTheme, "BeginBufferedPaint");
        pfnEndBufferedPaint = (FN_EndBufferedPaint)::GetProcAddress(hUxTheme, "EndBufferedPaint");
    }
}

STDMETHODIMP CGitExtensionsShellEx::Initialize (
    LPCITEMIDLIST pidlFolder, LPDATAOBJECT pDataObj, HKEY hProgID )
{
    FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg = { TYMED_HGLOBAL };
    HDROP     hDrop;

    /* store the folder, if provided */
    if (pidlFolder)
        SHGetPathFromIDList(pidlFolder, m_szFile);

    if (!pDataObj)
        return S_OK;

    // Look for CF_HDROP data in the data object.
    if (FAILED( pDataObj->GetData ( &fmt, &stg ) ))
    {
        // Nope! Return an "invalid argument" error back to Explorer.
        return E_INVALIDARG;
    }

    // Get a pointer to the actual data.
    hDrop = (HDROP) GlobalLock ( stg.hGlobal );

    // Make sure it worked.
    if ( NULL == hDrop )
        return E_INVALIDARG;

    // Sanity check - make sure there is at least one filename.
    UINT uNumFiles = DragQueryFile ( hDrop, 0xFFFFFFFF, NULL, 0 );
    HRESULT hr = S_OK;

    if (uNumFiles == 0)
        hr = E_INVALIDARG;
    // Get the name of the first file and store it in our member variable m_szFile.
    else if (!DragQueryFile( hDrop, 0, m_szFile, MAX_PATH ))
        hr = E_INVALIDARG;

    GlobalUnlock ( stg.hGlobal );
    ReleaseStgMedium ( &stg );

    return hr;
}

HBITMAP CGitExtensionsShellEx::IconToBitmapPARGB32(UINT uIcon)
{
    std::map<UINT, HBITMAP>::iterator bitmap_it = bitmaps.lower_bound(uIcon);
    if (bitmap_it != bitmaps.end() && bitmap_it->first == uIcon)
        return bitmap_it->second;

    HICON hIcon = (HICON)LoadImage(_Module.GetModuleInstance(), MAKEINTRESOURCE(uIcon), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (!hIcon)
        return NULL;

    if (pfnBeginBufferedPaint == NULL || pfnEndBufferedPaint == NULL || pfnGetBufferedPaintBits == NULL)
        return NULL;

    SIZE sizIcon;
    sizIcon.cx = GetSystemMetrics(SM_CXSMICON);
    sizIcon.cy = GetSystemMetrics(SM_CYSMICON);

    RECT rcIcon;
    SetRect(&rcIcon, 0, 0, sizIcon.cx, sizIcon.cy);
    HBITMAP hBmp = NULL;

    HDC hdcDest = CreateCompatibleDC(NULL);
    if (hdcDest)
    {
        if (SUCCEEDED(Create32BitHBITMAP(hdcDest, &sizIcon, NULL, &hBmp)))
        {
            HBITMAP hbmpOld = (HBITMAP)SelectObject(hdcDest, hBmp);
            if (hbmpOld)
            {
                BLENDFUNCTION bfAlpha = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                BP_PAINTPARAMS paintParams = {0};
                paintParams.cbSize = sizeof(paintParams);
                paintParams.dwFlags = BPPF_ERASE;
                paintParams.pBlendFunction = &bfAlpha;

                HDC hdcBuffer;
                HPAINTBUFFER hPaintBuffer = pfnBeginBufferedPaint(hdcDest, &rcIcon, BPBF_DIB, &paintParams, &hdcBuffer);
                if (hPaintBuffer)
                {
                    if (DrawIconEx(hdcBuffer, 0, 0, hIcon, sizIcon.cx, sizIcon.cy, 0, NULL, DI_NORMAL))
                    {
                        // If icon did not have an alpha channel we need to convert buffer to PARGB
                        ConvertBufferToPARGB32(hPaintBuffer, hdcDest, hIcon, sizIcon);
                    }

                    // This will write the buffer contents to the destination bitmap
                    pfnEndBufferedPaint(hPaintBuffer, TRUE);
                }

                SelectObject(hdcDest, hbmpOld);
            }
        }

        DeleteDC(hdcDest);
    }

    DestroyIcon(hIcon);

    if(hBmp)
        bitmaps.insert(bitmap_it, std::make_pair(uIcon, hBmp));
    return hBmp;
}

HRESULT CGitExtensionsShellEx::Create32BitHBITMAP(HDC hdc, const SIZE *psize, __deref_opt_out void **ppvBits, __out HBITMAP* phBmp)
{
    *phBmp = NULL;

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biCompression = BI_RGB;

    bmi.bmiHeader.biWidth = psize->cx;
    bmi.bmiHeader.biHeight = psize->cy;
    bmi.bmiHeader.biBitCount = 32;

    HDC hdcUsed = hdc ? hdc : GetDC(NULL);
    if (hdcUsed)
    {
        *phBmp = CreateDIBSection(hdcUsed, &bmi, DIB_RGB_COLORS, ppvBits, NULL, 0);
        if (hdc != hdcUsed)
        {
            ReleaseDC(NULL, hdcUsed);
        }
    }
    return (NULL == *phBmp) ? E_OUTOFMEMORY : S_OK;
}

HRESULT CGitExtensionsShellEx::ConvertBufferToPARGB32(HPAINTBUFFER hPaintBuffer, HDC hdc, HICON hicon, SIZE& sizIcon)
{
    RGBQUAD *prgbQuad;
    int cxRow;
    HRESULT hr = pfnGetBufferedPaintBits(hPaintBuffer, &prgbQuad, &cxRow);
    if (SUCCEEDED(hr))
    {
        ARGB *pargb = reinterpret_cast<ARGB *>(prgbQuad);
        if (!HasAlpha(pargb, sizIcon, cxRow))
        {
            ICONINFO info;
            if (GetIconInfo(hicon, &info))
            {
                if (info.hbmMask)
                {
                    hr = ConvertToPARGB32(hdc, pargb, info.hbmMask, sizIcon, cxRow);
                }

                DeleteObject(info.hbmColor);
                DeleteObject(info.hbmMask);
            }
        }
    }

    return hr;
}

bool CGitExtensionsShellEx::HasAlpha(__in ARGB *pargb, SIZE& sizImage, int cxRow)
{
    ULONG cxDelta = cxRow - sizImage.cx;
    for (ULONG y = sizImage.cy; y; --y)
    {
        for (ULONG x = sizImage.cx; x; --x)
        {
            if (*pargb++ & 0xFF000000)
            {
                return true;
            }
        }

        pargb += cxDelta;
    }

    return false;
}

HRESULT CGitExtensionsShellEx::ConvertToPARGB32(HDC hdc, __inout ARGB *pargb, HBITMAP hbmp, SIZE& sizImage, int cxRow)
{
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biCompression = BI_RGB;

    bmi.bmiHeader.biWidth = sizImage.cx;
    bmi.bmiHeader.biHeight = sizImage.cy;
    bmi.bmiHeader.biBitCount = 32;

    HRESULT hr = E_OUTOFMEMORY;
    HANDLE hHeap = GetProcessHeap();
    void *pvBits = HeapAlloc(hHeap, 0, bmi.bmiHeader.biWidth * 4 * bmi.bmiHeader.biHeight);
    if (pvBits)
    {
        hr = E_UNEXPECTED;
        if (GetDIBits(hdc, hbmp, 0, bmi.bmiHeader.biHeight, pvBits, &bmi, DIB_RGB_COLORS) == bmi.bmiHeader.biHeight)
        {
            ULONG cxDelta = cxRow - bmi.bmiHeader.biWidth;
            ARGB *pargbMask = static_cast<ARGB *>(pvBits);

            for (ULONG y = bmi.bmiHeader.biHeight; y; --y)
            {
                for (ULONG x = bmi.bmiHeader.biWidth; x; --x)
                {
                    if (*pargbMask++)
                    {
                        // transparent pixel
                        *pargb++ = 0;
                    }
                    else
                    {
                        // opaque pixel
                        *pargb++ |= 0xFF000000;
                    }
                }

                pargb += cxDelta;
            }

            hr = S_OK;
        }

        HeapFree(hHeap, 0, pvBits);
    }

    return hr;
}

bool IsExists(const std::wstring& dir)
{
    DWORD dwAttrib = GetFileAttributes(dir.c_str());

    return (dwAttrib != INVALID_FILE_ATTRIBUTES);
}

bool IsFileExists(LPCWSTR str)
{
    DWORD dwAttrib = GetFileAttributes(str);

    return (dwAttrib != INVALID_FILE_ATTRIBUTES) && ((dwAttrib & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

bool CGitExtensionsShellEx::ValidWorkingDir(const std::wstring& dir)
{
    if (dir.empty())
        return false;

    if (IsExists(dir + L"\\.git\\") || IsExists(dir + L"\\.git"))
        return true;

    return IsExists(dir + L"\\info\\") &&
        IsExists(dir + L"\\objects\\") &&
        IsExists(dir + L"\\refs\\");
}

bool CGitExtensionsShellEx::IsValidGitDir(TCHAR m_szFile[])
{
    if (m_szFile[0] == '\0')
        return false;

    std::wstring dir(m_szFile);

    do
    {
        if (ValidWorkingDir(dir))
            return true;
        size_t pos = dir.rfind('\\');
        if (dir.rfind('\\') != std::wstring::npos)
            dir.resize(pos);
    } while (dir.rfind('\\') != std::wstring::npos);
    return false;
}

STDMETHODIMP CGitExtensionsShellEx::QueryContextMenu  (
    HMENU hMenu, UINT menuIndex, UINT uidFirstCmd,
    UINT uidLastCmd, UINT uFlags )
{
    // If the flags include CMF_DEFAULTONLY then we shouldn't do anything.
    if ( uFlags & CMF_DEFAULTONLY )
        return S_OK;

    CString szCascadeShellMenuItems = GetRegistryValue(HKEY_CURRENT_USER, L"SOFTWARE\\GitExtensions\\GitExtensions", L"CascadeShellMenuItems");
    if (szCascadeShellMenuItems.IsEmpty())
        szCascadeShellMenuItems = "11011100111111";
    bool CascadeContextMenu = szCascadeShellMenuItems.Find('1') != -1;
    HMENU popupMenu = NULL;
    if (CascadeContextMenu)
        popupMenu = CreateMenu();

    bool isValidDir = IsValidGitDir(m_szFile);
    bool isFile = IsFileExists(m_szFile);

    // preset values, if not used
    AddFilesId = -1;
    ApplyPatchId = -1;
    BrowseId = -1;
    CreateBranchId = -1;
    CheckoutBranchId = -1;
    CheckoutRevisionId = -1;
    CloneId = -1;
    CommitId = -1;
    FileHistoryId = -1;
    PullId = -1;
    PushId = -1;
    SettingsId = -1;
    ViewDiffId = -1;
    ResetFileChangesId = -1;

    UINT submenuIndex = 0;
    int id = 0;
    bool isSubMenu;

    if (isValidDir)
    {
        if (!isFile)
        {
            isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 2);
            BrowseId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"Browse", IDI_ICONBROWSEFILEEXPLORER, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, DisplayInSubmenu(szCascadeShellMenuItems, 2));

            isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 7);
            CommitId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"Commit", IDI_ICONCOMMIT, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);

            isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 10);
            PullId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"Pull", IDI_ICONPULL, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);

            isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 11);
            PushId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"Push", IDI_ICONPUSH, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);

            isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 13);
            ViewDiffId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"View changes", IDI_ICONVIEWCHANGES, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);

            isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 4);
            if (isSubMenu && submenuIndex > 0) {
                InsertMenu(popupMenu, submenuIndex++, MF_SEPARATOR|MF_BYPOSITION, 0, NULL); ++id;
            }
            CheckoutBranchId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"Checkout branch", IDI_ICONBRANCHCHECKOUT, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);

            isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 5);
            CheckoutRevisionId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"Checkout revision", IDI_ICONREVISIONCHECKOUT, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);

            isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 3);
            CreateBranchId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"Create branch", IDI_ICONBRANCHCREATE, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);
        }

        isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 8);
        if (isSubMenu && submenuIndex > 0) {
            InsertMenu(popupMenu, submenuIndex++, MF_SEPARATOR|MF_BYPOSITION, 0, NULL); ++id;
        }
        FileHistoryId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"File history", IDI_ICONFILEHISTORY, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);

        isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 9);
        ResetFileChangesId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"Reset file changes", IDI_ICONTRESETFILETO, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);

        isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 0);
        AddFilesId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"Add files", IDI_ICONADDED, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);

        isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 1);
        ApplyPatchId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"Apply patch", 0, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);
    }
    else 
    {
        isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 6);
        CloneId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"Clone", IDI_ICONCLONEREPOGIT, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);
    }

    isSubMenu = DisplayInSubmenu(szCascadeShellMenuItems, 12);
    if (isSubMenu && submenuIndex > 0) {
        InsertMenu(popupMenu, submenuIndex++, MF_SEPARATOR|MF_BYPOSITION, 0, NULL); ++id;
    }
    SettingsId=AddMenuItem(!isSubMenu ? hMenu : popupMenu, L"Settings", IDI_ICONSETTINGS, uidFirstCmd, ++id, !isSubMenu ? menuIndex++ : submenuIndex++, isSubMenu);

    ++id;

    if (CascadeContextMenu)
    {
        MENUITEMINFO info;
        info.cbSize = sizeof( MENUITEMINFO );
        info.fMask = MIIM_STRING | MIIM_ID | MIIM_BITMAP | MIIM_SUBMENU;
        info.wID = uidFirstCmd + 1;
        info.hbmpItem = IsVistaOrLater() ? IconToBitmapPARGB32(IDI_GITEXTENSIONS) : HBMMENU_CALLBACK;
        myIDMap[1] = IDI_GITEXTENSIONS;
        myIDMap[uidFirstCmd + 1] = IDI_GITEXTENSIONS;
        info.dwTypeData = _T("Git Extensions");
        info.hSubMenu = popupMenu;
        InsertMenuItem(hMenu, menuIndex, true, &info);
    }

    return MAKE_HRESULT ( SEVERITY_SUCCESS, FACILITY_NULL, id);
}

UINT CGitExtensionsShellEx::AddMenuItem(HMENU hMenu, LPTSTR text, int resource, UINT uidFirstCmd, UINT id, UINT position, bool isSubMenu)
{
    MENUITEMINFO mii;
    memset(&mii, 0, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING | MIIM_ID;
    if (resource)
    {
        mii.fMask |= MIIM_BITMAP;
        mii.hbmpItem = IsVistaOrLater() ? IconToBitmapPARGB32(resource) : HBMMENU_CALLBACK;
        myIDMap[id] = resource;
        myIDMap[uidFirstCmd + id] = resource;
    }
    mii.wID	= uidFirstCmd + id;
    std::wstring textEx;
    if (isSubMenu)
        mii.dwTypeData = text;
    else
    {
        textEx = std::wstring(L"GitEx ") + text;
        mii.dwTypeData = &textEx[0];
    }

    InsertMenuItem(hMenu, position, TRUE, &mii);
    return id;
}

bool CGitExtensionsShellEx::DisplayInSubmenu(CString settings, int id)
{
    if (settings.GetLength() < id)
    {
        return true;
    } else
    {
        return (settings[id] != '0');
    }
}

STDMETHODIMP CGitExtensionsShellEx::GetCommandString (
    UINT_PTR idCmd, UINT uFlags, UINT* pwReserved, LPSTR pszName, UINT cchMax )
{
    USES_CONVERSION;

    // Check idCmd, it must be 0 since we have only one menu item.
    if ( 0 != idCmd )
        return E_INVALIDARG;

    // If Explorer is asking for a help string, copy our string into the
    // supplied buffer.
    if ( uFlags & GCS_HELPTEXT )
    {
        LPCTSTR szText = _T("Git shell extensions");

        if ( uFlags & GCS_UNICODE )
        {
            // We need to cast pszName to a Unicode string, and then use the
            // Unicode string copy API.
            lstrcpynW ( (LPWSTR) pszName, T2CW(szText), cchMax );
        }
        else
        {
            // Use the ANSI string copy API to return the help string.
            lstrcpynA ( pszName, T2CA(szText), cchMax );	
        }

        return S_OK;
    }

    return E_INVALIDARG;
}

void CGitExtensionsShellEx::RunGitEx(const TCHAR * command)
{
    CString szFile = m_szFile;
    CString szCommandName = command;
    CString args;

    args += command;
    args += " \"";
    args += m_szFile;
    args += "\"";

    CString dir = "";

    if (dir.GetLength() == 0)
        dir = GetRegistryValue(HKEY_CURRENT_USER, L"SOFTWARE\\GitExtensions\\GitExtensions", L"InstallDir");
    if (dir.GetLength() == 0)
        dir = GetRegistryValue(HKEY_USERS, L"SOFTWARE\\GitExtensions\\GitExtensions", L"InstallDir");
    if (dir.GetLength() == 0)
        dir = GetRegistryValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\GitExtensions\\GitExtensions", L"InstallDir");

    ShellExecute(NULL, L"open", L"GitExtensions.exe", args, dir, SW_SHOWNORMAL); 
}

STDMETHODIMP CGitExtensionsShellEx::InvokeCommand ( LPCMINVOKECOMMANDINFO pCmdInfo )
{
    // If lpVerb really points to a string, ignore this function call and bail out.
    if ( pCmdInfo == NULL ||0 != HIWORD( pCmdInfo->lpVerb ) )
        return E_INVALIDARG;

    int invokeId = LOWORD( pCmdInfo->lpVerb);

    // Get the command index - the only valid one is 0.
    if (invokeId == AddFilesId)
    {
        RunGitEx(_T("addfiles"));
        return S_OK;
    } else
    // Get the command index - the only valid one is 0.
    if (invokeId == ApplyPatchId)
    {
        RunGitEx(_T("applypatch"));
        return S_OK;
    } else
    if (invokeId == BrowseId)
    {
        RunGitEx(_T("browse"));
        return S_OK;
    } else
    if (invokeId == CreateBranchId)
    {
        RunGitEx(_T("branch"));
        return S_OK;
    } else
    if (invokeId == CheckoutBranchId)
    {
        RunGitEx(_T("checkoutbranch"));
        return S_OK;
    } else
    if (invokeId == CheckoutRevisionId)
    {
        RunGitEx(_T("checkoutrevision"));
        return S_OK;
    } else
    if (invokeId == CloneId)
    {
        RunGitEx(_T("clone"));
        return S_OK;
    } else
    if (invokeId == CommitId)
    {
        RunGitEx(_T("commit"));
        return S_OK;
    } else
    if (invokeId == FileHistoryId)
    {
        RunGitEx(_T("filehistory"));
        return S_OK;
    } else
    if (invokeId == PullId)
    {
        RunGitEx(_T("pull"));
        return S_OK;
    } else
    if (invokeId == PushId)
    {
        RunGitEx(_T("push"));
        return S_OK;
    } else
    if (invokeId == SettingsId)
    {
        RunGitEx(_T("settings"));
        return S_OK;
    } else
    if (invokeId == ViewDiffId)
    {
        RunGitEx(_T("viewdiff"));
        return S_OK;
    } else
    if (invokeId == ResetFileChangesId)
    {
        RunGitEx(_T("revert"));
        return S_OK;
    }
    return E_INVALIDARG;
}

STDMETHODIMP CGitExtensionsShellEx::HandleMenuMsg( UINT uMsg, WPARAM wParam, LPARAM lParam )
{	
    LRESULT res;
    return HandleMenuMsg2(uMsg, wParam, lParam, &res);
}

STDMETHODIMP CGitExtensionsShellEx::HandleMenuMsg2( UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pResult )
{
    switch (uMsg)
    {
    case WM_MEASUREITEM:
        {
            MEASUREITEMSTRUCT* lpmis = (MEASUREITEMSTRUCT*)lParam;
            if (lpmis==NULL)
                break;
            lpmis->itemWidth = 16;
            lpmis->itemHeight = 16;
            if (pResult)
                *pResult = TRUE;
        }
        break;
    case WM_DRAWITEM:
        {
            LPCTSTR resource;
            DRAWITEMSTRUCT* lpdis = (DRAWITEMSTRUCT*)lParam;
            if ((lpdis==NULL)||(lpdis->CtlType != ODT_MENU))
                return S_OK;		//not for a menu
            auto it = myIDMap.find(lpdis->itemID);
            if (it == myIDMap.end())
                return S_OK;
            resource = MAKEINTRESOURCE(it->second);
            if (resource == NULL)
                return S_OK;
            HICON hIcon = (HICON)LoadImage(_Module.GetModuleInstance(), resource, IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
            if (hIcon == NULL)
                return S_OK;
            DrawIconEx(lpdis->hDC,
                lpdis->rcItem.left,
                lpdis->rcItem.top + (lpdis->rcItem.bottom - lpdis->rcItem.top - 16) / 2,
                hIcon, 16, 16,
                0, NULL, DI_NORMAL);
            DestroyIcon(hIcon);
            if (pResult)
                *pResult = TRUE;
        }
        break;
    default:
        return S_OK;
    }

    return S_OK;
}

CString CGitExtensionsShellEx::GetRegistryValue( HKEY hOpenKey, LPCTSTR szKey, LPCTSTR path )
{
    CString result = "";
    HKEY key;

    TCHAR tempStr[512];
    unsigned long taille = sizeof(tempStr);
    unsigned long type;

    long res = RegOpenKeyEx(hOpenKey,szKey, 0, KEY_READ | KEY_WOW64_32KEY, &key);
    if (res != ERROR_SUCCESS) {
        return "";
    }
    if (RegQueryValueEx(key, path, 0, &type, (BYTE*)&tempStr[0], &taille) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return "";
    }

    tempStr[taille] = 0;

    result = tempStr;

    RegCloseKey(key);

    return result;
}
