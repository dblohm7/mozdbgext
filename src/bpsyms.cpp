#include "mozdbgext.h"
#include "mozdbgextcb.h"

#include <pathcch.h>
#include <winnt.h>

#include <algorithm>
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

enum SymbolType
{
  eFunctionSymbol,
  ePublicSymbol,
  eLineSymbol
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
  wchar_t modNameBuf[64];
  hr = gDebugSymbols->GetModuleNameStringWide(DEBUG_MODNAME_MODULE,
                                              DEBUG_ANY_ID, aBase,
                                              modNameBuf, 64, nullptr);
  if (SUCCEEDED(hr)) {
    dprintf("Warning: No PDB reference found inside module \"%S\"\n",
            modNameBuf);
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

template <typename CallbackT>
static void
LoadBpSymbolFile(const ULONG64 aBase, const wchar_t* aSymPath, CallbackT&& aCb)
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
  std::unordered_map<ULONG64,std::string> fileMap;
  std::string moduleName;
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
      moduleName = tokens[4];
      std::string::size_type pos = moduleName.find_last_of('.');
      // chop off any extension
      if (pos != std::string::npos) {
        moduleName.erase(moduleName.begin() + pos, moduleName.end());
      }
    } else if (startswith(buf, "FUNC ")) {
      auto tokens = split(std::string(buf, lineLen), ' ', 5);
      std::istringstream issAddress(tokens[1]);
      std::istringstream issSize(tokens[2]);
      ULONG64 address, size;
      issAddress >> std::hex >> address;
      issSize >> std::hex >> size;
      aCb(eFunctionSymbol, aBase + address, size, moduleName, tokens[4]);
    } else if(startswith(buf, "PUBLIC ")) {
      auto tokens = split(std::string(buf, lineLen), ' ', 4);
      std::istringstream issAddress(tokens[1]);
      ULONG64 address;
      issAddress >> std::hex >> address;
      aCb(ePublicSymbol, aBase + address, 0, moduleName, tokens[3]);
    } else if (startswith(buf, "FILE ")) {
      auto tokens = split(std::string(buf, lineLen), ' ', 3);
      std::istringstream issFileId(tokens[1]);
      ULONG64 fileId;
      issFileId >> std::dec >> fileId;
      fileMap[fileId] = tokens[2];
    } else if (std::isxdigit(buf[0], std::locale::classic())) {
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
      ossSymName << fileMap[fileId] << " @ " << lineNo;
      std::string symName(ossSymName.str());
      aCb(eLineSymbol, aBase + address, size, moduleName, symName);
    }
  }
}

template <typename CallbackT>
static bool
LoadBpSymbols(const std::wstring& aBasePdbPath, const ULONG64 aBase,
              CallbackT&& aCb)
{
  // Extract the unique ids for the pdb file from the module headers
  std::wstring uid, pdbFile;
  if (!GetDebugInfoUniqueId(aBase, uid, pdbFile)) {
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
  LoadBpSymbolFile(aBase, pdbPath, aCb);
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

template <typename CallbackT>
static void
LoadBpSymbolsForModules(const char* aPath, CallbackT&& aCb)
{
  DisableStdioSync stdioSyncDisabled;

  // For now we only support loading from a single bp symbol path
  if (!gBasePdbPath.empty()) {
    dprintf("Breakpad symbols are already loaded\n");
    return;
  }

  // aPath should contain the base path to the bp symbols
  DWORD attrs = GetFileAttributesA(aPath);
  if (attrs == INVALID_FILE_ATTRIBUTES ||
      (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    dprintf("Error: \"%s\" is not a valid directory\n", aPath);
    return;
  }

  // Get the module list;
  ULONG numLoaded, numUnloaded;
  HRESULT hr = gDebugSymbols->GetNumberModules(&numLoaded, &numUnloaded);
  if (FAILED(hr)) {
    dprintf("GetNumberModules failed\n");
    return;
  }
#if defined(DEBUG)
  dprintf("Module count: %u loaded, %u unloaded\n", numLoaded, numUnloaded);
#endif

  wchar_t basePdbPath[MAX_PATH + 1] = {0};
  int convResult = MultiByteToWideChar(CP_ACP, 0, aPath, -1, basePdbPath,
                                       MAX_PATH);
  if (!convResult) {
    dprintf("Error converting \"%s\" to UTF16\n", aPath);
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
    LoadBpSymbols(basePdbPath, base, aCb);
  }

  mozilla::DbgExtCallbacks::RegisterModuleEventListener(
    [=](PCWSTR aModName, ULONG64 aBaseAddress) {
      LoadBpSymbols(gBasePdbPath, aBaseAddress, aCb);
    }
  );
}

DECLARE_API(__declspec(dllexport) bpsynthsyms)
{
  LoadBpSymbolsForModules(args, [](SymbolType aType, ULONG64 aOffset,
                                   ULONG aSize, std::string& aModuleName,
                                   std::string& aName) -> void {
      if (aType == eLineSymbol) {
        return;
      }
      HRESULT hr = gDebugSymbols->AddSyntheticSymbol(aOffset, aSize,
                                                     aName.c_str(),
                                                     DEBUG_ADDSYNTHSYM_DEFAULT,
                                                     nullptr);
      if (FAILED(hr)) {
        dprintf("Failed to add synthetic symbol for \"%s\", hr 0x%08X\n",
                aName.c_str(), hr);
      }
    });
}

// 1) sort by offset to do symbol lookup;
// 2) sort by name to do name lookup
struct BpSymbolInfo
{
  BpSymbolInfo(ULONG64 aOffset, ULONG aSize, std::string& aModuleName,
               std::string& aName)
    : mOffset(aOffset)
    , mSize(aSize)
    , mModuleName(aModuleName)
    , mName(aName)
  {
  }
  explicit BpSymbolInfo(ULONG64 aOffset)
    : mOffset(aOffset)
    , mSize(0)
  {
  }
  ULONG64     mOffset;
  ULONG       mSize;
  std::string mModuleName;
  std::string mName;
};

std::vector<BpSymbolInfo> gSourceLineSyms;
std::vector<BpSymbolInfo> gSymsByOffset;
std::vector<BpSymbolInfo> gSymsByName;
auto gOffsetPredicate = [](const BpSymbolInfo& a, const BpSymbolInfo& b) -> bool {
        return a.mOffset < b.mOffset;
      };
auto gNamePredicate = [](const BpSymbolInfo& a, const BpSymbolInfo& b) -> bool {
        return a.mName < b.mName;
      };

DECLARE_API(__declspec(dllexport) bploadsyms)
{
  LoadBpSymbolsForModules(args, [](SymbolType aType, ULONG64 aOffset,
                                   ULONG aSize, std::string& aModuleName,
                                   std::string& aName) -> void {
      if (aType == eLineSymbol) {
        gSourceLineSyms.emplace_back(aOffset, aSize, aModuleName, aName);
        return;
      }
      gSymsByOffset.emplace_back(aOffset, aSize, aModuleName, aName);
    });
  std::sort(gSourceLineSyms.begin(), gSourceLineSyms.end(), gOffsetPredicate);
  gSymsByName = gSymsByOffset;
  std::sort(gSymsByOffset.begin(), gSymsByOffset.end(), gOffsetPredicate);
  std::sort(gSymsByName.begin(), gSymsByName.end(), gNamePredicate);
}

DECLARE_API(__declspec(dllexport) bpsyminfo)
{
  dprintf("%u breakpad symbols loaded\n%u source line symbols loaded",
          gSymsByOffset.size(), gSourceLineSyms.size());
}

static const size_t kSymbolBufSize = 0x1000000;

enum NearestSymbolFlags
{
  eIncludeLineNumbers
};

static bool
NearestSymbol(ULONG64 aOffset, std::string& aOutput, ULONG aFlags = 0)
{
  aOutput.clear();
  std::ostringstream oss;
  auto& symbol = std::upper_bound(gSymsByOffset.begin(), gSymsByOffset.end(),
                                  BpSymbolInfo(aOffset), gOffsetPredicate);
  if (symbol == gSymsByOffset.begin() || symbol == gSymsByOffset.end()) {
    // Try to fall back to the symbol engine
    ULONG64 displacement = 0;
    auto buf = std::make_unique<char[]>(kSymbolBufSize);
    ULONG bufSize = kSymbolBufSize;
    HRESULT hr = gDebugSymbols->GetNameByOffset(aOffset, buf.get(), bufSize,
                                                &bufSize, &displacement);
    if (FAILED(hr)) {
      return false;
    }
    // TODO: Get line numbers if eIncludeLineNumbers is set
    oss << buf.get() << "+0x" << std::hex << displacement;
    aOutput = oss.str();
    return true;
  }
  // This returns the first value >, but if it's > then we actually want the
  // one <= that address
  --symbol;
  ULONG64 offsetFromSym = aOffset - symbol->mOffset;
  oss << symbol->mModuleName << "!" << symbol->mName << "+0x"
      << std::hex << offsetFromSym;
  if (aFlags & eIncludeLineNumbers) {
    auto& lineSymbol = std::upper_bound(gSourceLineSyms.begin(),
                                        gSourceLineSyms.end(),
                                        BpSymbolInfo(aOffset),
                                        gOffsetPredicate);
    if (lineSymbol != gSourceLineSyms.begin() &&
        lineSymbol != gSourceLineSyms.end()) {
      --lineSymbol;
      oss << " [" << lineSymbol->mName << "]";
    }
  }
  aOutput = oss.str();
  return true;
}

DECLARE_API(__declspec(dllexport) bpk)
{
  const size_t kMaxFrames = 256;
  DEBUG_STACK_FRAME_EX frames[kMaxFrames];
  ULONG framesFilled = 0;
  HRESULT hr = gDebugControl->GetStackTraceEx(0, 0, 0, frames, kMaxFrames,
                                              &framesFilled);
  if (FAILED(hr)) {
    dprintf("Failed to obtain stack trace\n");
    return;
  }

#if defined(DEBUG)
  dprintf("Debug engine trace:\n");
  hr = gDebugControl->OutputStackTraceEx(DEBUG_OUTCTL_ALL_OTHER_CLIENTS,
                                         frames, framesFilled,
                                         DEBUG_STACK_FRAME_NUMBERS);
  if (FAILED(hr)) {
    dprintf("Failed to output stack trace\n");
    return;
  }
  dprintf("\nBreakpad trace:\n");
#endif

  for (ULONG i = 0; i < framesFilled; ++i) {
    std::string symName;
    if (!NearestSymbol(frames[i].InstructionOffset, symName)) {
      symName = "<No symbol found>";
    }
    dprintf("%02u %s\n", frames[i].FrameNumber, symName.c_str());
  }
}

