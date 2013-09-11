#include "mozdbgext.h"

namespace {

MOZDBGEXT_EXTENSION_APIS  sExtensionApis;
USHORT                    sMajorVersion;
USHORT                    sMinorVersion;
EXT_API_VERSION           sExtApiVersion = { 1, 0, MOZDBGEXT_VERSION_REVISION };

} // anonymous namespace


IDebugClientPtr     sDebugClient;
IDebugControl4Ptr   sDebugControl;
IDebugAdvanced3Ptr  sDebugAdvanced;

extern "C" __declspec(dllexport) VOID
WinDbgExtensionDllInit(MOZDBGEXT_EXTENSION_APIS* aExtensionApis,
                       USHORT aMajorVersion, USHORT aMinorVersion)
{
  sExtensionApis = *aExtensionApis;
  sMajorVersion = aMajorVersion;
  sMinorVersion = aMinorVersion;

  HRESULT hr = DebugCreate(__uuidof(IDebugClient), (void**)&sDebugClient);
  if (FAILED(hr)) {
    sExtensionApis.lpOutputRoutine("DebugCreate failed\n");
    return;
  }
  sDebugClient->QueryInterface(__uuidof(IDebugControl4),
                               (void**)&sDebugControl);
  if (!sDebugControl) {
    sExtensionApis.lpOutputRoutine("QueryInterface(IDebugControl4) failed\n");
    return;
  }
  sDebugClient->QueryInterface(__uuidof(IDebugAdvanced3),
                               (void**)&sDebugAdvanced);
  if (!sDebugAdvanced) {
    sExtensionApis.lpOutputRoutine("QueryInterface(IDebugAdvanced3) failed\n");
    return;
  }
}

extern "C" __declspec(dllexport) LPEXT_API_VERSION
ExtensionApiVersion(void)
{
  return &sExtApiVersion;
}

