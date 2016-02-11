#ifndef __PE_H
#define __PE_H

#include <windows.h>

bool
GetDataDirectoryEntry(ULONG64 const aModuleBase, ULONG const aEntryIndex,
                      ULONG64& aEntryBase, ULONG& aEntrySize);

#endif // __PE_H

