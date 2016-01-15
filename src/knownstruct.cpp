#include "mozdbgext.h"

static char sNames[] = "nsString\0nsCString\0nsAutoString\0nsAutoCString\0nsAString\0nsACString\0";

static bool
GetNames(PSTR aBuffer, PULONG aBufferSize)
{
  dprintf("GetNames\n");
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
  dprintf("Suppress type name \"%s\"\n", aStructName);
  return false;
}

static HRESULT
GetRepresentation(ULONG64 aAddress, PSTR aStructName, PSTR aBuffer,
                  PULONG aBufferSize)
{
  dprintf("Get representation \"%s\"\n", aStructName);
  return E_FAIL;
}

extern "C" HRESULT CALLBACK
KnownStructOutput(ULONG aFlag, ULONG64 aAddress, PSTR aStructName, PSTR aBuffer,
                  PULONG aBufferSize)
{
  dprintf("KnownStructOutput\n");
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

