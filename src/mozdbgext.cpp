#include "mozdbgext.h"

IDebugClient5Ptr        gDebugClient;
IDebugControl7Ptr       gDebugControl;
IDebugAdvanced3Ptr      gDebugAdvanced;
IDebugSymbols3Ptr       gDebugSymbols;
IDebugDataSpaces4Ptr    gDebugDataSpaces;
IDebugRegisters2Ptr     gDebugRegisters;
IDebugSystemObjects4Ptr gDebugSystemObjects;
ULONG                   gPointerWidth = 4; // Pointer width in bytes

HRESULT
DebugExtensionInitialize(PULONG aVersion, PULONG aFlags)
{
  *aVersion = DEBUG_EXTENSION_VERSION(1, 0);
  *aFlags = 0;

  // TODO: Just use the IDebugClient passed into extension func
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

  hr = gDebugClient->QueryInterface(__uuidof(IDebugRegisters2),
                                    (void**)&gDebugRegisters);
  if (!gDebugRegisters) {
    dprintf("QueryInterface(IDebugRegisters2) failed\n");
    return hr;
  }

  hr = gDebugClient->QueryInterface(__uuidof(IDebugSystemObjects4),
                                    (void**)&gDebugSystemObjects);
  if (!gDebugSystemObjects) {
    dprintf("QueryInterface(IDebugSystemObjects4) failed\n");
    return hr;
  }

  return S_OK;
}

