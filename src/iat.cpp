#include "mozdbgext.h"
#include "bpsyms.h"
#include "pe.h"

#include <stddef.h>
#include <sstream>
#include <string>

static std::wstring
GetStringFromTarget(ULONG64 const aOffset)
{
  ULONG strLen = 0;
  HRESULT hr = gDebugDataSpaces->ReadMultiByteStringVirtualWide(aOffset,
                                  MAX_PATH, CP_UTF8, nullptr, strLen, &strLen);
  if (FAILED(hr)) {
    return std::wstring();
  }

  auto str = std::make_unique<wchar_t[]>(strLen);
  hr = gDebugDataSpaces->ReadMultiByteStringVirtualWide(aOffset, MAX_PATH,
                                          CP_UTF8, str.get(), strLen, &strLen);
  if (hr != S_OK) {
    return std::wstring();
  }
  return std::wstring(str.get());
}

template <typename ThunkDataT, typename OrdinalT, OrdinalT OrdinalFlag>
static bool
WalkImageThunkData(ULONG64 const aModuleBase,
                   IMAGE_IMPORT_DESCRIPTOR const &aImgDesc,
                   ULONG64 const aPtr,
                   std::wstring& aName)
{
  ULONG64 thunk = aModuleBase + aImgDesc.FirstThunk;
  if (thunk > aPtr) {
    return false;
  }
  HRESULT hr;
  ThunkDataT thunkData;
  ULONG index = 0;
  while (thunk < aPtr) {
    hr = gDebugDataSpaces->ReadVirtual(thunk, &thunkData, sizeof(ThunkDataT), nullptr);
    if (FAILED(hr) || thunkData.u1.Ordinal == 0) {
      return false;
    }
    ++index;
    thunk += sizeof(ThunkDataT);
  }
  ULONG64 importLookupTable = aModuleBase + aImgDesc.OriginalFirstThunk;
  ULONG64 iltEntryBase = importLookupTable + index * sizeof(ThunkDataT);
  ThunkDataT iltEntry;
  hr = gDebugDataSpaces->ReadVirtual(iltEntryBase, &iltEntry,
                                     sizeof(ThunkDataT), nullptr);
  if (FAILED(hr)) {
    return false;
  }
  if (iltEntry.u1.Ordinal & OrdinalFlag) {
    std::wostringstream oss;
    oss << L"Ordinal " << (iltEntry.u1.Ordinal & (~OrdinalFlag));
    aName = oss.str();
  } else {
    ULONG64 nameOffset = aModuleBase + iltEntry.u1.AddressOfData + offsetof(IMAGE_IMPORT_BY_NAME, Name);
    aName = GetStringFromTarget(nameOffset);
    if (aName.empty()) {
      return false;
    }
  }
  return true;
}

HRESULT CALLBACK
iat(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  // This doesn't evaluate breakpad
  DEBUG_VALUE dv;
  HRESULT hr = gDebugControl->Evaluate(aArgs, DEBUG_VALUE_INT64, &dv, nullptr);
  if (FAILED(hr)) {
    return hr;
  }
  ULONG64 ptr = dv.I64;
  // Find the enclosing module
  ULONG moduleIndex;
  ULONG64 moduleBase;
  hr = gDebugSymbols->GetModuleByOffset(ptr, 0, &moduleIndex, &moduleBase);
  if (FAILED(hr)) {
    return hr;
  }
  ULONG64 importBase;
  ULONG importSize;
  if (!GetDataDirectoryEntry(moduleBase, IMAGE_DIRECTORY_ENTRY_IMPORT,
                             importBase, importSize)) {
    return E_FAIL;
  }
  ULONG numImportDescriptors = importSize / sizeof(IMAGE_IMPORT_DESCRIPTOR);
  auto importDescriptors = std::make_unique<IMAGE_IMPORT_DESCRIPTOR[]>(numImportDescriptors);
  hr = gDebugDataSpaces->ReadVirtual(importBase, importDescriptors.get(), importSize, nullptr);
  if (FAILED(hr)) {
    return hr;
  }
  std::wstring moduleName;
  std::wstring functionName;
  for (ULONG i = 0; i < numImportDescriptors; ++i) {
    if (!importDescriptors[i].OriginalFirstThunk) {
      continue;
    }
    if (gPointerWidth == 4) {
      if (!WalkImageThunkData<IMAGE_THUNK_DATA32, DWORD, IMAGE_ORDINAL_FLAG32>(moduleBase, importDescriptors[i], ptr, functionName)) {
        continue;
      }
    } else {
      if (!WalkImageThunkData<IMAGE_THUNK_DATA64, ULONGLONG, IMAGE_ORDINAL_FLAG64>(moduleBase, importDescriptors[i], ptr, functionName)) {
        continue;
      }
    }
    ULONG64 nameOffset = moduleBase + importDescriptors[i].Name;
    moduleName = GetStringFromTarget(nameOffset);
    if (moduleName.empty()) {
      return E_FAIL;
    }
    dprintf("Expected target: %S!%S\n", moduleName.c_str(), functionName.c_str());
    ULONG64 actualTarget = 0;
    hr = gDebugDataSpaces->ReadPointersVirtual(1, ptr, &actualTarget);
    if (FAILED(hr)) {
      return hr;
    }
    std::string actualSymbol;
    ULONG64 actualOffset;
    if (NearestSymbol(actualTarget, actualSymbol, actualOffset)) {
      dprintf("Actual target: %s\n", actualSymbol.c_str());
    }
    return S_OK;
  }
  return E_FAIL;
}

