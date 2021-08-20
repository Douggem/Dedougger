
#include "PEInfo.hpp"

void PEInfoEx::Parse(HANDLE processHandle) {
	IMAGE_DOS_HEADER		dosHeader;
	IMAGE_NT_HEADERS		ntHeaders;
	size_t bytesRead;
	bool rpmResult;
	
	rpmResult = ReadProcessMemory(processHandle, (void*)this->moduleBase, &dosHeader, sizeof(dosHeader), &bytesRead);
	size_t ntHeadersAddress = this->moduleBase + dosHeader.e_lfanew;
	rpmResult = ReadProcessMemory(processHandle, (void*)ntHeadersAddress, &ntHeaders, sizeof(ntHeaders), &bytesRead);
	this->entryPointOffset = ntHeaders.OptionalHeader.AddressOfEntryPoint;
	size_t firstSectionAddress = (((ULONG_PTR)(ntHeadersAddress)+FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader) + ((ntHeaders)).FileHeader.SizeOfOptionalHeader));
	this->ParseDirectories(processHandle, ntHeaders, firstSectionAddress);	

}

void PEInfoEx::ParseDirectories(HANDLE processHandle, const IMAGE_NT_HEADERS & ntHeaders, size_t firstSectionAddress) {
	//
	// We have to read strings from the target binary.  Using a MAX_PATH sized buffer
	// isn't sound but it'll do for now.  We'll read strings into nameBuf and copy them
	// out into string objects as needed.
	//
	char nameBuf[MAX_PATH];
	bool rpmResult;
	size_t bytesRead;

	for (int i = 0; i < ntHeaders.FileHeader.NumberOfSections; i++) {
		IMAGE_SECTION_HEADER currentSectionHeader;
		size_t currentSectionAddress = firstSectionAddress + (i * sizeof(IMAGE_SECTION_HEADER));
		rpmResult = ReadProcessMemory(processHandle, (void*)currentSectionAddress, &currentSectionHeader, sizeof(currentSectionHeader), &bytesRead);
		std::string sectionName((const char*)&currentSectionHeader.Name, 8);
		this->sections[sectionName] = currentSectionHeader;
	}

	for (int directoryType = DATA_DIRECTORY_TYPE::EXPORT_TABLE; directoryType <= DATA_DIRECTORY_TYPE::RESERVED; directoryType++) {
		const IMAGE_DATA_DIRECTORY *directory = &ntHeaders.OptionalHeader.DataDirectory[directoryType];
		switch (directoryType) {
		case(DATA_DIRECTORY_TYPE::IMPORT_TABLE): {
			this->ParseImportTable(processHandle, directory);
			break;
		}
		case (DATA_DIRECTORY_TYPE::IMPORT_ADDRESS_TABLE): {
			break;
		} case(DATA_DIRECTORY_TYPE::EXPORT_TABLE): {
			this->ParseExportTable(processHandle, directory);
			break;
		}

		}
	}
}

void PEInfoEx::ParseExportTable(HANDLE processHandle, const IMAGE_DATA_DIRECTORY* directory) {
	char nameBuf[MAX_PATH];
	bool rpmResult;
	size_t bytesRead;
	WORD *ordinalTable;
	DWORD *funcAddressTable;
	DWORD *funcNameTable;
	DWORD funcNameRva;
	WORD ordinal;
	DWORD funcRva;

	IMAGE_EXPORT_DIRECTORY exportDesc;
	if (directory->Size == 0) {
		return;
	}
	IMAGE_EXPORT_DIRECTORY *exportDescAddress = (IMAGE_EXPORT_DIRECTORY*)(this->moduleBase + directory->VirtualAddress);
	ReadProcessMemory(processHandle, (void*)(exportDescAddress), &exportDesc, sizeof(exportDesc), &bytesRead);
	
	ordinalTable = (WORD*)(this->moduleBase + exportDesc.AddressOfNameOrdinals);
	funcAddressTable = (DWORD*)(this->moduleBase + exportDesc.AddressOfFunctions);
	funcNameTable = (DWORD*)(this->moduleBase + exportDesc.AddressOfNames);

	for (int ii = 0; ii < exportDesc.NumberOfNames; ii++) {
		std::string funcName;
		ReadProcessMemory(processHandle, (void*)(&funcNameTable[ii]), &funcNameRva, sizeof(funcNameRva), &bytesRead);
		ReadProcessMemory(processHandle, (void*)(this->moduleBase + funcNameRva), &nameBuf, sizeof(nameBuf), &bytesRead);
		ReadProcessMemory(processHandle, (void*)(&ordinalTable[ii]), &ordinal, sizeof(ordinal), &bytesRead);
		ReadProcessMemory(processHandle, (void*)(&funcAddressTable[ordinal]), &funcRva, sizeof(funcRva), &bytesRead);

		funcName = nameBuf;
		SP_ExportedFunction exFunc = std::make_shared<ExportedFunction>(nameBuf, ordinal + exportDesc.Base, this->moduleBase + funcRva);
		this->exportedFuncsByName[funcName] = exFunc;
		this->exportedFuncsByOrdinal[ordinal] = exFunc;		
	}	
}

void PEInfoEx::ParseImportTable(HANDLE processHandle, const IMAGE_DATA_DIRECTORY * directory) {
	char nameBuf[MAX_PATH];
	bool rpmResult;
	size_t bytesRead;
	IMAGE_IMPORT_DESCRIPTOR importDesc;
	if (directory->Size == 0) {
		return;
	}
	uint8_t *importDescAddress = (uint8_t*)(this->moduleBase + directory->VirtualAddress);
	ReadProcessMemory(processHandle, (void*)(importDescAddress), &importDesc, sizeof(importDesc), &bytesRead);
	while (importDesc.Name != 0) {
		std::string moduleName;
		std::vector<ImportedFunction> importedFunctions;
		ImportLookupTable iatEntry;

		ReadProcessMemory(processHandle, (void*)(this->moduleBase + importDesc.Name), &nameBuf, sizeof(nameBuf), &bytesRead);
		moduleName = nameBuf;
		SP_ImportedModule module = std::make_shared<ImportedModule>(moduleName);
		if (importDesc.Characteristics != 0) {
			size_t thunkTableEntry;
			size_t *thunkTableAddress;

			ImportLookupTable *iatAddress = reinterpret_cast<ImportLookupTable*>(this->moduleBase + importDesc.Characteristics);
			thunkTableAddress = (size_t*)(this->moduleBase + importDesc.FirstThunk);

			ReadProcessMemory(processHandle, (void*)(iatAddress), &iatEntry, sizeof(iatEntry), &bytesRead);
			while (iatEntry.qword != 0) {
				std::string functionName;
				if (iatEntry.ordinalFlag) {
					ReadProcessMemory(processHandle, (void*)(thunkTableAddress),
						&thunkTableEntry, sizeof(thunkTableEntry), &bytesRead);
					ImportedFunction func(iatEntry.ordinalNumber, thunkTableEntry);
					importedFunctions.push_back(std::move(func));
					module->AddImportedFunction(func);
				}
				else {
					rpmResult = ReadProcessMemory(processHandle, (void*)(this->moduleBase + iatEntry.nameTableRva + FIELD_OFFSET(ImportNameTable, name)),
						&nameBuf, sizeof(nameBuf), &bytesRead);
					ReadProcessMemory(processHandle, (void*)(thunkTableAddress),
						&thunkTableEntry, sizeof(thunkTableEntry), &bytesRead);
					ImportedFunction func(nameBuf, thunkTableEntry);
					module->AddImportedFunction(func);
					importedFunctions.push_back(std::move(func));
				}
				iatAddress++;
				thunkTableAddress++;
				size_t iatEntrySize = sizeof(iatEntry);
				ReadProcessMemory(processHandle, (void*)(iatAddress), &iatEntry, iatEntrySize, &bytesRead);
			}
			importDescAddress += sizeof(importDesc);
			ReadProcessMemory(processHandle, (void*)(importDescAddress), &importDesc, sizeof(importDesc), &bytesRead);
		}
		this->importedModules[moduleName] = module;
	}
}

PEInfoEx::PEInfoEx(size_t moduleBase, HANDLE processHandle) {
	this->moduleBase = moduleBase;
	this->Parse(processHandle);
}
