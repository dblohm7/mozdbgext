#ifndef __MOZDBGEXT_H
#define __MOZDBGEXT_H

#define KDEXT_64BIT
#include <windows.h>
#include <comdef.h>
#include "dbgeng.h"
#include "dbghelp.h"

#include <memory>
#include <string>
#include <utility>

_COM_SMARTPTR_TYPEDEF(IDebugClient5, __uuidof(IDebugClient5));
_COM_SMARTPTR_TYPEDEF(IDebugControl7, __uuidof(IDebugControl7));
_COM_SMARTPTR_TYPEDEF(IDebugAdvanced3, __uuidof(IDebugAdvanced3));
_COM_SMARTPTR_TYPEDEF(IDebugSymbols3, __uuidof(IDebugSymbols3));
_COM_SMARTPTR_TYPEDEF(IDebugDataSpaces4, __uuidof(IDebugDataSpaces4));
_COM_SMARTPTR_TYPEDEF(IDebugRegisters2, __uuidof(IDebugRegisters2));
_COM_SMARTPTR_TYPEDEF(IDebugSystemObjects4, __uuidof(IDebugSystemObjects4));

extern IDebugClient5Ptr         gDebugClient;
extern IDebugControl7Ptr        gDebugControl;
extern IDebugAdvanced3Ptr       gDebugAdvanced;
extern IDebugSymbols3Ptr        gDebugSymbols;
extern IDebugDataSpaces4Ptr     gDebugDataSpaces;
extern IDebugRegisters2Ptr      gDebugRegisters;
extern IDebugSystemObjects4Ptr  gDebugSystemObjects;
extern ULONG                    gPointerWidth;

namespace detail {

template <typename CharType>
class DebugOutput
{
public:

  template<typename... Args>
  static inline HRESULT
  dvprintf(ULONG aOutputControl, ULONG aMask, CharType* aFmt, Args&&... aArgs);
};

template <>
class DebugOutput<const char>
{
public:
  template <typename... Args>
  static inline HRESULT
  dvprintf(ULONG aOutputControl, ULONG aMask, const char* aFmt, Args&&... aArgs)
  {
    return gDebugControl->ControlledOutput(aOutputControl, aMask, aFmt,
                                           std::forward<Args>(aArgs)...);
  }
};

template <>
class DebugOutput<const wchar_t>
{
public:
  template <typename... Args>
  static inline HRESULT
  dvprintf(ULONG aOutputControl, ULONG aMask, const wchar_t* aFmt,
           Args&&... aArgs)
  {
    return gDebugControl->ControlledOutputWide(aOutputControl, aMask, aFmt,
                                               std::forward<Args>(aArgs)...);
  }
};

} // namespace detail

template <typename CharType, typename... Args>
inline HRESULT
dprintf(CharType* aFmt, Args&&... aArgs)
{
  return detail::DebugOutput<CharType>::dvprintf(DEBUG_OUTCTL_ALL_OTHER_CLIENTS,
                                                 DEBUG_OUTPUT_NORMAL, aFmt,
                                                 std::forward<Args>(aArgs)...);
}

template <typename CharType, typename... Args>
inline HRESULT
dmlprintf(CharType* aFmt, Args&&... aArgs)
{
  return detail::DebugOutput<CharType>::dvprintf(
      DEBUG_OUTCTL_ALL_OTHER_CLIENTS | DEBUG_OUTCTL_DML, DEBUG_OUTPUT_NORMAL,
      aFmt, std::forward<Args>(aArgs)...);
}

template <typename CharType, typename... Args>
inline HRESULT
symprintf(CharType* aFmt, Args&&... aArgs)
{
  return detail::DebugOutput<CharType>::dvprintf(
      DEBUG_OUTCTL_ALL_OTHER_CLIENTS,
      DEBUG_OUTPUT_VERBOSE | DEBUG_OUTPUT_SYMBOLS,
      aFmt, std::forward<Args>(aArgs)...);
}

inline bool
ToUTF16(const std::string& aStr, std::wstring& aWideStr,
        const UINT aCodePage = CP_UTF8)
{
  int len = MultiByteToWideChar(aCodePage, 0, aStr.c_str(), aStr.length(),
                                nullptr, 0);
  if (!len) {
    return false;
  }
  auto wideBuf = std::make_unique<wchar_t[]>(len);
  len = MultiByteToWideChar(aCodePage, 0, aStr.c_str(), aStr.length(),
                            wideBuf.get(), len);
  if (!len) {
    return false;
  }
  aWideStr.assign(wideBuf.get(), len);
  return true;
}

inline bool
ToChar(const std::wstring& aWideStr, std::string& aStr,
       const UINT aCodePage = CP_UTF8)
{
  int len = WideCharToMultiByte(aCodePage,
                                WC_ERR_INVALID_CHARS | WC_NO_BEST_FIT_CHARS,
                                aWideStr.c_str(), aWideStr.length(), nullptr,
                                0, nullptr, nullptr);
  if (!len) {
    return false;
  }
  auto buf = std::make_unique<char[]>(len);
  len = WideCharToMultiByte(aCodePage,
                            WC_ERR_INVALID_CHARS | WC_NO_BEST_FIT_CHARS,
                            aWideStr.c_str(), aWideStr.length(), buf.get(), len,
                            nullptr, nullptr);
  if (!len) {
    return false;
  }
  aStr.assign(buf.get(), len);
  return true;
}

inline std::wstring
GetModuleName(ULONG64 const aBase)
{
  ULONG reallen = 0;
  HRESULT hr = gDebugSymbols->GetModuleNameStringWide(DEBUG_MODNAME_MODULE,
                                                      DEBUG_ANY_ID, aBase,
                                                      nullptr, 0, &reallen);
  if (FAILED(hr) || hr != S_FALSE) {
    return L"(Error retrieving module name size)";
  }

  auto buf = std::make_unique<wchar_t[]>(reallen);
  hr = gDebugSymbols->GetModuleNameStringWide(DEBUG_MODNAME_MODULE,
                                              DEBUG_ANY_ID, aBase,
                                              buf.get(), reallen, nullptr);
  if (FAILED(hr)) {
    return L"(Error retrieving module name)";
  }

  return std::wstring(buf.get());
}

template <typename T, size_t N>
size_t ArrayLength(T (&aArray)[N])
{
  return N;
}

inline HRESULT
GetFieldOffset(PCSTR aModuleName, PCSTR aTypeName, PCSTR aFieldName,
               PULONG aOffset)
{
  if (!aModuleName || !aTypeName || !aFieldName || !aOffset) {
    return E_INVALIDARG;
  }

  *aOffset = 0;

  ULONG64 modBase;
  HRESULT hr = gDebugSymbols->GetModuleByModuleName(aModuleName, 0, 0, &modBase);
  if (FAILED(hr)) {
    return hr;
  }

  ULONG typeId;
  hr = gDebugSymbols->GetTypeId(modBase, aTypeName, &typeId);
  if (FAILED(hr)) {
    return hr;
  }

  return gDebugSymbols->GetFieldOffset(modBase, typeId, aFieldName, aOffset);
}

#endif // __MOZDBGEXT_H

