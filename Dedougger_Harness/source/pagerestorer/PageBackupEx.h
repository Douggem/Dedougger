#pragma once

#include <Windows.h>
#include <stdio.h>
#include "dexception.h"

namespace dedougger {
	class PageBackupEx {
		MEMORY_BASIC_INFORMATION page_info;
		PVOID page_address;		
		PVOID data;
		bool dirty;
		bool trackPageChanges;
		DWORD original_protect;
		int ProtectPage(HANDLE process);
		int resize(HANDLE process);
	public:
		PageBackupEx(PVOID remote_page, HANDLE process, bool trackPageChanges = true);
		int restore(HANDLE process);
		int backup(HANDLE process);
		int backup(HANDLE process, PMEMORY_BASIC_INFORMATION memInfo);
		void mark_dirty(HANDLE process);
		bool is_dirty() { return this->dirty; }
		PVOID get_page_address() { return this->page_address; }
		SIZE_T get_page_size() { return this->page_info.RegionSize; }
		PVOID get_page_last_byte() { return (PVOID)((SIZE_T)this->page_address + this->page_info.RegionSize); }
	};
}