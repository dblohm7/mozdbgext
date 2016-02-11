#ifndef __BPSYMS_H
#define __BPSYMS_H

#include <windows.h>
#include <string>

bool
NearestSymbol(ULONG64 const aOffset, std::string& aOutput,
              ULONG64& aOutSymOffset, ULONG aFlags = 0);

#endif // __BPSYMS_H

