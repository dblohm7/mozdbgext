#ifndef __MOZDBGEXT_H
#define __MOZDBGEXT_H

#if defined(_M_X64)
#define KDEXT_64BIT
#endif
#include <windows.h>
#include <comdef.h>
#include "dbgeng.h"
#include "dbghelp.h"
#include "wdbgexts.h"

#if defined(_M_IX86)

typedef WINDBG_EXTENSION_APIS MOZDBGEXT_EXTENSION_APIS;
#define MOZDBGEXT_VERSION_REVISION EXT_API_VERSION_NUMBER32

#elif defined(_M_X64)

typedef WINDBG_EXTENSION_APIS64 MOZDBGEXT_EXTENSION_APIS;
#define MOZDBGEXT_VERSION_REVISION EXT_API_VERSION_NUMBER64

#else
#error Unknown processor architecture
#endif

_COM_SMARTPTR_TYPEDEF(IDebugClient5, __uuidof(IDebugClient5));
_COM_SMARTPTR_TYPEDEF(IDebugControl4, __uuidof(IDebugControl4));
_COM_SMARTPTR_TYPEDEF(IDebugAdvanced3, __uuidof(IDebugAdvanced3));
_COM_SMARTPTR_TYPEDEF(IDebugSymbols3, __uuidof(IDebugSymbols3));
_COM_SMARTPTR_TYPEDEF(IDebugDataSpaces4, __uuidof(IDebugDataSpaces4));

extern IDebugClient5Ptr     sDebugClient;
extern IDebugControl4Ptr    sDebugControl;
extern IDebugAdvanced3Ptr   sDebugAdvanced;
extern IDebugSymbols3Ptr    sDebugSymbols;
extern IDebugDataSpaces4Ptr sDebugDataSpaces;
extern ULONG                sPointerWidth;

#endif // __MOZDBGEXT_H

