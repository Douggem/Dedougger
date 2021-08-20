#include "PageRestorerEx.h"
#include <vector>
#include "dexception.h"

namespace dedougger {
	
	PageRestorerEx::PageRestorerEx(DWORD processId) {
		this->free_unknown_pages	= true;
		this->processId				= processId;
		this->mmgType				= MMapGenerationType::WORKING_SET;
		this->pdType				= PageDifferentialType::MEMORY_WATCH;
		this->process_handle		= OpenProcess(PROCESS_ALL_ACCESS, false, processId);

		if (this->process_handle == INVALID_HANDLE_VALUE) {
			throw OpenProcessFailedException();
		}
	}

	int PageRestorerEx::save_state() {
		
		PSAPI_WORKING_SET_INFORMATION singleSetInfo;
		PSAPI_WORKING_SET_INFORMATION* completeSetInfo;
		bool queryResult = QueryWorkingSet(this->process_handle, &singleSetInfo, sizeof(singleSetInfo));
		assert(!queryResult && ERROR_BAD_LENGTH == GetLastError());
		uint64_t numEntries = singleSetInfo.NumberOfEntries;
		size_t completeSetSize = sizeof(PSAPI_WORKING_SET_INFORMATION) * numEntries;
		completeSetInfo = (PSAPI_WORKING_SET_INFORMATION*)malloc(completeSetSize);
		queryResult = QueryWorkingSet(this->process_handle, &completeSetInfo, completeSetSize);

		assert(InitializeProcessForWsWatch(this->process_handle));

		//
		// We don't really care about the performance of save state - it's only called once, 
		// whereas restore state will be called hundreds of thousands of times.  So, even
		// though this class's API looks like it has functionality to use working set to 
		// save state, it is unused (for now).
		//
		return this->save_state_working_set();		
	}

	int PageRestorerEx::save_page(const PSAPI_WORKING_SET_BLOCK *block_info) {
		MEMORY_BASIC_INFORMATION mem_info = set_block_to_mem_info(block_info);
#ifdef _DEBUG
			// DEBUG CHECKS
			MEMORY_BASIC_INFORMATION mem_info_test;
			size_t bytes_returned;
			bytes_returned = VirtualQueryEx(this->process_handle, mem_info.BaseAddress, &mem_info_test, sizeof(mem_info_test));
			assert(mem_info.BaseAddress == mem_info_test.BaseAddress);
			assert(mem_info.Protect == mem_info_test.Protect);
		
#endif
		int pagesSaved = this->save_page(&mem_info);
		return pagesSaved;
	}

	int PageRestorerEx::save_page(PMEMORY_BASIC_INFORMATION mem_info) {
		PageBackupEx* page = nullptr;
		
		auto found = this->pages.find(mem_info->BaseAddress);
		if (found != this->pages.end())
		{
			//
			// If the page is already tracked, use the existing page.
			//
			 page = this->pages.at(mem_info->BaseAddress);
		} else {
			//
			// If the page isn't already in the map, create a new PageBackupEx,
			// add it to the map, and use it for the backup.
			// 
			page = new PageBackupEx(mem_info->BaseAddress, this->process_handle);
			this->pages[mem_info->BaseAddress] = page;
		}
		int result = page->backup(this->process_handle, mem_info);
		return result;
	}

	int PageRestorerEx::save_state_working_set() {
		SIZE_T bytes_returned = 0;
		PVOID current_page = nullptr;
		//
		// We're using a static vector here to avoid the allocs/frees on every state restoration.
		// I know I know, premature optimziation and all that.
		//
		static std::vector<PageBackupEx*> tracked_pages;
		static PSAPI_WORKING_SET_INFORMATION* workingSetPages;
		static size_t workingSetSize = sizeof(PSAPI_WORKING_SET_INFORMATION);
		if (workingSetPages == nullptr) {
			workingSetPages = (PSAPI_WORKING_SET_INFORMATION*)malloc(workingSetSize);
		}
		tracked_pages.clear();

		int pages_saved = 0;
		//
		// Enumerate all pages.  If it's a page we track, restore it.
		// If it's not a page we track, free it since it was likely 
		// allocated during the fuzz iteration.
		//
		bool qwsResult = QueryWorkingSet(this->process_handle, workingSetPages, workingSetSize);
		if (!qwsResult) {
			assert(GetLastError() == ERROR_BAD_LENGTH);
			workingSetSize = workingSetPages->NumberOfEntries * sizeof(PSAPI_WORKING_SET_INFORMATION);
			free(workingSetPages);
			workingSetPages = (PSAPI_WORKING_SET_INFORMATION*)malloc(workingSetSize);
			qwsResult = QueryWorkingSet(this->process_handle, workingSetPages, workingSetSize);
			auto error = GetLastError();
			assert(qwsResult);
		}

		for (int i = 0; i < workingSetPages->NumberOfEntries; i++) {
			PSAPI_WORKING_SET_BLOCK& page = workingSetPages->WorkingSetInfo[i];
			current_page = (PVOID)(page.VirtualPage << 12);
			MEMORY_BASIC_INFORMATION mem_info = { 0 };
			
			
			
			this->save_page(&page);
			pages_saved++;
			
		}

		return pages_saved;
	}

	int PageRestorerEx::save_state_virtual_query() {
		//
		// Enumerate all pages in the target process.  We do this by VirtualQuery'ing on 
		// address zero and walking to the next page by adding baseaddress and regionsize
		//
		SIZE_T bytes_returned = 0;
		MEMORY_BASIC_INFORMATION mem_info = { 0 };
		PVOID current_page = nullptr;
		int pages_saved = 0;
		do {
			bytes_returned = VirtualQueryEx(this->process_handle, current_page, &mem_info, sizeof(mem_info));
			if (bytes_returned > 0) {
				current_page = mem_info.BaseAddress;
				if (mem_info.State == MEM_COMMIT) {
					//
					// If and only if the page is committed, save a snapshot of it.
					// Non committed pages are ignored.  
					//
					this->save_page(&mem_info);
					pages_saved++;
					//
					// Re-do the VirtualQuery call in case some pages got coalesced 
					// after our VirtualProtect
					//
					bytes_returned = VirtualQueryEx(this->process_handle, current_page, &mem_info, sizeof(mem_info));
				}
				current_page = (BYTE*)current_page + mem_info.RegionSize;
			}
		} while (bytes_returned > 0);

		return pages_saved;
	}

	int PageRestorerEx::restore_state_virtual_query()	{
		SIZE_T bytes_returned = 0;
		MEMORY_BASIC_INFORMATION mem_info = { 0 };
		PVOID current_page = nullptr;
		//
		// We're using a static vector here to avoid the allocs/frees on every state restoration.
		// I know I know, premature optimziation and all that.
		//
		static std::vector<PageBackupEx*> tracked_pages;
		tracked_pages.clear();

		int pages_restored = 0;
		int pages_killed = 0;
		//
		// Enumerate all pages.  If it's a page we track, restore it.
		// If it's not a page we track, free it since it was likely 
		// allocated during the fuzz iteration.
		//

		do {
			bytes_returned = VirtualQueryEx(this->process_handle, current_page, &mem_info, sizeof(mem_info));
			if (bytes_returned > 0) {
				current_page = mem_info.BaseAddress;
				if (mem_info.State == MEM_COMMIT) {
					//
					// If and only if the page is committed do we restore it OR free it
					// if it isn't tracked.
					//
					try {
						tracked_pages.push_back(this->pages.at(mem_info.BaseAddress));

					}
					catch (std::out_of_range e) {
						// If it's not a page we're tracking, kill it
						//printf("Freeing page\n");
						bool success = VirtualFreeEx(this->process_handle, mem_info.BaseAddress, 0, MEM_RELEASE);
						pages_killed++;
						if (!success) {
							int error = GetLastError();
							int boo = 0;

						}
					}
				}
				current_page = (BYTE*)current_page + mem_info.RegionSize;
			}
		} while (bytes_returned > 0);

		for (int i = tracked_pages.size() - 1; i >= 0; i--) {
			tracked_pages[i]->restore(this->process_handle);
			pages_restored++;
		}

		return pages_restored;
	}

	int PageRestorerEx::restore_state_working_set()	{
		SIZE_T bytes_returned = 0;
		PVOID current_page = nullptr;
		//
		// We're using a static vector here to avoid the allocs/frees on every state restoration.
		// I know I know, premature optimziation and all that.
		//
		static std::vector<PageBackupEx*> tracked_pages;
		static PSAPI_WORKING_SET_INFORMATION *workingSetPages;
		static size_t workingSetSize = sizeof(PSAPI_WORKING_SET_INFORMATION);
		if (workingSetPages == nullptr) {
			workingSetPages = (PSAPI_WORKING_SET_INFORMATION*)malloc(workingSetSize);
		}
		tracked_pages.resize(0);
		
		int pages_restored = 0;
		int pages_killed = 0;
		//
		// Enumerate all pages.  If it's a page we track, restore it.
		// If it's not a page we track, free it since it was likely 
		// allocated during the fuzz iteration.
		//
		bool qwsResult = QueryWorkingSet(this->process_handle, workingSetPages, workingSetSize);
		if (!qwsResult) {
			assert(GetLastError() == ERROR_BAD_LENGTH);
			workingSetSize = workingSetPages->NumberOfEntries * sizeof(PSAPI_WORKING_SET_INFORMATION);
			free(workingSetPages);
			workingSetPages = (PSAPI_WORKING_SET_INFORMATION*)malloc(workingSetSize);
			qwsResult = QueryWorkingSet(this->process_handle, workingSetPages, workingSetSize);
			auto error = GetLastError();
			assert(qwsResult);
		}

		for (int i = 0; i < workingSetPages->NumberOfEntries; i++) {
			PSAPI_WORKING_SET_BLOCK &page = workingSetPages->WorkingSetInfo[i];
			current_page = (PVOID)(page.VirtualPage << 12);
			auto tracked_candidate = this->pages.find(current_page);
			if (tracked_candidate != this->pages.end()) {
				tracked_pages.push_back(tracked_candidate->second);
			}
			else {
				// If it's not a page we're tracking, kill it
				//printf("Freeing page\n");
				bool success = VirtualFreeEx(this->process_handle, current_page, 0, MEM_RELEASE);
				pages_killed++;
				if (!success) {
					int error = GetLastError();
					int boo = 0;
				}
			}
		}

		
		for (int i = tracked_pages.size() - 1; i >= 0; i--) {
			auto tracked_page = tracked_pages[i];
			if (tracked_page->is_dirty()) {
				tracked_page->restore(this->process_handle);
				pages_restored++;
			}
		}

		return pages_restored;
	}	

	int PageRestorerEx::restore_state() {
		if (false) {
			return this->restore_state_virtual_query();
		}
		else {
			return this->restore_state_working_set();
		}
	}

	bool PageRestorerEx::touch_address(LPVOID address) {
		auto potential_block = this->pages.upper_bound(address);
		if (potential_block == this->pages.end()) {
			return false;
		}
		potential_block--;
		bool touched = false;
		if (potential_block != this->pages.end()) {
			PVOID lastByte = potential_block->second->get_page_last_byte();
			if ( lastByte > address) {
				potential_block->second->mark_dirty(this->process_handle);
				touched = true;
			}
			else {
				//
				// Page isn't in our map.  Do nothing.
				//
			}
		}
		else {
			//
			// Page isn't in our map.  Do nothing.
			//
		}
		return touched;
	}



	MEMORY_BASIC_INFORMATION set_block_to_mem_info(const PSAPI_WORKING_SET_BLOCK *block) {
		MEMORY_BASIC_INFORMATION result;
		DWORD protection = set_block_protection_to_mem_info_protection(block->Protection);
		result.AllocationBase = (PVOID)(block->VirtualPage << 12);
		result.AllocationProtect = protection;
		result.BaseAddress = (PVOID)(block->VirtualPage << 12);
		result.PartitionId = 0;
		result.Protect = protection;
		// The working set includes only physically available and allocated memory, thus it must
		// be committed.
		result.RegionSize = 4096; // all will be typical pages right?
		result.State = MEM_COMMIT;
		result.Type = 0; // TODO: Do we need to resolve this?
		
		return result;
	}

	DWORD set_block_protection_to_mem_info_protection(ULONG_PTR protection) {
		//
		// The memory set block protection is made up of flags, unlike the 
		// MEMORY_BASIC_INFORMATION protection constants.  e.g. executable and
		// read/write is Execute | ReadWrite in the memory set protection, where
		// that doesn't work in the normal MEMORY_BASIC_INFORMATION constant.
		//
		DWORD result = 0;
		DWORD base_protection = protection & 7;
		
		if (protection & 8) {
			result |= PAGE_NOCACHE;
		}
		
		if (protection & 16) {
			result |= PAGE_GUARD;
		}
		
		switch (protection) {
		case 1:
			result |= PAGE_READONLY;
			break;
		case 2:
			result |= PAGE_EXECUTE;
			break;
		case 3:
			result |= PAGE_EXECUTE_READ;
			break;
		case 4:
			result |= PAGE_READWRITE;
			break;
		case 5:
			result |= PAGE_WRITECOPY;
			break;
		case 6:
			result |= PAGE_EXECUTE_READWRITE;
			break;
		case 7:
			result |= PAGE_EXECUTE_WRITECOPY;
			break;
		}

		return result;
	}

}