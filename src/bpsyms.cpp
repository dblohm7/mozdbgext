#include "mozdbgext.h"
#include "mozdbgextcb.h"

#include <pathcch.h>
#include <winnt.h>

#include <fstream>
#include <iomanip>
#include <ios>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

struct IMAGE_DEBUG_INFO_CODEVIEW
{
  DWORD mSignature;
  GUID  mGuid;
  DWORD mAge;
  char  mPath[1];
};

static std::wstring
GenerateDebugInfoUniqueId(REFGUID aGuid, DWORD aAge)
{
  std::wostringstream oss;
  // guid, all caps, no dashes, then concatenate age (hex)
  oss << std::hex << std::uppercase << std::setfill(L'0') << std::setw(8)
      << aGuid.Data1;
  oss << std::hex << std::uppercase << std::setfill(L'0') << std::setw(4)
      << aGuid.Data2;
  oss << std::hex << std::uppercase << std::setfill(L'0') << std::setw(4)
      << aGuid.Data3;
  for (unsigned int i = 0; i < sizeof(aGuid.Data4); ++i) {
    oss << std::hex << std::uppercase << std::setfill(L'0') << std::setw(2)
        << aGuid.Data4[i];
  }
  oss << std::hex << std::uppercase << aAge;
  return oss.str();
}

static bool
GetDebugInfoUniqueId(ULONG64 aBase, std::wstring& aId, std::wstring& aPdbName)
{
  IMAGE_DOS_HEADER header;
  HRESULT hr = gDebugDataSpaces->ReadVirtual(aBase, &header,
                                             sizeof(IMAGE_DOS_HEADER), nullptr);
  if (FAILED(hr)) {
    dprintf("Failed to read IMAGE_DOS_HEADER\n");
    return false;
  }
  ULONG64 dbgBase, dbgSize;
  ULONG64 ntBase = aBase + header.e_lfanew;
  if (gPointerWidth == 4) {
    IMAGE_NT_HEADERS32 ntHeader;
    hr = gDebugDataSpaces->ReadVirtual(ntBase, &ntHeader, sizeof(ntHeader),
                                       nullptr);
    if (FAILED(hr)) {
      dprintf("Failed to read IMAGE_NT_HEADERS\n");
      return false;
    }
    IMAGE_DATA_DIRECTORY& dbgDataDir =
      ntHeader.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    dbgBase = aBase + dbgDataDir.VirtualAddress;
    dbgSize = dbgDataDir.Size;
  } else {
    IMAGE_NT_HEADERS64 ntHeader;
    hr = gDebugDataSpaces->ReadVirtual(ntBase, &ntHeader, sizeof(ntHeader),
                                       nullptr);
    if (FAILED(hr)) {
      dprintf("Failed to read IMAGE_NT_HEADERS\n");
      return false;
    }
    IMAGE_DATA_DIRECTORY& dbgDataDir =
      ntHeader.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    dbgBase = aBase + dbgDataDir.VirtualAddress;
    dbgSize = dbgDataDir.Size;
  }
  ULONG numDataDirEntries = dbgSize / sizeof(IMAGE_DEBUG_DIRECTORY);
  auto dbgDir = std::make_unique<IMAGE_DEBUG_DIRECTORY[]>(numDataDirEntries);
  hr = gDebugDataSpaces->ReadVirtual(dbgBase, dbgDir.get(), dbgSize, nullptr);
  if (FAILED(hr)) {
    dprintf("Failed to read IMAGE_DEBUG_DIRECTORY\n");
    return false;
  }
  for (ULONG i = 0; i < numDataDirEntries; ++i) {
    if (dbgDir[i].Type == IMAGE_DEBUG_TYPE_CODEVIEW) {
      auto cvData = std::make_unique<unsigned char[]>(dbgDir[i].SizeOfData);
      hr = gDebugDataSpaces->ReadVirtual(aBase + dbgDir[i].AddressOfRawData,
                                         cvData.get(), dbgDir[i].SizeOfData,
                                         nullptr);
      if (FAILED(hr)) {
        dprintf("Failed to read CodeView info\n");
        return false;
      }
      auto cvInfo = reinterpret_cast<IMAGE_DEBUG_INFO_CODEVIEW*>(cvData.get());
      aId = GenerateDebugInfoUniqueId(cvInfo->mGuid, cvInfo->mAge);
      int pathLen = MultiByteToWideChar(CP_UTF8, 0, cvInfo->mPath, -1, nullptr,
                                        0);
      if (!pathLen) {
        dprintf("MultiByteToWideChar on pdb path failed\n");
        return false;
      }
      auto widePath = std::make_unique<wchar_t[]>(pathLen);
      pathLen = MultiByteToWideChar(CP_UTF8, 0, cvInfo->mPath, -1,
                                    widePath.get(), pathLen);
      if (!pathLen) {
        dprintf("MultiByteToWideChar on pdb path failed\n");
        return false;
      }
      wchar_t name[_MAX_FNAME] = {0};
      if (_wsplitpath_s(widePath.get(), nullptr, 0, nullptr, 0, name,
                        _MAX_FNAME, nullptr, 0)) {
        dprintf("_wsplitpath_s failed on pdb path\n");
        return false;
      }
      aPdbName = name;
      return true;
    }
  }
  return false;
}

template <typename CharType>
std::vector<std::basic_string<CharType>>
split(const std::basic_string<CharType>& aBuf, const CharType aDelim,
      size_t aMaxTokens)
{
  std::vector<std::basic_string<CharType>> result;

  std::basic_string<CharType>::size_type i = 0, j = 0;
  while (result.size() < aMaxTokens - 1 &&
         (j = aBuf.find(aDelim, i)) != std::basic_string<CharType>::npos) {
    result.push_back(aBuf.substr(i, j - i));
    i = j + 1;
  }
  result.push_back(aBuf.substr(i, aBuf.size()));

  return result;
}

static size_t
trim(char* aBuf, size_t aBufLen)
{
  if (!aBufLen) {
    return 0;
  }
  size_t i = aBufLen - 1;
  while (i >= 0 && std::isspace(aBuf[i], std::locale::classic())) {
    aBuf[i] = 0;
    --i;
  }
  return i + 1;
}

template <size_t N>
static bool
startswith(const char* aBuf, const char (&aLiteral)[N])
{
  // N - 1 to exclude terminating nul
  return !strncmp(aBuf, aLiteral, N - 1);
}

static const size_t kSymBufLen = 0x1000000; // 16MB

static void
LoadBpSymbolFile(const ULONG64 aBase, const wchar_t* aSymPath)
{
  auto buffer = std::make_unique<char[]>(kSymBufLen);
  std::ifstream i(aSymPath);
  if (!i) {
    DWORD fileAttrs = GetFileAttributesW(aSymPath);
    if (fileAttrs == INVALID_FILE_ATTRIBUTES &&
        GetLastError() == ERROR_PATH_NOT_FOUND) {
      // We don't have breakpad symbols for every module out there.
      // Fail silently in this case.
      return;
    }
    dprintf("Failed to open \"%S\"\n", aSymPath);
    return;
  }
  i.rdbuf()->pubsetbuf(buffer.get(), kSymBufLen);
#if defined(INCLUDE_LINE_SYMBOLS)
  std::unordered_map<ULONG64,std::string> fileMap;
#endif // defined(INCLUDE_LINE_SYMBOLS)
  char buf[1024] = {0};
  HRESULT hr;
  while (true) {
    i.getline(buf, sizeof(buf) - 1);
    if (!i) {
      break;
    }
    size_t lineLen = strlen(buf);
    lineLen = trim(buf, lineLen);
    if (!lineLen) {
      continue;
    }
    if (startswith(buf, "MODULE ")) {
      auto tokens = split(std::string(buf, lineLen), ' ', 5);
    } else if (startswith(buf, "FUNC ")) {
      auto tokens = split(std::string(buf, lineLen), ' ', 5);
      std::istringstream issAddress(tokens[1]);
      std::istringstream issSize(tokens[2]);
      ULONG64 address, size;
      issAddress >> std::hex >> address;
      issSize >> std::hex >> size;
      hr = gDebugSymbols->AddSyntheticSymbol(aBase + address, size,
                                             tokens[4].c_str(),
                                             DEBUG_ADDSYNTHSYM_DEFAULT, nullptr);
      if (FAILED(hr)) {
        dprintf("Failed to add synthetic symbol for \"%s\", hr 0x%08X\n",
                tokens[4].c_str(), hr);
      }
    } else if(startswith(buf, "PUBLIC ")) {
      auto tokens = split(std::string(buf, lineLen), ' ', 4);
      std::istringstream issAddress(tokens[1]);
      ULONG64 address;
      issAddress >> std::hex >> address;
      hr = gDebugSymbols->AddSyntheticSymbol(aBase + address, 0,
                                             tokens[3].c_str(),
                                             DEBUG_ADDSYNTHSYM_DEFAULT, nullptr);
      if (FAILED(hr)) {
        dprintf("Failed to add synthetic symbol for \"%s\", hr 0x%08X\n",
                tokens[3].c_str(), hr);
      }
#if defined(INCLUDE_LINE_SYMBOLS)
    } else if (startswith(buf, "FILE ")) {
      auto tokens = split(std::string(buf, lineLen), ' ', 3);
      std::istringstream issFileId(tokens[1]);
      ULONG64 fileId;
      issFileId >> std::dec >> fileId;
      fileMap[fileId] = tokens[2];
    } else if (std::isxdigit(buf[0])) {
      // line record
      auto tokens = split(std::string(buf, lineLen), ' ', 4);
      std::istringstream issAddress(tokens[0]);
      std::istringstream issSize(tokens[1]);
      std::istringstream issLine(tokens[2]);
      std::istringstream issFileId(tokens[3]);
      ULONG64 address, size, lineNo, fileId;
      issAddress >> std::hex >> address;
      issSize >> std::hex >> size;
      issLine >> std::dec >> lineNo;
      issFileId >> std::dec >> fileId;
      std::ostringstream ossSymName;
      ossSymName << fileMap[fileId] << ":" << lineNo;
      std::string symName(ossSymName.str());
      hr = gDebugSymbols->AddSyntheticSymbol(aBase + address, size,
                                             symName.c_str(),
                                             DEBUG_ADDSYNTHSYM_DEFAULT, nullptr);
      if (FAILED(hr)) {
        dprintf("Failed to add synthetic symbol for \"%s\", hr 0x%08X\n",
                symName.c_str(), hr);
      }
#endif // defined(INCLUDE_LINE_SYMBOLS)
    }
  }
}

static bool
LoadBpSymbols(const std::wstring& aBasePdbPath, const ULONG64 aBase)
{
  // Extract the unique ids for the pdb file from the module headers
  std::wstring uid, pdbFile;
  if (!GetDebugInfoUniqueId(aBase, uid, pdbFile)) {
    dprintf("GetDebugInfoUniqueId failed\n");
    return false;
  }
  // Construct the breakpad symbol path
  wchar_t pdbPath[MAX_PATH + 1] = {0};
  wcsncpy(pdbPath, aBasePdbPath.c_str(), MAX_PATH);
  HRESULT hr;
  hr = PathCchAppend(pdbPath, MAX_PATH, (pdbFile + L".pdb").c_str());
  if (FAILED(hr)) {
    dprintf("Path append failed\n");
    return false;
  }
  hr = PathCchAppend(pdbPath, MAX_PATH, uid.c_str());
  if (FAILED(hr)) {
    dprintf("Path append failed\n");
    return false;
  }
  hr = PathCchAppend(pdbPath, MAX_PATH, (pdbFile + L".sym").c_str());
  if (FAILED(hr)) {
    dprintf("Path append failed\n");
    return false;
  }
  // Load the actual breakpad symbols
  LoadBpSymbolFile(aBase, pdbPath);
  return true;
}

static std::wstring gBasePdbPath;

namespace {

/**
 * Perf improvement: don't use stdio on the same files as iostreams while an
 * instance of this class is on the stack!
 */
class DisableStdioSync
{
public:
  DisableStdioSync()
    : mPrevSetting(std::ios_base::sync_with_stdio(false))
  {
  }

  ~DisableStdioSync()
  {
    std::ios_base::sync_with_stdio(mPrevSetting);
  }

private:
  DisableStdioSync(const DisableStdioSync&) = delete;
  DisableStdioSync(const DisableStdioSync&&) = delete;
  DisableStdioSync& operator=(const DisableStdioSync&) = delete;

  bool  mPrevSetting;
};

} // anonymous namespace

DECLARE_API(__declspec(dllexport) loadbpsyms)
{
  DisableStdioSync stdioSyncDisabled;

  // For now we only support loading from a single bp symbol path
  if (!gBasePdbPath.empty()) {
    dprintf("Breakpad symbols are already loaded\n");
    return;
  }

  // args should contain the base path to the bp symbols
  DWORD attrs = GetFileAttributesA(args);
  if (attrs == INVALID_FILE_ATTRIBUTES ||
      (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    dprintf("Error: \"%s\" is not a valid directory\n", args);
    return;
  }

  // Get the module list;
  ULONG numLoaded, numUnloaded;
  HRESULT hr = gDebugSymbols->GetNumberModules(&numLoaded, &numUnloaded);
  if (FAILED(hr)) {
    dprintf("GetNumberModules failed\n");
    return;
  }

  wchar_t basePdbPath[MAX_PATH + 1] = {0};
  int convResult = MultiByteToWideChar(CP_ACP, 0, args, -1, basePdbPath,
                                       MAX_PATH);
  if (!convResult) {
    dprintf("Error converting \"%s\" to UTF16\n", args);
    return;
  }
  gBasePdbPath = basePdbPath;

  // For each module, load its symbol file
  for (ULONG i = 0; i < numLoaded; ++i) {
    ULONG64 base;
    hr = gDebugSymbols->GetModuleByIndex(i, &base);
    if (FAILED(hr)) {
      dprintf("GetModuleByIndex(%u) failed\n", i);
      return;
    }
    LoadBpSymbols(basePdbPath, base);
  }

  mozilla::DbgExtCallbacks::RegisterModuleEventListener(
    [](PCWSTR aModName, ULONG64 aBaseAddress) {
      LoadBpSymbols(gBasePdbPath, aBaseAddress);
    }
  );
}

