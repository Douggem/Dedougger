#pragma once

#include <Windows.h>
#include <assert.h>
#include <map>
#include <memory>
#include <Psapi.h>
#include "PageBackupEx.h"

namespace dedougger {

	enum class PageDifferentialType : char {
		MEMORY_WATCH,
		READ_ONLY_PAGES
	};

	enum class MMapGenerationType : char {
		VIRTUAL_QUERY,
		WORKING_SET 
	};
	
	MEMORY_BASIC_INFORMATION set_block_to_mem_info(const PSAPI_WORKING_SET_BLOCK*);
	DWORD set_block_protection_to_mem_info_protection(ULONG_PTR protection);

	class PageRestorerEx {	
	protected:
		std::map<LPVOID, PageBackupEx*> pages;
		HANDLE process_handle;
		DWORD processId;
		bool free_unknown_pages;
		PageDifferentialType pdType;
		MMapGenerationType mmgType;

		int restore_page(LPVOID page);
		int save_page(LPVOID page);
		int save_page(const PSAPI_WORKING_SET_BLOCK*);
		int save_page(PMEMORY_BASIC_INFORMATION);
		int save_state_working_set();
		int save_state_virtual_query();
		int restore_state_virtual_query();
		int restore_state_working_set();
	public:
		PageRestorerEx(DWORD processId);
		int restore_state();
		int save_state();
		bool touch_address(LPVOID address);
		void set_free_unknown_pages(bool val) { this->free_unknown_pages = val; }
		HANDLE get_process_handle() { return this->process_handle; }		
	};
	typedef std::unique_ptr<PageRestorerEx> UP_PageRestorerEx;
	typedef std::shared_ptr<PageRestorerEx> SP_PageRestorerEx;
}