#!/bin/bash
DBGINC='C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v8.1\\Debuggers\\inc'
DBGLIB='C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v8.1\\Debuggers\\lib\\x86'
DDKINC='C:\\WinDDK\\7600.16385.1\\inc'
INCLUDE="${DBGINC};${DDKINC};${INCLUDE}"
LIB="${DBGLIB};${LIB}"
# cl -Zi -EHsc -MD -DUNICODE -D_UNICODE -I\"${DBGINC}\" -I\"${DDKINC}\" mozdbgext.cpp mozmutex.cpp dbgeng.lib -link '-DLL' '-out:mozdbgext.dll' -libpath:\"${DBGLIB}\"
cl -Zi -EHsc -LD -MD -DUNICODE -D_UNICODE mozdbgext.cpp mozmutex.cpp knownstruct.cpp dbgeng.lib -link -def:mozdbgext.def
