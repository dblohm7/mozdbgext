#ifndef __MOZDBGEXT_H
#define __MOZDBGEXT_H

#define KDEXT_64BIT
#include <windows.h>
#include <comdef.h>
#include "dbgeng.h"
#include "dbghelp.h"

#include <utility>

_COM_SMARTPTR_TYPEDEF(IDebugClient5, __uuidof(IDebugClient5));
_COM_SMARTPTR_TYPEDEF(IDebugControl7, __uuidof(IDebugControl7));
_COM_SMARTPTR_TYPEDEF(IDebugAdvanced3, __uuidof(IDebugAdvanced3));
_COM_SMARTPTR_TYPEDEF(IDebugSymbols3, __uuidof(IDebugSymbols3));
_COM_SMARTPTR_TYPEDEF(IDebugDataSpaces4, __uuidof(IDebugDataSpaces4));

extern IDebugClient5Ptr     gDebugClient;
extern IDebugControl7Ptr    gDebugControl;
extern IDebugAdvanced3Ptr   gDebugAdvanced;
extern IDebugSymbols3Ptr    gDebugSymbols;
extern IDebugDataSpaces4Ptr gDebugDataSpaces;
extern ULONG                gPointerWidth;

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

#endif // __MOZDBGEXT_H

