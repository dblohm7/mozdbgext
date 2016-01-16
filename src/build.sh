#!/bin/bash
DBGINC='C:\Program Files (x86)\Windows Kits\10\Debuggers\inc'
DBGLIB='C:\Program Files (x86)\Windows Kits\10\Debuggers\lib\x86'
SDKINC='C:\Program Files (x86)\Windows Kits\10\Include\10.0.10586.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.10586.0\shared'
DDKINC='C:\\WinDDK\\7600.16385.1\\inc'
INCLUDE="${DBGINC};${DDKINC};${SDKINC};${INCLUDE}"
LIB="${DBGLIB};${LIB}"
cl -Zi -EHsc -LD -MD -DUNICODE -D_UNICODE mozdbgext.cpp mozdbgextcb.cpp mozmutex.cpp knownstruct.cpp bpsyms.cpp dbgeng.lib pathcch.lib -link -def:mozdbgext.def
