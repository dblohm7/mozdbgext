#!/bin/bash
DBGINC='C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v8.0\\Debuggers\\inc'
DBGLIB='C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v8.0\\Debuggers\\lib\\x86'
DDKINC='C:\\WinDDK\\7600.16385.1\\inc'
INCLUDE="${DBGINC};${DDKINC};${INCLUDE}"
LIB="${DBGLIB};${LIB}"
# cl -Zi -EHsc -MD -DUNICODE -D_UNICODE -I\"${DBGINC}\" -I\"${DDKINC}\" mozdbgext.cpp mozmutex.cpp dbgeng.lib -link '-DLL' '-out:mozdbgext.dll' -libpath:\"${DBGLIB}\"
cl -Zi -EHsc -MD -DUNICODE -D_UNICODE mozdbgext.cpp mozmutex.cpp dbgeng.lib -link '-DLL' '-out:mozdbgext.dll'
