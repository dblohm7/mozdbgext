#include "mozdbgext.h"

// Naming this "ExtensionApis" is important - some wdbgexts macros depend on it
WINDBG_EXTENSION_APIS64  ExtensionApis;

IDebugClient5Ptr      gDebugClient;
IDebugControl7Ptr     gDebugControl;
IDebugAdvanced3Ptr    gDebugAdvanced;
IDebugSymbols3Ptr     gDebugSymbols;
IDebugDataSpaces4Ptr  gDebugDataSpaces;
ULONG                 gPointerWidth = 4; // Pointer width in bytes

extern "C" __declspec(dllexport) HRESULT
DebugExtensionInitialize(PULONG aVersion, PULONG aFlags)
{
  *aVersion = DEBUG_EXTENSION_VERSION(1, 0);
  *aFlags = 0;

  HRESULT hr = DebugCreate(__uuidof(IDebugClient5), (void**)&gDebugClient);
  if (FAILED(hr)) {
    dprintf("DebugCreate failed\n");
    return hr;
  }

  hr = gDebugClient->QueryInterface(__uuidof(IDebugControl7),
                                    (void**)&gDebugControl);
  if (!gDebugControl) {
    dprintf("QueryInterface(IDebugControl7) failed\n");
    return hr;
  }

  ExtensionApis.nSize = sizeof(ExtensionApis);
  hr = gDebugControl->GetWindbgExtensionApis64(&ExtensionApis);
  if (FAILED(hr)) {
    return hr;
  }

  hr = gDebugControl->IsPointer64Bit();
  if (hr == S_OK) {
    gPointerWidth = 8;
  }

  hr = gDebugClient->QueryInterface(__uuidof(IDebugAdvanced3),
                                    (void**)&gDebugAdvanced);
  if (!gDebugAdvanced) {
    dprintf("QueryInterface(IDebugAdvanced3) failed\n");
    return hr;
  }

  hr = gDebugClient->QueryInterface(__uuidof(IDebugSymbols3),
                                    (void**)&gDebugSymbols);
  if (!gDebugSymbols) {
    dprintf("QueryInterface(IDebugSymbols3) failed\n");
    return hr;
  }

  hr = gDebugClient->QueryInterface(__uuidof(IDebugDataSpaces4),
                                    (void**)&gDebugDataSpaces);
  if (!gDebugDataSpaces) {
    dprintf("QueryInterface(IDebugDataSpaces4) failed\n");
    return hr;
  }

  return S_OK;
}

