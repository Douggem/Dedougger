#include "PageBackupEx.h"


namespace dedougger {

	PageBackupEx::PageBackupEx(PVOID remote_page, HANDLE process, bool trackPageChanges) {
		this->page_address = remote_page;
		this->trackPageChanges = trackPageChanges;		
		this->dirty = false;
		this->trackPageChanges = trackPageChanges;		
		this->original_protect = 0;
	}

	int PageBackupEx::backup(HANDLE process) {
		SIZE_T bytesRead = 0;
		BOOL success = 1;
		DWORD page_guard = 0;
		DWORD old_protect = 0;
		DWORD old_protect2;
		MEMORY_BASIC_INFORMATION page_info;
		VirtualQueryEx(process, this->page_address, &page_info, sizeof(page_info));
		return this->backup(process, &page_info);
	}

	int PageBackupEx::backup(HANDLE process, PMEMORY_BASIC_INFORMATION memInfo)	{

		SIZE_T bytesRead = 0;
		BOOL success = 1;
		DWORD page_guard = 0;
		DWORD old_protect = 0;
		DWORD old_protect2;
		this->page_info = *memInfo;
		this->original_protect = this->page_info.Protect;
		
		//
		// If the page isn't committed, we can't back it up.
		//
		if (this->page_info.State != MEM_COMMIT) {
			printf("Attempt to backup non-committed page\n");
			return 0;
		}
		else if (this->page_info.Protect == PAGE_NOACCESS) {
			printf("Attempt to backup no access page %p\n", this->page_info.BaseAddress);
			return 0;
		}


		//
		// If a backup already exists, free it.
		//
		if (this->data != nullptr) {
			free(this->data);
		}
		this->data = malloc(this->page_info.RegionSize);

		//
		// If the page is page guarded, we can't read it.  So we'll have to clear the page guard protection, 
		// read the page, and then restore the page guard.
		//
		page_guard = this->page_info.Protect & PAGE_GUARD;
		if (page_guard || this->page_info.Protect == PAGE_NOACCESS) {
			success = VirtualProtectEx(process, this->page_address, this->page_info.RegionSize, PAGE_READONLY & ~page_guard, &old_protect);
			if (!success) {
				throw VirtualProtectFailedException();
			}
		}

		success = ReadProcessMemory(process,
			this->page_info.BaseAddress,
			this->data,
			this->page_info.RegionSize,
			&bytesRead);

		if (success == false || bytesRead != this->page_info.RegionSize) {
			throw ReadProcessMemoryFailedException();
		}

		if (page_guard || this->page_info.Protect == PAGE_NOACCESS) {
			success = VirtualProtectEx(process, this->page_address, this->page_info.RegionSize, old_protect, &old_protect2);
			if (!success) {
				throw VirtualProtectFailedException();
			}
		}

		//
		// If we're to track changes to pages, write-protect the page.
		// WARNING: write-protected changes will cause calls to WriteProcessMemory
		// to fail on the page, which may break things that rely on this.
		//
		if (this->trackPageChanges) {
			this->ProtectPage(process);
		}

		return 0;
	}

int PageBackupEx::ProtectPage(HANDLE process) {
	DWORD oldProtectNoPG;
	MEMORY_BASIC_INFORMATION freshInfo;
	SIZE_T bytesWritten = 0;
	DWORD oldProtect = 0;
	DWORD oldOldProtect = 0;
	DWORD newProtect = 0;
	DWORD page_guard;
	int error = 0;
	int result = 0;
	LPVOID allocResult;
	bool doRestore = true;

	result = VirtualQueryEx(process, this->page_address, &freshInfo, sizeof(freshInfo));
	//
	// We don't want to do the whole region, just this page.
	//
	freshInfo.RegionSize = this->page_info.RegionSize;
	if (!result) {
		throw VirtualQueryFailedException();
	}

	oldProtect = freshInfo.Protect;
	oldProtectNoPG = oldProtect & (~PAGE_GUARD);
	page_guard = oldProtect & PAGE_GUARD;

	switch (oldProtectNoPG) {
	case (PAGE_EXECUTE_READ):
	case (PAGE_EXECUTE_READWRITE):
		newProtect = PAGE_EXECUTE_READ;
		break;
	case (PAGE_READONLY):
	case (PAGE_READWRITE):
		newProtect = PAGE_READONLY;
		break;
	case (PAGE_EXECUTE):
		newProtect = oldProtect;
		break;
	case (PAGE_WRITECOPY):
		doRestore = false;
		newProtect = PAGE_WRITECOPY;
		break;
	default:
		printf("Unknown protection constant 0x%x", oldProtect);
		throw new UnknownProtectionException();
		break;
	}

	newProtect |= page_guard;
	if (this->page_info.Type == MEM_MAPPED) {
		//
		// VirtualProtect can fail on mapped views of files.
		// Due to this, we unmap the file and fill the space it
		// held with a copy of the data the view contained.
		//
		//this->dirty = true;
		/*
		SIZE_T backup_size = freshInfo.RegionSize;
		BYTE* backup = (BYTE*)malloc(backup_size);
		SIZE_T bytesWritten = 0;
		result = ReadProcessMemory(process, freshInfo.BaseAddress, backup, backup_size, &bytesWritten);
		if (!result || bytesWritten != backup_size) {
			throw std::exception();
		}

		bool unmapResult = UnmapViewOfFile2(process, freshInfo.BaseAddress, NULL);
		if (!unmapResult) {
			throw std::exception();
		}
		LPVOID allocResult = VirtualAllocEx(process, freshInfo.BaseAddress, freshInfo.RegionSize, MEM_COMMIT, newProtect);
		if (allocResult == nullptr) {
			throw std::exception();
		}
		result = WriteProcessMemory(process, allocResult, backup, backup_size, &bytesWritten);
		if (!result || bytesWritten != backup_size) {
			throw std::exception();
		}
		free(backup);
		*/
	}

	if (newProtect != oldProtect) {
		result = VirtualProtectEx(process, freshInfo.BaseAddress, freshInfo.RegionSize - 1, newProtect, &oldOldProtect);
		if (!result) {
			//
			// In testing, there has been one and only one region in the 
			// target process that isn't mapped to a view of a file that
			// VirtualProtectEx fails on.  I have a suspicion this is the 
			// PEB or something like that.  Mark it as dirty without 
			// adjusting the protection so that it's restored to its 
			// previous state on restore.
			// 				
			this->dirty = true;
		}
	}

	return result;
	}

	int PageBackupEx::resize(HANDLE process) {
		MEMORY_BASIC_INFORMATION freshInfo;
		int result;
		PVOID allocResult;
		DWORD new_state;
		result = VirtualQueryEx(process, this->page_address, &freshInfo, sizeof(freshInfo));
		if (!result) {
			throw VirtualQueryFailedException();
		}
		// If the size of the page has changed, we need to re-allocate it
		if (freshInfo.RegionSize != this->page_info.RegionSize) {
			// Untested, test when reached			
			result = VirtualFreeEx(process, freshInfo.BaseAddress, 0, MEM_RELEASE);
			if (!result) {
				throw VirtualFreeFailedException();
			}
			if (this->page_info.State == MEM_COMMIT) {
				new_state = MEM_COMMIT | MEM_RESERVE;
			} else {
				new_state = this->page_info.State;
}
			allocResult = VirtualAllocEx(process,
				this->page_address,
				this->page_info.RegionSize,
				new_state,
				this->page_info.Protect);
			if (!allocResult) {
				throw VirtualAllocFailedException();
			}
		}
		return 1;
	}

	int PageBackupEx::restore(HANDLE process) {
		SIZE_T bytesWritten = 0;
		bool result;
		if (this->dirty) {
			//this->resize(process);
			this->dirty = false;
			// need to resize first
			result = WriteProcessMemory(process,
				this->page_address,
				this->data,
				this->page_info.RegionSize,
				&bytesWritten);
			this->ProtectPage(process);
			if (!result || bytesWritten != this->page_info.RegionSize) {
				throw WriteProcessMemoryFailedException();
			}
		}
		return bytesWritten > 0;		
	}

	void PageBackupEx::mark_dirty(HANDLE process) {
		DWORD old_protect = 0;
		this->dirty = true;
		bool result = VirtualProtectEx(process, 
			this->page_info.BaseAddress, 
			this->page_info.RegionSize, 
			this->original_protect, 
			&old_protect);
		if (!result) {
			throw VirtualProtectFailedException();
		}
	}
}
