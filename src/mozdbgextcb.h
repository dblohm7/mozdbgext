#ifndef __MOZDBGEXTCB_H
#define __MOZDBGEXTCB_H

#define KDEXT_64BIT
#include "dbgeng.h"

#include <functional>
#include <vector>

namespace mozilla {

class DbgExtCallbacks : public IDebugEventCallbacksWide
{
public:
  STDMETHODIMP QueryInterface(REFIID aIID, PVOID* aOutInterface);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  STDMETHODIMP GetInterestMask(PULONG aMask);
  STDMETHODIMP Breakpoint(PDEBUG_BREAKPOINT2 aBp);
  STDMETHODIMP Exception(PEXCEPTION_RECORD64 aException, ULONG aFirstChance);
  STDMETHODIMP CreateThread(ULONG64 aHandle, ULONG64 aDataOffset,
                            ULONG64 aStartOffset);
  STDMETHODIMP ExitThread(ULONG aExitCode);
  STDMETHODIMP CreateProcess(ULONG64 aImageFileHandle, ULONG64 aHandle,
                             ULONG64 aBaseOffset, ULONG aModuleSize,
                             PCWSTR aModuleName, PCWSTR aImageName,
                             ULONG aCheckSum, ULONG aTimeDateStamp,
                             ULONG64 aInitialThreadHandle,
                             ULONG64 aThreadDataOffset, ULONG64 aStartOffset);
  STDMETHODIMP ExitProcess(ULONG aExitCode);
  STDMETHODIMP LoadModule(ULONG64 aImageFileHandle, ULONG64 aBaseOffset,
                          ULONG aModuleSize, PCWSTR aModuleName,
                          PCWSTR aImageName, ULONG aChecksum,
                          ULONG aTimeDateStamp);
  STDMETHODIMP UnloadModule(PCWSTR aImageBaseName, ULONG64 aBaseOffset);
  STDMETHODIMP SystemError(ULONG aError, ULONG aLevel);
  STDMETHODIMP SessionStatus(ULONG aStatus);
  STDMETHODIMP ChangeDebuggeeState(ULONG aFlags, ULONG64 aArgument);
  STDMETHODIMP ChangeEngineState(ULONG aFlags, ULONG64 aArgument);
  STDMETHODIMP ChangeSymbolState(ULONG aFlags, ULONG64 aArgument);

  typedef std::function<void (PCWSTR,ULONG64,bool)> ModuleEventListenerFn;
  static bool RegisterModuleEventListener(ModuleEventListenerFn aListener);
  static bool DeregisterModuleEventListener(ModuleEventListenerFn aListener);

  typedef std::function<void (ULONG)> ProcessDetachListenerFn;
  static bool RegisterProcessDetachListener(ProcessDetachListenerFn aListener);
  static bool DeregisterProcessDetachListener(ProcessDetachListenerFn aListener);

private:
  DbgExtCallbacks();
  virtual ~DbgExtCallbacks();
  HRESULT EnumerateProcesses(std::vector<ULONG>& aPids);

  ULONG   mRefCnt;

  std::vector<ModuleEventListenerFn> mModuleEventListeners;
  std::vector<ProcessDetachListenerFn> mProcessDetachListeners;

  static DbgExtCallbacks* sInstance;
};

} // namespace mozilla

#endif // __MOZDBGEXTCB_H

