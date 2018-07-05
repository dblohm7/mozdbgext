#include "mozdbgext.h"
#include "mozdbgextcb.h"
#include "pe.h"
#include "bpsyms.h"

#include <pathcch.h>
#include <winnt.h>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include <algorithm>
#include <assert.h>
#include <fstream>
#include <iomanip>
#include <ios>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

#pragma pack(push, 1)
struct IMAGE_DEBUG_INFO_CODEVIEW
{
  DWORD mSignature;
  GUID  mGuid;
  DWORD mAge;
  char  mPath[1];
};
#pragma pack(pop)

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
  ULONG64 dbgBase;
  ULONG dbgSize;
  if (!GetDataDirectoryEntry(aBase, IMAGE_DIRECTORY_ENTRY_DEBUG, dbgBase,
                             dbgSize)) {
    return false;
  }
  ULONG numDataDirEntries = dbgSize / sizeof(IMAGE_DEBUG_DIRECTORY);
  auto dbgDir = std::make_unique<IMAGE_DEBUG_DIRECTORY[]>(numDataDirEntries);
  HRESULT hr = gDebugDataSpaces->ReadVirtual(dbgBase, dbgDir.get(), dbgSize,
                                             nullptr);
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
  symprintf("Warning: No PDB reference found inside module \"%S\"\n",
            GetModuleName(aBase).c_str());
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

template <typename CharType, size_t N>
static bool
startswith(const std::basic_string<CharType>& aStr,
           const CharType (&aLiteral)[N])
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

namespace {

struct BpLineInfo
{
  BpLineInfo(ULONG64 aRva, ULONG aSize, ULONG64 aFileId, ULONG64 aNameInt)
    : mRva(aRva)
    , mSize(aSize)
    , mFileId(aFileId)
    , mNameInt(aNameInt)
  {
  }

  ULONG64 mRva;
  ULONG   mSize;
  ULONG64 mFileId;
  ULONG64 mNameInt;
};

struct ModuleInfo;

// 1) sort by offset to do symbol lookup;
// 2) sort by name to do name lookup
struct BpSymbolInfo
{
  BpSymbolInfo(ULONG64 aRva, ULONG aSize, std::string& aName,
               std::string& aParams)
    : mRva(aRva)
    , mSize(aSize)
    , mName(aName)
    , mParams(aParams)
  {
  }
  ULONG64     mRva;
  ULONG       mSize;
  std::string mName;
  std::string mParams;
  std::weak_ptr<ModuleInfo> mModule;
};

struct ModuleInfo
{
  ModuleInfo(ULONG aSize, const std::string& aName)
    : mSize(aSize)
    , mName(aName)
  {
  }
  ULONG64     mSize;
  std::string mName;
  typedef std::unordered_map<ULONG64,std::string> FileMapType;
  FileMapType mFileMap;
  typedef std::map<UINT64,BpLineInfo> LineMapType;
  LineMapType mSourceLineSyms;
  typedef std::shared_ptr<BpSymbolInfo> MapValueSymbol;
  typedef std::map<UINT64,MapValueSymbol> SymbolRvaMapType;
  SymbolRvaMapType mSymsByRva;
  typedef std::map<std::string,MapValueSymbol> SymbolNameMapType;
  SymbolNameMapType mSymsByName;
};

struct ModuleKey
{
  ModuleKey(ULONG aPid, ULONG64 aBase)
    : mPid(aPid)
    , mBase(aBase)
  {}
  bool operator<(const ModuleKey& aOther) const
  {
    return mPid < aOther.mPid ||
           mPid == aOther.mPid && mBase < aOther.mBase;
  }
  ULONG   mPid;
  ULONG64 mBase;
};

} // anonymous namespace

static std::map<std::string,std::shared_ptr<ModuleInfo>> gModuleInfoByName;
static std::map<ModuleKey,std::shared_ptr<ModuleInfo>> gModuleInfoByKey;

std::shared_ptr<ModuleInfo>
EmplaceModule(const ULONG aPid, const std::string& aModName,
              DEBUG_MODULE_PARAMETERS& aModParams)
{
  auto module = gModuleInfoByName.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(aModName),
      std::forward_as_tuple(std::make_shared<ModuleInfo>(aModParams.Size,
                                                         aModName)));
  gModuleInfoByKey.emplace(
                      std::piecewise_construct,
                      std::forward_as_tuple(ModuleKey(aPid, aModParams.Base)),
                      std::forward_as_tuple((*module.first).second));

  return module.first->second;
}

template <typename CallbackT>
static void
LoadBpSymbolFile(ModuleInfo& aModuleInfo, const wchar_t* aSymPath,
                 CallbackT&& aCb)
{
  // Don't load the symbols if we already have them in memory.
  if (!aModuleInfo.mSymsByRva.empty()) {
    return;
  }

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
  ModuleInfo::FileMapType& fileMap = aModuleInfo.mFileMap;
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
      // Distinguish between func(params) and operator()(params)
      auto paramPos = tokens[4].find('(');
      const auto strOperatorLen = ArrayLength("operator") - 1;
      if (paramPos != std::string::npos && paramPos >= strOperatorLen) {
        auto startPos = paramPos - strOperatorLen;
        if (tokens[4].find("operator", startPos, strOperatorLen) == startPos) {
          paramPos = tokens[4].find('(', paramPos + 1);
        }
      }
      std::string symName(tokens[4].substr(0, paramPos));
      std::string params;
      if (paramPos != std::string::npos) {
        params = tokens[4].substr(paramPos);
      }
      aCb(eFunctionSymbol, address, size, aModuleInfo, symName, params,
          0, 0);
    } else if(startswith(line, "PUBLIC ")) {
      auto tokens = split(line, ' ', 4);
      std::istringstream issAddress(tokens[1]);
      ULONG64 address;
      issAddress >> std::hex >> address;
      aCb(ePublicSymbol, address, 0, aModuleInfo, tokens[3], std::string(),
          0, 0);
    } else if (startswith(line, "FILE ")) {
      auto tokens = split(line, ' ', 3);
      std::istringstream issFileId(tokens[1]);
      ULONG64 fileId;
      issFileId >> std::dec >> fileId;
      SanitizeFilePath(fileMap[fileId] = tokens[2]);
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
      aCb(eLineSymbol, address, size, aModuleInfo, std::string(),
          std::string(), fileId, lineNo);
    }
  }
  symprintf("Loaded Module \"%s\"\n", moduleName.c_str());
}

template <typename CallbackT>
static bool
LoadBpSymbols(const std::wstring& aBasePdbPath, ModuleInfo& aModuleInfo,
              const ULONG64 aBase, CallbackT&& aCb)
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
  LoadBpSymbolFile(aModuleInfo, pdbPath, aCb);
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
  DisableStdioSync& operator=(DisableStdioSync&&) = delete;

  const bool  mPrevSetting;
};

} // anonymous namespace

template <typename CallbackT>
static void
LoadBpSymbolsForModules(const char* aPath, CallbackT&& aCb)
{
  DisableStdioSync stdioSyncDisabled;

  ULONG pid;
  HRESULT hr = gDebugSystemObjects->GetCurrentProcessId(&pid);
  if (FAILED(hr)) {
    dprintf("GetCurrentProcessId failed\n");
    return;
  }

  // For now we only support loading from a single bp symbol path
  if (!gBasePdbPath.empty() && HasModuleInfoForPid(pid)) {
    dprintf("Breakpad symbols are already loaded for this process\n");
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
  hr = gDebugSymbols->GetNumberModules(&numLoaded, &numUnloaded);
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
    std::shared_ptr<ModuleInfo> moduleInfo = EmplaceModule(pid, modName,
                                                           modules[i]);
    LoadBpSymbols(basePdbPath, *moduleInfo, modules[i].Base, aCb);
  }

  mozilla::DbgExtCallbacks::RegisterModuleEventListener(
    [=](PCWSTR aModName, ULONG64 aBaseAddress, bool aIsLoad) -> void {
      if (!aIsLoad) {
        gModuleInfoByKey.erase(ModuleKey(pid, aBaseAddress));
        return;
      }

      DEBUG_MODULE_PARAMETERS modParams;
      HRESULT hr = gDebugSymbols->GetModuleParameters(1, &aBaseAddress, 0,
                                                      &modParams);
      if (FAILED(hr)) {
        return;
      }
      std::string name(modParams.ModuleNameSize, 0);
      hr = gDebugSymbols->GetModuleNameString(DEBUG_MODNAME_MODULE,
                                              DEBUG_ANY_ID, aBaseAddress,
                                              &name[0],
                                              modParams.ModuleNameSize,
                                              nullptr);
      if (FAILED(hr)) {
        return;
      }
      name.resize(modParams.ModuleNameSize - 1);
      std::shared_ptr<ModuleInfo> moduleInfo = EmplaceModule(pid, name,
                                                             modParams);
      LoadBpSymbols(gBasePdbPath, *moduleInfo, aBaseAddress, aCb);
    }
  );
}

HRESULT CALLBACK
bpsynthsyms(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  LoadBpSymbolsForModules(aArgs, [](SymbolType aType, ULONG64 aRva,
                                    ULONG aSize, ModuleInfo& aModuleInfo,
                                    std::string& aName, std::string& aParams,
                                    ULONG64 aFileId, ULONG64 aNameInt)
                                      -> void {
#if 0
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
#endif
    });
  return S_OK;
}

static void
ClearModuleInfoForPid(ULONG aPid)
{
  auto first = gModuleInfoByKey.lower_bound(ModuleKey(aPid, std::numeric_limits<ULONG64>::min()));
  auto last = gModuleInfoByKey.upper_bound(ModuleKey(aPid, std::numeric_limits<ULONG64>::max()));

  // Once we've deleted all ModuleInfo for that pid, we need to see if
  // that module is referenced by any other pids. Save the affected modules
  // off to a temporary vector so that we can look into that.
  std::vector<std::shared_ptr<ModuleInfo>> affectedModules;

  for (auto itr = first; itr != last; ++itr) {
    assert(itr->first.mPid == aPid);
    affectedModules.emplace_back(itr->second);
  }

  gModuleInfoByKey.erase(first, last);

  // Now that the modules for the pid are gone, we should be able to check
  // the refcount of the affected modules to tell us whether we can delete
  // their entry from gModuleInfoByName as well.
  for (auto&& m : affectedModules) {
    if (m.use_count() == 2) {
      // ie, the only users of this module are gModuleInfoByName and
      // affectedModules
      gModuleInfoByName.erase(m->mName);
    }
  }
}

static bool
HasModuleInfoForPid(ULONG aPid)
{
  auto first = gModuleInfoByKey.lower_bound(ModuleKey(aPid, std::numeric_limits<ULONG64>::min()));
  auto last = gModuleInfoByKey.upper_bound(ModuleKey(aPid, std::numeric_limits<ULONG64>::max()));
  return first != last;
}

HRESULT CALLBACK
bploadsyms(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  static const bool kRegdPidUnload =
    mozilla::DbgExtCallbacks::RegisterProcessDetachListener(&ClearModuleInfoForPid);
  LoadBpSymbolsForModules(aArgs, [](SymbolType aType, ULONG64 aRva,
                                    ULONG aSize, ModuleInfo& aModuleInfo,
                                    std::string& aName, std::string& aParams,
                                    ULONG64 aFileId, ULONG64 aNameInt)
                                      -> void {
      if (aType == eLineSymbol) {
        aModuleInfo.mSourceLineSyms.emplace(
                                std::piecewise_construct,
                                std::forward_as_tuple(aRva),
                                std::forward_as_tuple(aRva, aSize, aFileId,
                                                      aNameInt));
        return;
      }

      auto symPtr = std::make_shared<BpSymbolInfo>(aRva, aSize, aName, aParams);
      aModuleInfo.mSymsByRva.emplace(aRva, symPtr);
      aModuleInfo.mSymsByName.emplace(aName, std::move(symPtr));
    });
  return S_OK;
}

HRESULT CALLBACK
bpsyminfo(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  unsigned int symCount = 0;
  unsigned int lineCount = 0;
  for (auto&& i : gModuleInfoByName) {
    symCount += i.second->mSymsByRva.size();
    lineCount += i.second->mSourceLineSyms.size();
  }
  dprintf("%u breakpad symbols loaded\n%u source line symbols loaded\n",
          symCount, lineCount);
  return S_OK;
}

static const size_t kSymbolBufSize = 0x1000000;

enum NearestSymbolFlags
{
  eDMLOutput = 1,
  eIncludeLineNumbers = 2 | eDMLOutput,
  eLazyAddSynthSyms = 4
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

static inline void
EscapeForDml(std::string& aStr)
{
  // This one needs to be first so we don't convert other entities
  FindAndReplace(aStr, std::string("&"), std::string("&amp;"));
  FindAndReplace(aStr, std::string("<"), std::string("&lt;"));
  FindAndReplace(aStr, std::string(">"), std::string("&gt;"));
  FindAndReplace(aStr, std::string("\""), std::string("&quot;"));
}

static bool
GetEnclosingModule(ULONG64 const aOffset, std::string* aOutModuleName = nullptr)
{
  ULONG index;
  HRESULT hr = gDebugSymbols->GetModuleByOffset(aOffset, 0, &index, nullptr);
  if (FAILED(hr)) {
    return false;
  }

  if (!aOutModuleName) {
    return true;
  }

  ULONG nameLen = 0;
  hr = gDebugSymbols->GetModuleNameString(DEBUG_MODNAME_MODULE, index, 0,
                                          nullptr, 0, &nameLen);
  if (FAILED(hr)) {
    return false;
  }
  assert(nameLen > 0);

  aOutModuleName->resize(nameLen);
  hr = gDebugSymbols->GetModuleNameString(DEBUG_MODNAME_MODULE, index, 0,
                                          const_cast<char*>(aOutModuleName->c_str()),
                                          nameLen, nullptr);
  return hr == S_OK;
}

static bool
ResolveSymbolViaDbgEngine(ULONG64 const aOffset, std::string& aOutput,
                          ULONG64& aOutSymOffset, ULONG aFlags)
{
  ULONG64 displacement = 0;
  auto buf = std::make_unique<char[]>(kSymbolBufSize);
  ULONG bufSize = kSymbolBufSize;
  HRESULT hr = gDebugSymbols->GetNameByOffset(aOffset, buf.get(), bufSize,
                                              &bufSize, &displacement);
  if (FAILED(hr)) {
    return false;
  }
  // TODO: Get line numbers if eIncludeLineNumbers is set
  std::ostringstream oss;
  oss << buf.get() << "+0x" << std::hex << displacement << " (pdb)";
  gDebugSymbols->GetOffsetByName(buf.get(), &aOutSymOffset);
  aOutput = oss.str();
  return true;
}

static inline std::string
OutputPointerValue(ULONG64 const aOffset)
{
  unsigned int width = gPointerWidth * 2;
  std::ostringstream oss;
  oss << "0x" << std::hex << std::nouppercase << std::setfill('0')
      << std::setw(width) << aOffset;
  return oss.str();
}

bool
NearestSymbol(ULONG64 const aOffset, std::string& aOutput,
              ULONG64& aOutSymOffset, ULONG aFlags)
{
  aOutput.clear();

  ULONG pid;
  HRESULT hr = gDebugSystemObjects->GetCurrentProcessId(&pid);
  if (FAILED(hr)) {
    dprintf("GetCurrentProcessId failed\n");
    return false;
  }

  auto module = gModuleInfoByKey.upper_bound(ModuleKey(pid, aOffset));
  if (module == gModuleInfoByKey.begin()) {
    if (GetEnclosingModule(aOffset) &&
        ResolveSymbolViaDbgEngine(aOffset, aOutput, aOutSymOffset, aFlags)) {
      return true;
    }
    // We don't have a module for that, so just dump the hex value
    // (useful for JITcode)
    aOutput = OutputPointerValue(aOffset);
    return true;
  }
  // This returns the first module >, but if it's > then we actually want the
  // one <= that key
  --module;
  if (aOffset > (module->first.mBase + module->second->mSize)) {
    if (ResolveSymbolViaDbgEngine(aOffset, aOutput, aOutSymOffset, aFlags)) {
      return true;
    }
    // We don't have a module for that, so just dump the hex value
    // (useful for JITcode)
    aOutput = OutputPointerValue(aOffset);
    return true;
  }
  ULONG64 rvaLookup = aOffset - module->first.mBase;
  auto symbol = module->second->mSymsByRva.upper_bound(rvaLookup);
  if (symbol == module->second->mSymsByRva.begin()) {
    // Try to fall back to the symbol engine
    if (ResolveSymbolViaDbgEngine(aOffset, aOutput, aOutSymOffset, aFlags)) {
      return true;
    }
    aOutput = OutputPointerValue(aOffset);
    return true;
  }
  // This returns the first symbol >, but if it's > then we actually want the
  // one <= that RVA
  --symbol;
  aOutSymOffset = module->first.mBase + symbol->second->mRva;

  if (aFlags & eLazyAddSynthSyms) {
    gDebugSymbols->AddSyntheticSymbol(aOutSymOffset, symbol->second->mSize,
                                      symbol->second->mName.c_str(),
                                      DEBUG_ADDSYNTHSYM_DEFAULT, nullptr);
  }

  // We're going to output DML, so we need to escape any angle brackets in
  // the symbol name
  std::string symName(symbol->second->mName);
  if (aFlags & eDMLOutput) {
    EscapeForDml(symName);
  }

  std::string moduleName(module->second->mName);
  if (aFlags & eDMLOutput) {
    EscapeForDml(moduleName);
  }
  const auto& fileMap = module->second->mFileMap;

  std::ostringstream oss;
  ULONG64 offsetFromSym = aOffset - aOutSymOffset;
  oss << moduleName << "!" << symName << "+0x"
      << std::hex << offsetFromSym << std::flush;
  if ((aFlags & eIncludeLineNumbers) == eIncludeLineNumbers) {
    auto& sourceLineSyms = module->second->mSourceLineSyms;
    auto& lineSymbol = sourceLineSyms.upper_bound(aOffset);
    if (lineSymbol != sourceLineSyms.begin() &&
        lineSymbol != sourceLineSyms.end()) {
      --lineSymbol;
      // Look up the file name
      auto& fileItr = fileMap.find(lineSymbol->second.mFileId);
      if (fileItr == fileMap.end()) {
        dprintf("Error: File id does not map to a valid file name\n");
        return false;
      }
      std::string file(fileItr->second);
      EscapeForDml(file);
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
                       eDMLOutput | eLazyAddSynthSyms)) {
      symOutput = "<No symbol found>";
      EscapeForDml(symOutput);
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
  if (!NearestSymbol(address, symOutput, symOffset, eLazyAddSynthSyms)) {
    symOutput = "<No symbol found>";
  }
  if (gPointerWidth == 4) {
    dprintf("(%08I64x)   %s\n", symOffset, symOutput.c_str());
  } else {
    dprintf("(%016I64x)   %s\n", symOffset, symOutput.c_str());
  }
  return S_OK;
}

static bool
CrackSymbolicName(PCSTR aArgs, std::string& aOutModule, std::string& aSymName)
{
  aOutModule.clear();
  aSymName.clear();

  auto tokens = split(std::string(aArgs), '!', 2);
  if (tokens.size() != 2) {
    return false;
  }

  aOutModule = tokens[0];
  aSymName = tokens[1];
  return true;
}

static ModuleInfo::MapValueSymbol
LookupSymbolByName(const std::string& aModule, const std::string& aName)
{
  auto itr = gModuleInfoByName.find(aModule);
  if (itr == gModuleInfoByName.end()) {
    dprintf("Module \"%s\" not found\n", aModule.c_str());
    return nullptr;
  }

  auto entry = itr->second->mSymsByName.find(aName);
  if (entry == itr->second->mSymsByName.end()) {
    dprintf("Symbol \"%s!%s\" not found\n", aModule.c_str(), aName.c_str());
    return nullptr;
  }

  return entry->second;
}

HRESULT CALLBACK
bpbp(PDEBUG_CLIENT aClient, PCSTR aArgs)
{
  std::string module, name;

  if (!CrackSymbolicName(aArgs, module, name)) {
    dprintf("Failed to parse symbolic name; use |module!name| format\n");
    return E_FAIL;
  }

  auto sym = LookupSymbolByName(module, name);
  if (!sym) {
    return E_FAIL;
  }

  IDebugBreakpointPtr bp;
  HRESULT hr = gDebugControl->AddBreakpoint(DEBUG_BREAKPOINT_CODE,
                                            DEBUG_ANY_ID, &bp);
  if (FAILED(hr)) {
    dprintf("IDebugControl3::AddBreakpoint failed with HRESULT 0x%08X\n", hr);
    return hr;
  }

  ULONG64 offset;
  hr = gDebugSymbols->GetModuleByModuleName(module.c_str(), 0, nullptr, &offset);
  if (FAILED(hr)) {
    dprintf("IDebugSymbols2::GetModuleByModuleName failed with HRESULT 0x%08X\n", hr);
    return hr;
  }

  offset += sym->mRva;

  hr = bp->SetOffset(offset);
  if (FAILED(hr)) {
    dprintf("IDebugBreakpoint::SetOffset failed with HRESULT 0x%08X\n", hr);
    return hr;
  }

  hr = bp->AddFlags(DEBUG_BREAKPOINT_ENABLED);
  if (FAILED(hr)) {
    dprintf("IDebugBreakpoint::AddFlags failed with HRESULT 0x%08X\n", hr);
    return hr;
  }

  gDebugSymbols->AddSyntheticSymbol(offset, sym->mSize, sym->mName.c_str(),
                                    DEBUG_ADDSYNTHSYM_DEFAULT, nullptr);

  return S_OK;
}
