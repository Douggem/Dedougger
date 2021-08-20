#pragma once
#include "stdafx.h"
#include "pagerestore.h"

namespace duzz {
	void PageRestore::restore_page(LPVOID block_address) {

	}

	void PageRestore::backup_page(LPVOID block_address) {
		MemPageBackup pageBackup((PMEMORY_BASIC_INFORMATION)nullptr);
		//this->blocks[block_address] = pageBackup;
		
	}

}
