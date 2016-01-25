#include "mozdbgext.h"

#include <sstream>
#include <string>
#include <string.h>

// #define DEBUG_ENUM

static BOOL CALLBACK
EnumWindowsProc(HWND aHwnd, LPARAM aLParam)
{
  wchar_t className[MAX_PATH] = {0};
  if (!GetClassName(aHwnd, className, MAX_PATH)) {
    return TRUE;
  }
  if (wcsncmp(className, L"WinDbgFrameClass", MAX_PATH)) {
    return TRUE;
  }
  HWND* hwndWindbg = reinterpret_cast<HWND*>(aLParam);
  if (hwndWindbg) {
    *hwndWindbg = aHwnd;
  }
  return FALSE;
}

static HWND sHwndWindbg = NULL;

static bool
FindWindbgWindow()
{
  if (sHwndWindbg) {
    return true;
  }
  EnumWindows(&EnumWindowsProc, (LPARAM)&sHwndWindbg);
  if (!sHwndWindbg) {
    return false;
  }
  return true;
}

struct FindRicheditWindowContext
{
  explicit FindRicheditWindowContext(const std::wstring& aPath)
    : mPath(aPath)
    , mHwnd(NULL)
  {
  }
  const std::wstring& mPath;
  HWND                mHwnd;
  HWND                mTabCtrl;
};

static BOOL CALLBACK
EnumChildProc(HWND aHwnd, LPARAM aLParam)
{
  wchar_t buf[MAX_PATH + 1] = {0};
  if (!GetClassName(aHwnd, buf, MAX_PATH)) {
    return TRUE;
  }
  if (!wcsncmp(buf, L"DockClass", MAX_PATH)) {
#ifdef DEBUG_ENUM
    dprintf("Found dock window: 0x%p\n", aHwnd);
#endif
    return EnumChildWindows(aHwnd, &EnumChildProc, aLParam);
  }
  FindRicheditWindowContext& context =
    *reinterpret_cast<FindRicheditWindowContext*>(aLParam);
  if (!wcsncmp(buf, L"WinBaseClass", MAX_PATH)) {
#ifdef DEBUG_ENUM
    dprintf("Found WinBaseClass window: 0x%p\n", aHwnd);
#endif
    if (!GetWindowText(aHwnd, buf, MAX_PATH)) {
      return TRUE;
    }
#ifdef DEBUG_ENUM
    dprintf("Window text: \"%S\"\n", buf);
#endif
    int comparison = _wcsnicmp(buf, context.mPath.c_str(), MAX_PATH);
#ifdef DEBUG_ENUM
    dprintf("Comparison result is %d\n", comparison);
#endif
    if (!comparison) {
      context.mHwnd = FindWindowEx(aHwnd, NULL, L"RICHEDIT50W", nullptr);
      context.mTabCtrl = FindWindowEx(aHwnd, NULL, L"SysTabControl32", nullptr);
      return FALSE;
    }
  }
  return TRUE;
}

static HWND
FindRicheditWindow(const std::wstring& aPath)
{
  if (!FindWindbgWindow()) {
    return NULL;
  }
  FindRicheditWindowContext context(aPath);
  EnumChildWindows(sHwndWindbg, &EnumChildProc, (LPARAM)&context);
  return context.mHwnd;
}

// Terrible hack, but not exposed otherwise
HRESULT CALLBACK
gotoline(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  std::string path;
  unsigned int lineNo = 1;

  std::istringstream iss(aArgs);
  iss >> lineNo;
  if (!iss) {
    dprintf("Failed to extract line number argument\n");
    return E_FAIL;
  }
  getline(iss, path);
  if (!iss) {
    dprintf("Failed to extract path argument\n");
    return E_FAIL;
  }
  while (path[0] == ' ') {
    path.erase(path.begin());
  }
#ifdef DEBUG_ENUM
  dprintf("path is \"%s\"\n", path);
#endif

  std::wstring widePath;
  if (!ToUTF16(path, widePath, CP_ACP)) {
    dprintf("Can't convert path to UTF-16\n");
    return E_FAIL;
  }

  if (!FindWindbgWindow()) {
    dprintf("Can't find WinDbg top-level window!\n");
    return E_FAIL;
  }
#ifdef DEBUG_ENUM
  dprintf("WinDbg top-level window is 0x%p\n", sHwndWindbg);
#endif
  HWND richEditWindow = FindRicheditWindow(widePath);
  if (!richEditWindow) {
    dprintf("Can't find RichEdit window\n");
    return E_FAIL;
  }

  LRESULT result = SendMessage(richEditWindow, EM_GETLINECOUNT, 0, 0);
  if (!lineNo || lineNo > result) {
    dprintf("Line number out of bounds\n");
    return E_FAIL;
  }
  result = SendMessage(richEditWindow, EM_LINEINDEX, lineNo - 1, 0);
  if (result == -1) {
    dprintf("Can't find line number for index %d\n", lineNo - 1);
    return E_FAIL;
  }
  SendMessage(richEditWindow, EM_SETSEL, result, result);
  SendMessage(richEditWindow, EM_SCROLLCARET, 0, 0);
  return S_OK;
}
