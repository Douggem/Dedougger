#pragma once

#include <Windows.h>

namespace dedougger {
	class DeferredSWBP {
	public:
		char module_name[MAX_PATH];
		bool resolved;
		SIZE_T offset;
		size_t address;


		DeferredSWBP(const char* module_name, SIZE_T offset) {
			this->address = 0;
			this->offset = offset;
			resolved = false;

			strncpy_s(this->module_name, module_name, sizeof(this->module_name));
		}

		size_t resolve(void* module_address) {
			SIZE_T constructed_address;
			constructed_address = (SIZE_T)module_address + this->offset;
			this->resolved = true;
			this->address = constructed_address;
			return constructed_address;
		}
	};
}