#define INITGUID
#include "mozdbgext.h"
#include "mozdbgextcb.h"

#include <algorithm>

namespace mozilla {

DbgExtCallbacks* DbgExtCallbacks::sInstance = nullptr;

bool
DbgExtCallbacks::RegisterModuleEventListener(ModuleEventListenerFn aListener)
{
  if (!sInstance) {
    sInstance = new DbgExtCallbacks();
    HRESULT hr = gDebugClient->SetEventCallbacksWide(sInstance);
    if (FAILED(hr)) {
      delete sInstance;
      sInstance = nullptr;
      return false;
    }
  }
  sInstance->mModuleEventListeners.push_back(aListener);
  return true;
}

bool
DbgExtCallbacks::DeregisterModuleEventListener(ModuleEventListenerFn aListener)
{
  if (!sInstance) {
    return false;
  }
  // No-op for now
  // return sInstance->mModuleEventListeners.erase(aListener) > 0;
  return true;
}

bool
DbgExtCallbacks::RegisterProcessDetachListener(ProcessDetachListenerFn aListener)
{
  if (!sInstance) {
    sInstance = new DbgExtCallbacks();
    HRESULT hr = gDebugClient->SetEventCallbacksWide(sInstance);
    if (FAILED(hr)) {
      delete sInstance;
      sInstance = nullptr;
      return false;
    }
  }
  sInstance->mProcessDetachListeners.push_back(aListener);
  return true;
}

bool
DbgExtCallbacks::DeregisterProcessDetachListener(ProcessDetachListenerFn aListener)
{
  if (!sInstance) {
    return false;
  }
  // No-op for now
  // return sInstance->mProcessDetachListeners.erase(aListener) > 0;
  return true;
}

DbgExtCallbacks::DbgExtCallbacks()
  : mRefCnt(1)
{
}

DbgExtCallbacks::~DbgExtCallbacks()
{
}

STDMETHODIMP
DbgExtCallbacks::QueryInterface(REFIID aIID, PVOID* aOutInterface)
{
  IUnknown* punk = nullptr;
  if (aIID == IID_IUnknown) {
    punk = static_cast<IUnknown*>(this);
  } else if (aIID == IID_IDebugEventCallbacksWide) {
    punk = static_cast<IDebugEventCallbacksWide*>(this);
  }

  *aOutInterface = punk;
  if (!punk) {
    return E_NOINTERFACE;
  }
  punk->AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG)
DbgExtCallbacks::AddRef()
{
  return ++mRefCnt;
}

STDMETHODIMP_(ULONG)
DbgExtCallbacks::Release()
{
  ULONG result = --mRefCnt;
  if (result == 0) {
    delete this;
  } else if (result == 1 && sInstance == this) {
    auto inst = sInstance;
    sInstance = nullptr;
    inst->Release();
  }
  return result;
}

STDMETHODIMP
DbgExtCallbacks::GetInterestMask(PULONG aMask)
{
  if (!aMask) {
    return E_INVALIDARG;
  }
  *aMask = DEBUG_EVENT_LOAD_MODULE | DEBUG_EVENT_UNLOAD_MODULE |
           DEBUG_EVENT_EXIT_PROCESS | DEBUG_EVENT_SESSION_STATUS |
           DEBUG_EVENT_CHANGE_ENGINE_STATE;
  return S_OK;
}

STDMETHODIMP
DbgExtCallbacks::Breakpoint(PDEBUG_BREAKPOINT2 aBp)
{
  return E_NOTIMPL;
}

STDMETHODIMP
DbgExtCallbacks::Exception(PEXCEPTION_RECORD64 aException, ULONG aFirstChance)
{
  return E_NOTIMPL;
}

STDMETHODIMP
DbgExtCallbacks::CreateThread(ULONG64 aHandle, ULONG64 aDataOffset,
                              ULONG64 aStartOffset)
{
  return E_NOTIMPL;
}

STDMETHODIMP
DbgExtCallbacks::ExitThread(ULONG aExitCode)
{
  return E_NOTIMPL;
}

STDMETHODIMP
DbgExtCallbacks::CreateProcess(ULONG64 aImageFileHandle, ULONG64 aHandle,
                               ULONG64 aBaseOffset, ULONG aModuleSize,
                               PCWSTR aModuleName, PCWSTR aImageName,
                               ULONG aCheckSum, ULONG aTimeDateStamp,
                               ULONG64 aInitialThreadHandle,
                               ULONG64 aThreadDataOffset, ULONG64 aStartOffset)
{
  return E_NOTIMPL;
}

STDMETHODIMP
DbgExtCallbacks::ExitProcess(ULONG aExitCode)
{
  ULONG debuggerPid;
  HRESULT hr = gDebugSystemObjects->GetEventProcess(&debuggerPid);
  if (FAILED(hr)) {
    return hr;
  }

  std::for_each(mProcessDetachListeners.begin(),
                mProcessDetachListeners.end(),
                [&](ProcessDetachListenerFn fn) {
    fn(debuggerPid);
  });
  return S_OK;
}

STDMETHODIMP
DbgExtCallbacks::LoadModule(ULONG64 aImageFileHandle, ULONG64 aBaseOffset,
                            ULONG aModuleSize, PCWSTR aModuleName,
                            PCWSTR aImageName, ULONG aChecksum,
                            ULONG aTimeDateStamp)
{
  std::for_each(mModuleEventListeners.begin(),
                mModuleEventListeners.end(),
                [&](ModuleEventListenerFn fn) {
    fn(aImageName, aBaseOffset, true);
  });
  return S_OK;
}

STDMETHODIMP
DbgExtCallbacks::UnloadModule(PCWSTR aImageBaseName, ULONG64 aBaseOffset)
{
  std::for_each(mModuleEventListeners.begin(),
                mModuleEventListeners.end(),
                [&](ModuleEventListenerFn fn) {
    fn(aImageBaseName, aBaseOffset, false);
  });
  return S_OK;
}

STDMETHODIMP
DbgExtCallbacks::SystemError(ULONG aError, ULONG aLevel)
{
  return E_NOTIMPL;
}

HRESULT
DbgExtCallbacks::EnumerateProcesses(std::vector<ULONG>& aPids)
{
  aPids.clear();

  ULONG numProcesses = 0;
  HRESULT hr = gDebugSystemObjects->GetNumberProcesses(&numProcesses);
  if (FAILED(hr)) {
    return hr;
  }

  aPids.resize(numProcesses);
  hr = gDebugSystemObjects->GetProcessIdsByIndex(0, numProcesses, &aPids[0],
                                                 nullptr);
  if (FAILED(hr)) {
    aPids.clear();
    return hr;
  }

  return S_OK;
}

STDMETHODIMP
DbgExtCallbacks::SessionStatus(ULONG aStatus)
{
  // We don't care about active sessions, only ending sessions
  if (aStatus == DEBUG_SESSION_ACTIVE) {
    return S_OK;
  }

  // All other values of aStatus imply that the session was ended.
  ULONG debuggerPid;
  HRESULT hr = gDebugSystemObjects->GetEventProcess(&debuggerPid);
  if (FAILED(hr)) {
    return hr;
  }

  std::for_each(mProcessDetachListeners.begin(),
                mProcessDetachListeners.end(),
                [&](ProcessDetachListenerFn fn) {
    fn(debuggerPid);
  });
  return S_OK;
}

STDMETHODIMP
DbgExtCallbacks::ChangeDebuggeeState(ULONG aFlags, ULONG64 aArgument)
{
  return E_NOTIMPL;
}

STDMETHODIMP
DbgExtCallbacks::ChangeEngineState(ULONG aFlags, ULONG64 aArgument)
{
  // We only care when a target has been removed.
  if (!(aFlags & DEBUG_CES_SYSTEMS) || aArgument != DEBUG_ANY_ID) {
    return S_OK;
  }

  ULONG debuggerPid;
  HRESULT hr = gDebugSystemObjects->GetEventProcess(&debuggerPid);
  if (FAILED(hr)) {
    return hr;
  }

  std::for_each(mProcessDetachListeners.begin(),
                mProcessDetachListeners.end(),
                [&](ProcessDetachListenerFn fn) {
    fn(debuggerPid);
  });
  return S_OK;
}

STDMETHODIMP
DbgExtCallbacks::ChangeSymbolState(ULONG aFlags, ULONG64 aArgument)
{
  return E_NOTIMPL;
}

} // namespace mozilla

