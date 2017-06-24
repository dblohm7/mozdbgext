#include "mozdbgext.h"

static HRESULT
GetTEBFieldOffset(PCSTR aFieldName, PULONG aOffset)
{
  return GetFieldOffset("ntdll", "_TEB", aFieldName, aOffset);
}

HRESULT CALLBACK
actctx(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  ULONG64 tebAddress;
  HRESULT hr = gDebugSystemObjects->GetCurrentThreadTeb(&tebAddress);
  if (FAILED(hr)) {
    return hr;
  }

  ULONG actCtxStackPtrOffset;
  hr = GetTEBFieldOffset("ActivationContextStackPointer", &actCtxStackPtrOffset);
  if (FAILED(hr)) {
    return hr;
  }

  ULONG64 stackPtr;
  hr = gDebugDataSpaces->ReadPointersVirtual(1,
                                             tebAddress + actCtxStackPtrOffset,
                                             &stackPtr);
  if (FAILED(hr)) {
    return hr;
  }

  ULONG activeFrameOffset;
  hr = GetFieldOffset("ntdll", "_ACTIVATION_CONTEXT_STACK", "ActiveFrame",
                      &activeFrameOffset);
  if (FAILED(hr)) {
    return hr;
  }

  ULONG64 activeFramePtr;
  hr = gDebugDataSpaces->ReadPointersVirtual(1, stackPtr + activeFrameOffset,
                                             &activeFramePtr);
  if (FAILED(hr)) {
    return hr;
  }

  ULONG prevFrameOffset;
  hr = GetFieldOffset("ntdll", "_RTL_ACTIVATION_CONTEXT_STACK_FRAME", "Previous",
                      &prevFrameOffset);
  if (FAILED(hr)) {
    return hr;
  }

  ULONG actCtxOffset;
  hr = GetFieldOffset("ntdll", "_RTL_ACTIVATION_CONTEXT_STACK_FRAME",
                      "ActivationContext", &actCtxOffset);
  if (FAILED(hr)) {
    return hr;
  }

  ULONG index = 0;
  ULONG64 curActCtxFramePtr = activeFramePtr;
  do {
    ULONG64 curActCtx;
    hr = gDebugDataSpaces->ReadPointersVirtual(1,
                                               curActCtxFramePtr + actCtxOffset,
                                               &curActCtx);
    if (FAILED(hr)) {
      return hr;
    }
    dprintf("%02X %p\n", index, curActCtx);
    ++index;
    hr = gDebugDataSpaces->ReadPointersVirtual(1,
                                               curActCtxFramePtr + prevFrameOffset,
                                               &curActCtxFramePtr);
  } while (SUCCEEDED(hr) && curActCtxFramePtr);

  return hr;
}

