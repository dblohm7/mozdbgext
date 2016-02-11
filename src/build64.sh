#!/bin/bash
DBGINC='C:\Program Files (x86)\Windows Kits\10\Debuggers\inc'
DBGLIB='C:\Program Files (x86)\Windows Kits\10\Debuggers\lib\x64'
SDKINC='C:\Program Files (x86)\Windows Kits\10\Include\10.0.10586.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.10586.0\shared'
DDKINC='C:\\WinDDK\\7600.16385.1\\inc'
INCLUDE="${DBGINC};${DDKINC};${SDKINC};${INCLUDE}"
LIB="${DBGLIB};${LIB}"
# RELEASE_OPTS="-O2 -Oy-"
cl -Zi -EHsc -LD -MT -DUNICODE -D_UNICODE ${RELEASE_OPTS} mozdbgext.cpp mozdbgextcb.cpp mozmutex.cpp knownstruct.cpp uiext.cpp bpsyms.cpp params.cpp iat.cpp pe.cpp dbgeng.lib pathcch.lib user32.lib -link -def:mozdbgext.def
