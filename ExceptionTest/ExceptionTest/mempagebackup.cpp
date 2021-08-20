#include "stdafx.h"
#include "mempagebackup.h"
namespace duzz {
	MemPageBackup::MemPageBackup(PMEMORY_BASIC_INFORMATION pageInfo) {
		this->page_info = *pageInfo;
		
		// Using VirtualAlloc so the memory is page aligned
		PVOID backup = VirtualAlloc(nullptr, pageInfo->RegionSize, pageInfo->State, pageInfo->Protect);
		memcpy(backup, pageInfo->BaseAddress, pageInfo->RegionSize);
	}

	void MemPageBackup::Restore() {
		DWORD oldProtect = 0;
		MEMORY_BASIC_INFORMATION freshInfo = { 0 };
		// Ensure the page hasn't been freed		
		VirtualQuery(this->page_info.BaseAddress, &freshInfo, sizeof(freshInfo));
		// if the page is MEM_FREE, we need to reallocate it.
		if (freshInfo.Type &= MEM_FREE) {
			VirtualAlloc(this->page_info.BaseAddress, this->page_info.RegionSize, this->page_info.State, this->page_info.Protect);
		}
		VirtualProtect(this->page_info.BaseAddress, this->page_info.RegionSize, this->page_info.Protect, &oldProtect);
		memcpy(this->page_info.BaseAddress, this->page_backup, this->page_info.RegionSize);
	}
}