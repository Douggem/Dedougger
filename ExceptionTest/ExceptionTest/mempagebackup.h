#pragma once
#include "stdafx.h"

namespace duzz {

	class MemPageBackup {
		LPVOID original_page_address;
		LPVOID page_backup;
		MEMORY_BASIC_INFORMATION page_info;
	public:
		MemPageBackup(PMEMORY_BASIC_INFORMATION pageInfo);
		void Restore();
	};
}