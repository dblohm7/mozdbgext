#ifndef __MOZDBGEXT_H
#define __MOZDBGEXT_H

#define KDEXT_64BIT
#include <windows.h>
#include <comdef.h>
#include "dbgeng.h"
#include "dbghelp.h"

#define dprintf(fmt, ...) \
  gDebugControl->ControlledOutput(DEBUG_OUTCTL_ALL_OTHER_CLIENTS, DEBUG_OUTPUT_NORMAL, fmt, __VA_ARGS__)


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

#endif // __MOZDBGEXT_H

