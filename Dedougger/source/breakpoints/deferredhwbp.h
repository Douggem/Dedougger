#pragma once
#include <Windows.h>
#include "HwbpDescriptor.h"

namespace dedougger {
	class DeferredHWBP {
	public:
		char module_name[MAX_PATH];
		SIZE_T offset;
		HWBPDescriptor hwbp_descriptor;
		bool resolved;
		DeferredHWBP(const char* module_name, SIZE_T offset, BPCONDITION condition, BPLEN len) {
			this->hwbp_descriptor.address = -1;
			this->hwbp_descriptor.condition = condition;
			this->hwbp_descriptor.len = len;
			this->hwbp_descriptor.enabled = true;

			this->offset = offset;
			resolved = false;			
			
			strncpy_s(this->module_name, module_name, sizeof(this->module_name));
		}

		size_t resolve(size_t module_address) {
			SIZE_T constructed_address;
			constructed_address = (SIZE_T)module_address + this->offset;
			this->resolved = true;
			this->hwbp_descriptor.address = constructed_address;
			return constructed_address;
		}

	};
}