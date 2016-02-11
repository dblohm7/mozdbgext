#include "mozdbgext.h"

#include <memory>
#include <sstream>
#include <winnt.h>

#if 0
#if defined(_M_X64)

typedef struct DECLSPEC_ALIGN(16) _CONTEXTX64 {

    //
    // Register parameter home addresses.
    //
    // N.B. These fields are for convience - they could be used to extend the
    //      context record in the future.
    //

    DWORD64 P1Home;
    DWORD64 P2Home;
    DWORD64 P3Home;
    DWORD64 P4Home;
    DWORD64 P5Home;
    DWORD64 P6Home;

    //
    // Control flags.
    //

    DWORD ContextFlags;
    DWORD MxCsr;

    //
    // Segment Registers and processor flags.
    //

    WORD   SegCs;
    WORD   SegDs;
    WORD   SegEs;
    WORD   SegFs;
    WORD   SegGs;
    WORD   SegSs;
    DWORD EFlags;

    //
    // Debug registers
    //

    DWORD64 Dr0;
    DWORD64 Dr1;
    DWORD64 Dr2;
    DWORD64 Dr3;
    DWORD64 Dr6;
    DWORD64 Dr7;

    //
    // Integer registers.
    //

    DWORD64 Rax;
    DWORD64 Rcx;
    DWORD64 Rdx;
    DWORD64 Rbx;
    DWORD64 Rsp;
    DWORD64 Rbp;
    DWORD64 Rsi;
    DWORD64 Rdi;
    DWORD64 R8;
    DWORD64 R9;
    DWORD64 R10;
    DWORD64 R11;
    DWORD64 R12;
    DWORD64 R13;
    DWORD64 R14;
    DWORD64 R15;

    //
    // Program counter.
    //

    DWORD64 Rip;

    //
    // Floating point state.
    //

    union {
        XMM_SAVE_AREA32 FltSave;
        struct {
            M128A Header[2];
            M128A Legacy[8];
            M128A Xmm0;
            M128A Xmm1;
            M128A Xmm2;
            M128A Xmm3;
            M128A Xmm4;
            M128A Xmm5;
            M128A Xmm6;
            M128A Xmm7;
            M128A Xmm8;
            M128A Xmm9;
            M128A Xmm10;
            M128A Xmm11;
            M128A Xmm12;
            M128A Xmm13;
            M128A Xmm14;
            M128A Xmm15;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;

    //
    // Vector registers.
    //

    M128A VectorRegister[26];
    DWORD64 VectorControl;

    //
    // Special debug control registers.
    //

    DWORD64 DebugControl;
    DWORD64 LastBranchToRip;
    DWORD64 LastBranchFromRip;
    DWORD64 LastExceptionToRip;
    DWORD64 LastExceptionFromRip;
} CONTEXTX64, *PCONTEXTX64;

#endif // defined(_M_X64)

#include "pshpack4.h"

typedef struct _CONTEXTX86 {

    //
    // The flags values within this flag control the contents of
    // a CONTEXT record.
    //
    // If the context record is used as an input parameter, then
    // for each portion of the context record controlled by a flag
    // whose value is set, it is assumed that that portion of the
    // context record contains valid context. If the context record
    // is being used to modify a threads context, then only that
    // portion of the threads context will be modified.
    //
    // If the context record is used as an IN OUT parameter to capture
    // the context of a thread, then only those portions of the thread's
    // context corresponding to set flags will be returned.
    //
    // The context record is never used as an OUT only parameter.
    //

    DWORD ContextFlags;

    //
    // This section is specified/returned if CONTEXT_DEBUG_REGISTERS is
    // set in ContextFlags.  Note that CONTEXT_DEBUG_REGISTERS is NOT
    // included in CONTEXT_FULL.
    //

    DWORD   Dr0;
    DWORD   Dr1;
    DWORD   Dr2;
    DWORD   Dr3;
    DWORD   Dr6;
    DWORD   Dr7;

    //
    // This section is specified/returned if the
    // ContextFlags word contians the flag CONTEXT_FLOATING_POINT.
    //

    FLOATING_SAVE_AREA FloatSave;

    //
    // This section is specified/returned if the
    // ContextFlags word contians the flag CONTEXT_SEGMENTS.
    //

    DWORD   SegGs;
    DWORD   SegFs;
    DWORD   SegEs;
    DWORD   SegDs;

    //
    // This section is specified/returned if the
    // ContextFlags word contians the flag CONTEXT_INTEGER.
    //

    DWORD   Edi;
    DWORD   Esi;
    DWORD   Ebx;
    DWORD   Edx;
    DWORD   Ecx;
    DWORD   Eax;

    //
    // This section is specified/returned if the
    // ContextFlags word contians the flag CONTEXT_CONTROL.
    //

    DWORD   Ebp;
    DWORD   Eip;
    DWORD   SegCs;              // MUST BE SANITIZED
    DWORD   EFlags;             // MUST BE SANITIZED
    DWORD   Esp;
    DWORD   SegSs;

    //
    // This section is specified/returned if the ContextFlags word
    // contains the flag CONTEXT_EXTENDED_REGISTERS.
    // The format and contexts are processor specific
    //

    BYTE    ExtendedRegisters[MAXIMUM_SUPPORTED_EXTENSION];

} CONTEXTX86, *PCONTEXTX86;

#include "poppack.h"
#endif

// need to know:
// are we 32 or 64 bit?
// if we're 32 bit, is fpo turned on?
// need current context

static HRESULT
GetParams32(const unsigned int aNumParams)
{
  ULONG64 frame;
  HRESULT hr = gDebugRegisters->GetFrameOffset2(DEBUG_REGSRC_FRAME, &frame);
  if (FAILED(hr)) {
    return hr;
  }
  // frame += 2 * sizeof(DWORD); // account for prev ebp + return address

  auto data = std::make_unique<DWORD[]>(aNumParams);
  ULONG bytesRead = 0;
  hr = gDebugDataSpaces->ReadVirtual(frame, data.get(),
                                     aNumParams * sizeof(DWORD), &bytesRead);
  if (FAILED(hr)) {
    return hr;
  }
  for (unsigned int i = aNumParams; i > 0; --i) {
    dprintf("0x%08X\n", data[i - 1]);
  }
  return S_OK;
}

static HRESULT
GetParams64(const unsigned int aNumParams)
{
  return E_NOTIMPL;
}

HRESULT CALLBACK
params(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  std::istringstream iss(aArgs);
  unsigned int numParams = 0;
  iss >> numParams;
  if (!iss || numParams < 1) {
    return E_FAIL;
  }
  /*
  CONTEXTX64 context64;
  CONTEXTX86 context32;
  PVOID pContext = gPointerWidth == 8 ? &context64 : &context32;
  ULONG contextSize = gPointerWidth == 8 ? sizeof(context64) : sizeof(context32);
  ULONG64 instructionOffset;
  DEBUG_STACK_FRAME scopeFrame;
  HRESULT hr = gDebugSymbols->GetScope(&instructionOffset, &scopeFrame,
                                       pContext, contextSize);
  if (FAILED(hr)) {
    return E_FAIL;
  }
  */
  if (gPointerWidth == 4) {
    return GetParams32(numParams);
  } else if (gPointerWidth == 8) {
    return GetParams64(numParams);
  }
  return E_FAIL;
}

