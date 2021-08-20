#pragma once
#include "stdafx.h"
#include <map>
#include "mempagebackup.h"

namespace duzz {

	class PageRestore {
	public:
		void restore_page(LPVOID block_address);
		void backup_page(LPVOID block_address);
	protected:
		//std::map<LPVOID, MemPageBackup> blocks;
	};
}