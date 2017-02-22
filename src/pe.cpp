#include "pe.h"
#include "mozdbgext.h"

template <typename NTHeaderT>
static bool
GetDataDirectoryEntry(ULONG64 const aBase, ULONG64 const aNtHeaderBase,
                      ULONG const aEntryIndex, ULONG64& aDbgBase,
                      ULONG& aDbgSize)
{
  NTHeaderT ntHeader;
  HRESULT hr = gDebugDataSpaces->ReadVirtual(aNtHeaderBase, &ntHeader,
                                             sizeof(ntHeader), nullptr);
  if (FAILED(hr)) {
    return false;
  }
  IMAGE_DATA_DIRECTORY& dbgDataDir =
    ntHeader.OptionalHeader.DataDirectory[aEntryIndex];
  aDbgBase = aBase + dbgDataDir.VirtualAddress;
  aDbgSize = dbgDataDir.Size;
  return true;
}


bool
GetDataDirectoryEntry(ULONG64 const aModuleBase, ULONG const aEntryIndex,
                      ULONG64& aEntryBase, ULONG& aEntrySize)
{
  IMAGE_DOS_HEADER header;
  HRESULT hr = gDebugDataSpaces->ReadVirtual(aModuleBase, &header,
                                             sizeof(IMAGE_DOS_HEADER), nullptr);
  if (FAILED(hr)) {
    dprintf("Failed to read IMAGE_DOS_HEADER for module \"%S\"\n",
            GetModuleName(aModuleBase).c_str());
    return false;
  }
  ULONG64 ntBase = aModuleBase + header.e_lfanew;
  if (gPointerWidth == 4) {
    if (!GetDataDirectoryEntry<IMAGE_NT_HEADERS32>(aModuleBase, ntBase, aEntryIndex, aEntryBase, aEntrySize)) {
      dprintf("Failed to read IMAGE_NT_HEADERS for module \"%S\"\n",
              GetModuleName(aModuleBase).c_str());
      return false;
    }
  } else {
    if (!GetDataDirectoryEntry<IMAGE_NT_HEADERS64>(aModuleBase, ntBase, aEntryIndex, aEntryBase, aEntrySize)) {
      dprintf("Failed to read IMAGE_NT_HEADERS for module \"%S\"\n",
              GetModuleName(aModuleBase).c_str());
      return false;
    }
  }
  return true;
}
