#include "mozdbgext.h"
#include <stddef.h> // for offsetof

/*
#define DECLARE_API(s)                             \
    CPPMOD VOID                                    \
    s(                                             \
        HANDLE                 hCurrentProcess,    \
        HANDLE                 hCurrentThread,     \
        ULONG64                dwCurrentPc,        \
        ULONG                  dwProcessor,        \
        PCSTR                  args                \
     )
*/

namespace {

const char sStackTraceDatabaseSymbolName[] = "ntdll!RtlpStackTraceDataBase";
const char sStackTraceDatabaseTypeName[] = "ntdll!_STACK_TRACE_DATABASE";
const char sStackTraceEntryFieldName[] = "EntryIndexArray";
const char sStackTraceEntryTypeName[] = "ntdll!_RTL_STACK_TRACE_ENTRY";
const char sStackTraceBacktraceFieldName[] = "BackTrace";
const char sStackTraceDepthFieldName[] = "Depth";

bool gStackTraceDbInit = false;
ULONG64 gDbOffset = 0;
ULONG gEntryArrayOffset = 0;

void** QueryStackTraceDatabase(WORD aIndexHigh, WORD aIndexLow)
{
  HRESULT hr;
  dprintf("index is 0x%hd%hd\n", aIndexHigh, aIndexLow);
  if (!gStackTraceDbInit) {
    hr = gDebugSymbols->GetOffsetByName(sStackTraceDatabaseSymbolName,
                                        &gDbOffset);
    if (FAILED(hr)) {
      dprintf("GetOffsetByName(\"%s\") failed\n",
              sStackTraceDatabaseSymbolName);
      return nullptr;
    }
    ULONG stackTraceDbTypeId = 0;
    ULONG64 stackTraceDbModule = 0;
    hr = gDebugSymbols->GetSymbolTypeId(sStackTraceDatabaseTypeName,
                                        &stackTraceDbTypeId,
                                        &stackTraceDbModule);
    if (FAILED(hr)) {
      dprintf("GetSymbolTypeId(\"%s\") failed\n", sStackTraceDatabaseTypeName);
      return nullptr;
    }
    ULONG entryArrayTypeId = 0;
    hr = gDebugSymbols->GetFieldTypeAndOffset(stackTraceDbModule,
                                              stackTraceDbTypeId,
                                              sStackTraceEntryFieldName,
                                              &entryArrayTypeId,
                                              &gEntryArrayOffset);
    if (FAILED(hr)) {
      dprintf("GetFieldTypeAndOffset(\"%s\") failed\n",
              sStackTraceEntryFieldName);
      return nullptr;
    }
    ULONG64 dbStruct = 0;
    hr = gDebugDataSpaces->ReadPointersVirtual(1, gDbOffset, &dbStruct);
    if (FAILED(hr)) {
      dprintf("ReadPointersVirtual(dbStruct) failed\n");
      return nullptr;
    }
    ULONG64 entryAddress = 0;
    hr = gDebugDataSpaces->ReadPointersVirtual(1, dbStruct + gEntryArrayOffset,
                                               &entryAddress);
    if (FAILED(hr)) {
      dprintf("ReadPointersVirtual(\"%s\") failed\n",
              sStackTraceEntryFieldName);
      return nullptr;
    }
    DWORD index = static_cast<DWORD>(aIndexHigh) << 16 | aIndexLow;
    entryAddress -= index * gPointerWidth;
    // Now we have the address of the _RTL_STACK_TRACE_ENTRY
    ULONG64 entryPointer = 0;
    hr = gDebugDataSpaces->ReadPointersVirtual(1, entryAddress, &entryPointer);
    if (FAILED(hr)) {
      dprintf("ReadPointersVirtual(0x%0llX) failed with HRESULT 0x%08X\n", entryAddress, hr);
      return nullptr;
    }
    ULONG stackTraceEntryTypeId = 0;
    hr = gDebugSymbols->GetSymbolTypeId(sStackTraceEntryTypeName,
                                        &stackTraceEntryTypeId, nullptr);
    if (FAILED(hr)) {
      dprintf("GetSymbolTypeId(\"%s\") failed\n", sStackTraceEntryTypeName);
      return nullptr;
    }
    ULONG depthOffset = 0;
    hr = gDebugSymbols->GetFieldOffset(stackTraceDbModule,
                                       stackTraceEntryTypeId,
                                       sStackTraceDepthFieldName,
                                       &depthOffset);
    if (FAILED(hr)) {
      dprintf("GetFieldOffset(\"%s\") failed\n", sStackTraceDepthFieldName);
      return nullptr;
    }
    ULONG backtraceOffset = 0;
    hr = gDebugSymbols->GetFieldOffset(stackTraceDbModule,
                                       stackTraceEntryTypeId,
                                       sStackTraceBacktraceFieldName,
                                       &backtraceOffset);
    if (FAILED(hr)) {
      dprintf("GetFieldOffset(\"%s\") failed\n", sStackTraceBacktraceFieldName);
      return nullptr;
    }
    USHORT depth = 0;
    ULONG64 depthPointer = entryPointer + depthOffset;
    hr = gDebugDataSpaces->ReadVirtual(depthPointer, &depth, sizeof(depth),
                                       nullptr);
    if (FAILED(hr)) {
      dprintf("ReadVirtual(\"%s\") failed\n", sStackTraceDepthFieldName);
      return nullptr;
    }
    dprintf("Depth is %hd\n", depth);
    ULONG64 backtracePointer = entryPointer + backtraceOffset;
    ULONG64 backtrace[32] = {0};
    hr = gDebugDataSpaces->ReadPointersVirtual(32, backtracePointer, backtrace);
    if (FAILED(hr)) {
      dprintf("ReadPointersVirtual(backtrace) failed\n");
      return nullptr;
    }
    ULONG nameLen = 128;
    char* name = (char*)malloc(nameLen);
    if (!name) {
      return nullptr;
    }
    ULONG reqdNameLen = 0;
    ULONG64 disp = 0;
    for (int i = 0; i < depth; ++i) {
      hr = gDebugSymbols->GetNameByOffset(backtrace[i], name, nameLen,
                                          &reqdNameLen, &disp);
      if (S_FALSE == hr) {
        char* newName = (char*) realloc(name, reqdNameLen);
        if (newName) {
          name = newName;
          nameLen = reqdNameLen;
        }
        hr = gDebugSymbols->GetNameByOffset(backtrace[i], name, nameLen,
                                            &reqdNameLen, &disp);
      }
      if (S_OK == hr) {
        dprintf("%d: 0x%llX %s+0x%llX\n", i, backtrace[i], name, disp);
      } else {
        dprintf("%d: 0x%llX\n", i, backtrace[i]);
      }
    }
    free(name); name = nullptr;
    // gStackTraceDbInit = true;
  }
  return nullptr;
}

const char sSymbolName[] = "ntdll!RtlCriticalSectionList";

} // anonymous namespace

DECLARE_API(__declspec(dllexport) mozmutex)
{
  ULONG64 csListOffset = 0;
  HRESULT hr = gDebugSymbols->GetOffsetByName(sSymbolName, &csListOffset);
  if (FAILED(hr)) {
    dprintf("GetOffsetByName failed\n");
    return;
  }
  const size_t linkOffset = offsetof(RTL_CRITICAL_SECTION_DEBUG,
                                     ProcessLocksList);
  LIST_ENTRY csListHead;
  ULONG bytesRead;
  hr = gDebugDataSpaces->ReadVirtual(csListOffset, &csListHead,
                                     sizeof(csListHead), &bytesRead);
  if (FAILED(hr)) {
    dprintf("ReadVirtual of %s failed\n", sSymbolName);
    return;
  }

  ULONG64 offset = (ULONG64) csListHead.Flink;
  RTL_CRITICAL_SECTION_DEBUG csDebug;
  while (offset != csListOffset) {
    offset -= linkOffset;
    hr = gDebugDataSpaces->ReadVirtual(offset, &csDebug, sizeof(csDebug),
                                       &bytesRead);
    if (FAILED(hr)) {
      dprintf("ReadVirtual failed\n");
      return;
    }
    dprintf("CRITICAL_SECTION found at 0x%p\n", csDebug.CriticalSection);
    QueryStackTraceDatabase(csDebug.CreatorBackTraceIndexHigh,
                            csDebug.CreatorBackTraceIndex);
    /*
    ULONG typeId;
    ULONG64 module;
    char nameBuf[1024] = {0};
    ULONG nameBufLen = sizeof(nameBuf) - 1;
    hr = gDebugSymbols->GetOffsetTypeId((ULONG64)csDebug.CriticalSection,
                                        &typeId, &module);
    if (SUCCEEDED(hr)) {
      hr = gDebugSymbols->GetTypeName(module, typeId, nameBuf, nameBufLen,
                                      &nameBufLen);
      if (SUCCEEDED(hr)) {
        dprintf("Nearest symbol: %s\n", nameBuf);
      }
    }
    */
    offset = (ULONG64) csDebug.ProcessLocksList.Flink;
  }
}

