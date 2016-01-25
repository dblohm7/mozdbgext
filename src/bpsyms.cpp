#include "mozdbgext.h"
#include "mozdbgextcb.h"

#include <pathcch.h>
#include <winnt.h>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <ios>
#include <locale>
#include <map>
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

template <typename NTHeaderT>
static bool
GetDataDirectory(const ULONG64 aBase, const ULONG64 aNtHeaderBase,
                 ULONG64& aDbgBase, ULONG64& aDbgSize)
{
  NTHeaderT ntHeader;
  HRESULT hr = gDebugDataSpaces->ReadVirtual(aNtHeaderBase, &ntHeader,
                                             sizeof(ntHeader), nullptr);
  if (FAILED(hr)) {
    return false;
  }
  IMAGE_DATA_DIRECTORY& dbgDataDir =
    ntHeader.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
  aDbgBase = aBase + dbgDataDir.VirtualAddress;
  aDbgSize = dbgDataDir.Size;
  return true;
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
    if (!GetDataDirectory<IMAGE_NT_HEADERS32>(aBase, ntBase, dbgBase, dbgSize)) {
      dprintf("Failed to read IMAGE_NT_HEADERS\n");
      return false;
    }
  } else {
    if (!GetDataDirectory<IMAGE_NT_HEADERS64>(aBase, ntBase, dbgBase, dbgSize)) {
      dprintf("Failed to read IMAGE_NT_HEADERS\n");
      return false;
    }
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
    symprintf("Warning: No PDB reference found inside module \"%S\"\n",
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

static void
trim(std::string& aStr)
{
  if (aStr.empty()) {
    return;
  }
  auto i = aStr.rbegin();
  while (i != aStr.rend() && (std::isspace(*i, std::locale::classic()))) {
    i = std::string::reverse_iterator(aStr.erase(i.base() - 1));
  }
}

template <size_t N>
static bool
startswith(const std::string& aStr, const char (&aLiteral)[N])
{
  return aStr.find(aLiteral) == 0;
}

static void
SanitizeFilePath(std::string& aStr)
{
  if (startswith(aStr, "hg:")) {
    auto tokens = split(aStr, ':', 3);
    aStr = tokens[2].substr(0, tokens[2].find_last_of(':'));
  }
}

static const size_t kSymBufLen = 0x1000000; // 16MB

struct ModuleInfo
{
  ModuleInfo(ULONG64 aBase, ULONG aSize, const std::string& aName)
    : mBase(aBase)
    , mLimit(aBase + aSize)
    , mName(aName)
  {
  }
  explicit ModuleInfo(ULONG64 aBase)
    : mBase(aBase)
    , mLimit(aBase)
  {
  }
  bool operator<(const ModuleInfo& aOther) const
  {
    return mBase < aOther.mBase;
  }
  ULONG64     mBase;
  ULONG64     mLimit;
  std::string mName;
  typedef std::unordered_map<ULONG64,std::string> FileMapType;
  FileMapType mFileMap;
};

static std::map<ULONG64,ModuleInfo> gModuleInfo;

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
  auto& curModuleInfo = gModuleInfo.find(aBase);
  if (curModuleInfo == gModuleInfo.end()) {
    dprintf("Failed to find module info\n");
    return;
  }
  ModuleInfo::FileMapType& fileMap = curModuleInfo->second.mFileMap;
  ULONG64 low = 0xFFFFFFFFFFFFFFFF, high = 0;
  std::string moduleName;
  HRESULT hr;
  std::string line;
  while (true) {
    if (!std::getline(i, line)) {
      break;
    }
    trim(line);
    if (line.empty()) {
      continue;
    }
    if (startswith(line, "MODULE ")) {
      auto tokens = split(line, ' ', 5);
      moduleName = tokens[4];
      std::string::size_type pos = moduleName.find_last_of('.');
      // chop off any extension
      if (pos != std::string::npos) {
        moduleName.erase(moduleName.begin() + pos, moduleName.end());
      }
    } else if (startswith(line, "FUNC ")) {
      auto tokens = split(line, ' ', 5);
      std::istringstream issAddress(tokens[1]);
      std::istringstream issSize(tokens[2]);
      ULONG64 address, size;
      issAddress >> std::hex >> address;
      issSize >> std::hex >> size;
      low = std::min(low, aBase + address);
      high = std::max(high, aBase + address);
      auto paramPos = tokens[4].find('(');
      std::string symName(tokens[4].substr(0, paramPos));
      std::string params;
      if (paramPos != std::string::npos) {
        params = tokens[4].substr(paramPos);
      }
      aCb(eFunctionSymbol, aBase + address, size, aBase, symName, params,
          0, 0);
    } else if(startswith(line, "PUBLIC ")) {
      auto tokens = split(line, ' ', 4);
      std::istringstream issAddress(tokens[1]);
      ULONG64 address;
      issAddress >> std::hex >> address;
      low = std::min(low, aBase + address);
      high = std::max(high, aBase + address);
      aCb(ePublicSymbol, aBase + address, 0, aBase, tokens[3], std::string(),
          0, 0);
    } else if (startswith(line, "FILE ")) {
      auto tokens = split(line, ' ', 3);
      std::istringstream issFileId(tokens[1]);
      ULONG64 fileId;
      issFileId >> std::dec >> fileId;
      fileMap[fileId] = tokens[2];
      SanitizeFilePath(fileMap[fileId]);
    } else if (std::isxdigit(line[0], std::locale::classic())) {
      // line record
      auto tokens = split(line, ' ', 4);
      std::istringstream issAddress(tokens[0]);
      std::istringstream issSize(tokens[1]);
      std::istringstream issLine(tokens[2]);
      std::istringstream issFileId(tokens[3]);
      ULONG64 address, size, lineNo, fileId;
      issAddress >> std::hex >> address;
      issSize >> std::hex >> size;
      issLine >> std::dec >> lineNo;
      issFileId >> std::dec >> fileId;
      aCb(eLineSymbol, aBase + address, size, aBase, std::string(),
          std::string(), fileId, lineNo);
    }
  }
  symprintf("Module \"%s\": Base: 0x%p, Lowest symbol address: 0x%p, Highest "
            "symbol address: 0x%p\n", moduleName.c_str(), aBase, low, high);
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

  auto modules = std::make_unique<DEBUG_MODULE_PARAMETERS[]>(numLoaded);
  hr = gDebugSymbols->GetModuleParameters(numLoaded, nullptr, 0, modules.get());
  if (FAILED(hr)) {
    dprintf("GetModuleParameters failed\n");
    return;
  }

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
    std::string modName(modules[i].ModuleNameSize, 0);
    hr = gDebugSymbols->GetModuleNameString(DEBUG_MODNAME_MODULE, i, 0,
                                            &modName[0],
                                            modules[i].ModuleNameSize, nullptr);
    if (FAILED(hr)) {
      dprintf("GetModuleNameString(%u) failed\n", i);
      return;
    }
    modName.resize(modules[i].ModuleNameSize - 1);
    gModuleInfo.emplace(std::piecewise_construct,
                        std::forward_as_tuple(modules[i].Base),
                        std::forward_as_tuple(modules[i].Base, modules[i].Size,
                                              modName));
    LoadBpSymbols(basePdbPath, modules[i].Base, aCb);
  }

  mozilla::DbgExtCallbacks::RegisterModuleEventListener(
    [=](PCWSTR aModName, ULONG64 aBaseAddress) {
      DEBUG_MODULE_PARAMETERS modParams;
      HRESULT hr = gDebugSymbols->GetModuleParameters(1, &aBaseAddress, 0,
                                                      &modParams);
      if (SUCCEEDED(hr)) {
        std::string name(modParams.ModuleNameSize, 0);
        hr = gDebugSymbols->GetModuleNameString(DEBUG_MODNAME_MODULE,
                                                DEBUG_ANY_ID, aBaseAddress,
                                                &name[0],
                                                modParams.ModuleNameSize,
                                                nullptr);
        if (SUCCEEDED(hr)) {
          name.resize(modParams.ModuleNameSize - 1);
          gModuleInfo.emplace(std::piecewise_construct,
                              std::forward_as_tuple(aBaseAddress),
                              std::forward_as_tuple(aBaseAddress,
                                                    modParams.Size, name));
        }
      }
      LoadBpSymbols(gBasePdbPath, aBaseAddress, aCb);
    }
  );
}

HRESULT CALLBACK
bpsynthsyms(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  LoadBpSymbolsForModules(aArgs, [](SymbolType aType, ULONG64 aOffset,
                                    ULONG aSize, ULONG64 aModuleBae,
                                    std::string& aName, std::string& aParams,
                                    ULONG64 aFileId, ULONG64 aNameInt)
                                      -> void {
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
  return S_OK;
}

namespace {

// 1) sort by offset to do symbol lookup;
// 2) sort by name to do name lookup
struct BpSymbolInfo
{
  BpSymbolInfo(ULONG64 aOffset, ULONG aSize, ULONG64 aModuleBase,
               ULONG64 aFileId, ULONG64 aNameInt)
    : mOffset(aOffset)
    , mSize(aSize)
    , mModuleBase(aModuleBase)
    , mFileId(aFileId)
    , mNameInt(aNameInt)
  {
  }
  BpSymbolInfo(ULONG64 aOffset, ULONG aSize, ULONG64 aModuleBase,
               std::string& aName, std::string& aParams)
    : mOffset(aOffset)
    , mSize(aSize)
    , mModuleBase(aModuleBase)
    , mName(aName)
    , mParams(aParams)
    , mFileId(0)
    , mNameInt(0)
  {
  }
  explicit BpSymbolInfo(ULONG64 aOffset)
    : mOffset(aOffset)
    , mSize(0)
    , mModuleBase(0)
    , mFileId(0)
    , mNameInt(0)
  {
  }
  ULONG64     mOffset;
  ULONG       mSize;
  ULONG64     mModuleBase;
  std::string mName;
  std::string mParams;
  ULONG64     mFileId;
  ULONG64     mNameInt;
};

std::map<UINT64,BpSymbolInfo> gSourceLineSyms;
std::map<UINT64,BpSymbolInfo> gSymsByOffset;

} // anonymous namespace

HRESULT CALLBACK
bploadsyms(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  LoadBpSymbolsForModules(aArgs, [](SymbolType aType, ULONG64 aOffset,
                                    ULONG aSize, ULONG64 aModuleBase,
                                    std::string& aName, std::string& aParams,
                                    ULONG64 aFileId, ULONG64 aNameInt)
                                      -> void {
      if (aType == eLineSymbol) {
        gSourceLineSyms.emplace(std::piecewise_construct,
                                std::forward_as_tuple(aOffset),
                                std::forward_as_tuple(aOffset, aSize,
                                                      aModuleBase, aFileId,
                                                      aNameInt));
        return;
      }
      gSymsByOffset.emplace(std::piecewise_construct,
                            std::forward_as_tuple(aOffset),
                            std::forward_as_tuple(aOffset, aSize, aModuleBase,
                                                  aName, aParams));
    });
  return S_OK;
}

HRESULT CALLBACK
bpsyminfo(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  dprintf("%u breakpad symbols loaded\n%u source line symbols loaded\n",
          gSymsByOffset.size(), gSourceLineSyms.size());
  return S_OK;
}

static const size_t kSymbolBufSize = 0x1000000;

enum NearestSymbolFlags
{
  eDMLOutput = 1,
  eIncludeLineNumbers = 3 // Implies eDMLOutput
};

template <typename CharType>
void
FindAndReplace(std::basic_string<CharType>& aStr,
               const std::basic_string<CharType>& aReplace,
               const std::basic_string<CharType>& aWith)
{
  typedef std::basic_string<CharType>::size_type SizeT;
  SizeT offset = 0;
  SizeT replaceLength = aReplace.length();
  SizeT withLength = aWith.length();
  while ((offset = aStr.find(aReplace, offset)) !=
          std::basic_string<CharType>::npos) {
    aStr.replace(offset, replaceLength, aWith);
    offset += withLength;
  }
}

inline void
ConvertToDml(std::string& aStr)
{
  // This one needs to be first so we don't convert other entities
  FindAndReplace(aStr, std::string("&"), std::string("&amp;"));
  FindAndReplace(aStr, std::string("<"), std::string("&lt;"));
  FindAndReplace(aStr, std::string(">"), std::string("&gt;"));
  FindAndReplace(aStr, std::string("\""), std::string("&quot;"));
}

static bool
NearestSymbol(ULONG64 aOffset, std::string& aOutput, ULONG64& aOutSymOffset,
              ULONG aFlags = 0)
{
  aOutput.clear();
  std::ostringstream oss;
  auto& symbol = gSymsByOffset.upper_bound(aOffset);
  if (symbol == gSymsByOffset.begin() || symbol == gSymsByOffset.end()) {
    // Try to fall back to the symbol engine
    ULONG64 displacement = 0;
    auto buf = std::make_unique<char[]>(kSymbolBufSize);
    ULONG bufSize = kSymbolBufSize;
    HRESULT hr = gDebugSymbols->GetNameByOffset(aOffset, buf.get(), bufSize,
                                                &bufSize, &displacement);
    if (FAILED(hr)) {
      oss << "0x" << std::hex << aOffset;
      aOutput = oss.str();
      return true;
    }
    // TODO: Get line numbers if eIncludeLineNumbers is set
    oss << buf.get() << "+0x" << std::hex << displacement;
    gDebugSymbols->GetOffsetByName(buf.get(), &aOutSymOffset);
    aOutput = oss.str();
    return true;
  }
  // This returns the first value >, but if it's > then we actually want the
  // one <= that address
  --symbol;
  aOutSymOffset = symbol->second.mOffset;

  auto& module = gModuleInfo.upper_bound(aOffset);
  if (module == gModuleInfo.end()) {
    oss << "0x" << std::hex << aOffset;
    aOutput = oss.str();
    return true;
  }
  --module;
  if (aOffset > module->second.mLimit) {
    oss << "0x" << std::hex << aOffset;
    aOutput = oss.str();
    return true;
  }

  // We're going to output DML, so we need to escape any angle brackets in
  // the symbol name
  std::string symName(symbol->second.mName);
  if (aFlags & eDMLOutput) {
    ConvertToDml(symName);
  }

  auto& curModuleInfo = gModuleInfo.find(symbol->second.mModuleBase);
  if (curModuleInfo == gModuleInfo.end()) {
    dprintf("Error: ModuleInfo for base 0x%p not found\n",
            symbol->second.mModuleBase);
    return false;
  }
  std::string moduleName(curModuleInfo->second.mName);
  if (aFlags & eDMLOutput) {
    ConvertToDml(moduleName);
  }
  const auto& fileMap = curModuleInfo->second.mFileMap;

  ULONG64 offsetFromSym = aOffset - symbol->second.mOffset;
  oss << moduleName << "!" << symName << "+0x"
      << std::hex << offsetFromSym << std::flush;
  if ((aFlags & eIncludeLineNumbers) == eIncludeLineNumbers) {
    auto& lineSymbol = gSourceLineSyms.upper_bound(aOffset);
    if (lineSymbol != gSourceLineSyms.begin() &&
        lineSymbol != gSourceLineSyms.end()) {
      --lineSymbol;
      // Look up the file name
      auto& fileItr = fileMap.find(lineSymbol->second.mFileId);
      if (fileItr == fileMap.end()) {
        dprintf("Error: File id does not map to a valid file name\n");
        return false;
      }
      std::string file(fileItr->second);
      ConvertToDml(file);
      oss << " [<exec cmd=\".open " << file << "\">"
          << file
          << "</exec>"
          << " @ "
          << "<exec cmd=\"!gotoline " << std::dec << lineSymbol->second.mNameInt
                                      << " " << file << "\">"
          << std::dec << lineSymbol->second.mNameInt
          << "</exec>]"
          << std::flush;
    }
  }
  aOutput = oss.str();
  return true;
}

HRESULT CALLBACK
bpk(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  const size_t kMaxFrames = 256;
  DEBUG_STACK_FRAME_EX frames[kMaxFrames];
  ULONG framesFilled = 0;
  HRESULT hr = gDebugControl->GetStackTraceEx(0, 0, 0, frames, kMaxFrames,
                                              &framesFilled);
  if (FAILED(hr)) {
    dprintf("Failed to obtain stack trace\n");
    return E_FAIL;
  }

#if defined(DEBUG)
  dprintf("Debug engine trace:\n");
  hr = gDebugControl->OutputStackTraceEx(DEBUG_OUTCTL_ALL_OTHER_CLIENTS,
                                         frames, framesFilled,
                                         DEBUG_STACK_FRAME_NUMBERS);
  if (FAILED(hr)) {
    dprintf("Failed to output stack trace\n");
    return E_FAIL;
  }
  dprintf("\nBreakpad trace:\n");
#endif

  for (ULONG i = 0; i < framesFilled; ++i) {
    std::string symOutput;
    ULONG64 symOffset;
    if (!NearestSymbol(frames[i].InstructionOffset, symOutput, symOffset,
                       eDMLOutput)) {
      symOutput = "<No symbol found>";
      ConvertToDml(symOutput);
    }
#ifdef DEBUG_DML
    dprintf("symOutput.length() == %u\n", symOutput.length());
    dprintf("%02x %s\n", frames[i].FrameNumber, symOutput.c_str());
#else
    dmlprintf("%02x %s\n", frames[i].FrameNumber, symOutput.c_str());
#endif
  }
  return S_OK;
}

HRESULT CALLBACK
bpln(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  std::istringstream iss(aArgs);
  ULONG64 address = 0;
  iss >> std::hex >> address;
  if (!iss) {
    dprintf("Failed to parse address parameter\n");
    return E_FAIL;
  }
  std::string symOutput;
  ULONG64 symOffset;
  if (!NearestSymbol(address, symOutput, symOffset)) {
    symOutput = "<No symbol found>";
  }
  if (gPointerWidth == 4) {
    dprintf("(%08I64x)   %s\n", symOffset, symOutput.c_str());
  } else {
    dprintf("(%016I64x)   %s\n", symOffset, symOutput.c_str());
  }
  return S_OK;
}
