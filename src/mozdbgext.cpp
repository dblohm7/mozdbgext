#include "mozdbgext.h"

namespace {

USHORT                    sMajorVersion;
USHORT                    sMinorVersion;
EXT_API_VERSION           sExtApiVersion = { 1, 0, MOZDBGEXT_VERSION_REVISION };

} // anonymous namespace

// Naming this "ExtensionApis" is important - some wdbgexts macros depend on it
MOZDBGEXT_EXTENSION_APIS  ExtensionApis;

IDebugClient5Ptr      sDebugClient;
IDebugControl4Ptr     sDebugControl;
IDebugAdvanced3Ptr    sDebugAdvanced;
IDebugSymbols3Ptr     sDebugSymbols;
IDebugDataSpaces4Ptr  sDebugDataSpaces;
ULONG                 sPointerWidth = 4; // Pointer width in bytes

extern "C" __declspec(dllexport) VOID
WinDbgExtensionDllInit(MOZDBGEXT_EXTENSION_APIS* aExtensionApis,
                       USHORT aMajorVersion, USHORT aMinorVersion)
{
  ExtensionApis = *aExtensionApis;
  sMajorVersion = aMajorVersion;
  sMinorVersion = aMinorVersion;

  HRESULT hr = DebugCreate(__uuidof(IDebugClient5), (void**)&sDebugClient);
  if (FAILED(hr)) {
    dprintf("DebugCreate failed\n");
    return;
  }
  sDebugClient->QueryInterface(__uuidof(IDebugControl4),
                               (void**)&sDebugControl);
  if (!sDebugControl) {
    dprintf("QueryInterface(IDebugControl4) failed\n");
    return;
  }
  hr = sDebugControl->IsPointer64Bit();
  if (hr == S_OK) {
    sPointerWidth = 8;
  }
  sDebugClient->QueryInterface(__uuidof(IDebugAdvanced3),
                               (void**)&sDebugAdvanced);
  if (!sDebugAdvanced) {
    dprintf("QueryInterface(IDebugAdvanced3) failed\n");
    return;
  }
  sDebugClient->QueryInterface(__uuidof(IDebugSymbols3),
                               (void**)&sDebugSymbols);
  if (!sDebugSymbols) {
    dprintf("QueryInterface(IDebugSymbols3) failed\n");
    return;
  }
  sDebugClient->QueryInterface(__uuidof(IDebugDataSpaces4),
                               (void**)&sDebugDataSpaces);
  if (!sDebugDataSpaces) {
    dprintf("QueryInterface(IDebugDataSpaces4) failed\n");
    return;
  }
}

extern "C" __declspec(dllexport) LPEXT_API_VERSION
ExtensionApiVersion(void)
{
  return &sExtApiVersion;
}

