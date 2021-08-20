#pragma once

#include <exception>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <Windows.h>

class FunctionNotFoundException : public std::exception {};
//
// PE data types that we'll ReadProcessMemory into.  All data in these
// is sourced from the target process - so don't assume that pointers, 
// RVAs, etc. can be dereffed willy nilly.  Where necessary, we take 
// data out of these and turn them into data that can be used within
// the class freely without having to reach into the target process.
//
enum DATA_DIRECTORY_TYPE {
	EXPORT_TABLE = 0,
	IMPORT_TABLE,
	RESOURCE_TABLE,
	EXCEPTION_TABLE,
	CERTIFICATE_TABLE,
	RELOCATION_TABLE,
	DEBUG_DATA,
	ARCHITECTURE_DATA,
	MACHINE_VALUE,
	TLS_TABLE,
	LOAD_CONFIGURATION_TABLE,
	BOUND_IMPORT_TABLE,
	IMPORT_ADDRESS_TABLE,
	DELAY_IMPORT_DESCRIPTOR,
	COM_RUNTIME_HEADER,
	RESERVED
};

struct ImportLookupTable {
#ifdef _AMD64_
	union {
		union {
			uint16_t ordinalNumber;
			uint32_t nameTableRva;
		};
		struct {
			uint8_t padding[7];
			uint8_t ordinalFlag;
		};
		uint64_t qword;
	};
#endif
};

struct ImportNameTable {
	uint16_t hint;
	char name; // null terminated cstring
};

class ImportedFunction {
	std::string functionName;
	size_t ordinal;
	size_t functionAddress;
	bool importedByOrdinal;
public:
	ImportedFunction(std::string funcName, size_t functionAddress) {
		this->functionName = funcName;
		this->functionAddress = functionAddress;
		this->importedByOrdinal = false;
		this->ordinal = -1;
	}
	
	ImportedFunction(size_t ordinal, size_t functionAddress) {
		this->ordinal = ordinal;
		this->functionAddress = functionAddress;
	}
	
	ImportedFunction(const ImportedFunction &other) {
		this->functionName = other.functionName;
		this->functionAddress = other.functionAddress;
		this->importedByOrdinal = other.importedByOrdinal;
		this->ordinal = other.ordinal;
	}

	ImportedFunction(ImportedFunction &&other) {
		this->functionName = std::move(other.functionName);
		this->functionAddress = other.functionAddress;
		this->importedByOrdinal = other.importedByOrdinal;
		this->ordinal = other.ordinal;
	}

	bool ImportedByOrdinal() const { return this->importedByOrdinal != 0; }
	size_t Ordinal() const { return this->ordinal; }
	std::string FunctionName() const { return this->functionName; }
};
typedef std::shared_ptr<ImportedFunction> SP_ImportedFunction;
typedef std::unique_ptr<ImportedFunction> UP_ImportedFunction;

class ExportedFunction {
	std::string functionName;
	size_t ordinal;
	size_t functionAddress;
public:
	ExportedFunction(std::string functionName, size_t ordinal, size_t functionAddress) {
		this->functionName = functionName;
		this->functionAddress = functionAddress;
		this->ordinal = ordinal;
	}

	ExportedFunction(const ExportedFunction &other) {
		this->functionName = other.functionName;
		this->functionAddress = other.functionAddress;
		this->ordinal = other.ordinal;
	}

	ExportedFunction(const ExportedFunction &&other) {
		this->functionName = std::move(other.functionName);
		this->functionAddress = other.functionAddress;
		this->ordinal = other.ordinal;
	}

	const std::string	GetFunctionName() const { return this->functionName; }
	const size_t		GetFunctionAddress() const { return this->functionAddress; }
};
typedef std::shared_ptr<ExportedFunction> SP_ExportedFunction;
typedef std::unique_ptr<ExportedFunction> UP_ExportedFunction;

class ImportedModule {
	std::string moduleName;
	std::map<std::string, SP_ImportedFunction> importedByName;
	std::map<size_t, SP_ImportedFunction> importedByOrdinal;
public:
	ImportedModule(std::string moduleName) {
		this->moduleName = moduleName;
	}

	void AddImportedFunction(ImportedFunction &importedFunc) {
		if (importedFunc.ImportedByOrdinal()) {
			this->importedByOrdinal[importedFunc.Ordinal()] = std::make_shared<ImportedFunction>(importedFunc);
		}
		else {
			this->importedByName[importedFunc.FunctionName()] = std::make_shared<ImportedFunction>(importedFunc);
		}
	}

	std::string ModuleName() const { return this->moduleName; }
};
typedef std::shared_ptr<ImportedModule> SP_ImportedModule;
typedef std::shared_ptr<ImportedModule> UP_ImportedModule;

//
// Actual class that will parse the PE in the target process
// and store data about it.
//
class PEInfoEx {
	size_t moduleBase;
	std::string modulePath;
	std::string moduleName;
	size_t entryPointOffset;
	std::map<std::string, IMAGE_SECTION_HEADER> sections;
	std::map<std::string, SP_ImportedModule>	importedModules;
	std::map<std::string, SP_ExportedFunction>	exportedFuncsByName;
	std::map<WORD, SP_ExportedFunction>			exportedFuncsByOrdinal;

	void Parse(HANDLE processHandle);
	void ParseDirectories(HANDLE processHandle, const IMAGE_NT_HEADERS &ntHeaders, size_t firstSectionAddress);
	void ParseImportTable(HANDLE processHandle, const IMAGE_DATA_DIRECTORY *directory);
	void ParseExportTable(HANDLE processHandle, const IMAGE_DATA_DIRECTORY* directory);
public:
	PEInfoEx(size_t moduleBase, HANDLE processHandle);
	PEInfoEx(PEInfoEx&& other) {
		std::swap(this->modulePath, other.modulePath);
		std::swap(this->moduleName, other.moduleName);
		std::swap(this->sections, other.sections);
	}
	~PEInfoEx() {}

	size_t GetEntryPointOffset() { return this->entryPointOffset; }
	const SP_ImportedFunction GetImportedFunction(const std::string &moduleName, const std::string &functionName);
	const SP_ExportedFunction GetExportedFunction(const std::string &functionName) {
		auto foundFunc = this->exportedFuncsByName.find(functionName);
		if (foundFunc == this->exportedFuncsByName.end()) {
			throw(FunctionNotFoundException());
		}
		return foundFunc->second;
	}
};
typedef std::unique_ptr<PEInfoEx> UP_PEInfoEx;
typedef std::shared_ptr<PEInfoEx> SP_PEInfoEx;

