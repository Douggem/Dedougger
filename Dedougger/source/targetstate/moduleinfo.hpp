#pragma once
#include <string>
#include "targetstate\peinfo.hpp"
namespace dedougger {
	class ModuleInfo {
		std::string path;
		std::string name;
		size_t		base;	
		SP_PEInfoEx module;
	public:
		ModuleInfo() {
			this->base = 0;
		}

		ModuleInfo(size_t baseAddress, std::string filePath, HANDLE processHandle) {
			size_t pathIndex = filePath.find_first_of("\\\\?\\");
			if (pathIndex != std::string::npos) {
				this->path = filePath.substr(pathIndex + 4);
			}
			else {
				this->path = filePath.substr(pathIndex);
			}
			this->base = baseAddress;

			size_t moduleNameIndex = this->path.find_last_of('\\') + 1;
			this->name = this->path.substr(moduleNameIndex);
			this->module = std::make_shared<PEInfoEx>(this->base, processHandle);
		}

		ModuleInfo(ModuleInfo& other) {
			this->module	= other.module;
			this->path		= other.path;
			this->name		= other.name;
			this->base		= other.base;
		}

		ModuleInfo(ModuleInfo&& other) {
			this->module	= other.module;
			this->path		= std::move(other.path);
			this->name		= std::move(other.name);
			this->base		= other.base;
		}

		ModuleInfo& operator=(const ModuleInfo& other) {
			this->path = other.path;
			this->name = other.name;
			this->base = other.base;
			this->module = other.module;
			return *this;
		}

		const std::string GetModuleName() const { return this->name; }
		const size_t GetModuleBaseAddress() const { return this->base; };
		const SP_PEInfoEx GetPEInfo() const { return this->module; }
	};
}