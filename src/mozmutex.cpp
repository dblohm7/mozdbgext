#include "mozdbgext.h"

/*
#define DECLARE_API32(s)                           \
    CPPMOD VOID                                    \
    s(                                             \
        HANDLE                 hCurrentProcess,    \
        HANDLE                 hCurrentThread,     \
        ULONG                  dwCurrentPc,        \
        ULONG                  dwProcessor,        \
        PCSTR                  args                \
     )
*/

DECLARE_API(__declspec(dllexport) mozmutex)
{
}

