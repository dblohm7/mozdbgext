#include "mozdbgext.h"

static char sNames[] = "\0";

static bool
GetNames(PSTR aBuffer, PULONG aBufferSize)
{
#if defined(DEBUG)
  dprintf("GetNames\n");
#endif
  if (*aBufferSize < sizeof(sNames)) {
    return false;
  }
  memcpy(aBuffer, sNames, sizeof(sNames));
  *aBufferSize = sizeof(sNames);
  return true;
}

static bool
SuppressTypeName(PSTR aStructName)
{
#if defined(DEBUG)
  dprintf("Suppress type name \"%s\"\n", aStructName);
#endif
  return false;
}

static HRESULT
GetRepresentation(ULONG64 aAddress, PSTR aStructName, PSTR aBuffer,
                  PULONG aBufferSize)
{
#if defined(DEBUG)
  dprintf("Get representation \"%s\"\n", aStructName);
#endif
  return E_NOTIMPL;
}

HRESULT CALLBACK
KnownStructOutput(ULONG aFlag, ULONG64 aAddress, PSTR aStructName, PSTR aBuffer,
                  PULONG aBufferSize)
{
#if defined(DEBUG)
  dprintf("KnownStructOutput\n");
#endif
  switch (aFlag) {
    case DEBUG_KNOWN_STRUCT_GET_NAMES:
      if (!aBuffer || !aBufferSize) {
        return E_INVALIDARG;
      }
      return GetNames(aBuffer, aBufferSize) ? S_OK : S_FALSE;
    case DEBUG_KNOWN_STRUCT_SUPPRESS_TYPE_NAME:
      if (!aStructName) {
        return E_INVALIDARG;
      }
      return SuppressTypeName(aStructName) ? S_OK : S_FALSE;
    case DEBUG_KNOWN_STRUCT_GET_SINGLE_LINE_OUTPUT:
      return GetRepresentation(aAddress, aStructName, aBuffer, aBufferSize);
    default:
      return E_NOTIMPL;
  }
}

