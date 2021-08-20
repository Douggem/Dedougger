#pragma once
#include <vector>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <Windows.h>
#include <TlHelp32.h>
#include "dexception.h"
#include "ThreadBackupEx.hpp"

namespace dedougger {
	class ThreadRestorerEx {
		std::map<DWORD, ThreadBackupEx*> threads;		
		std::map<DWORD, HANDLE> threads_to_kill;
		DWORD process_id;
		HANDLE processHandle;
		int kill_thread(DWORD thread_id);
		int save_thread(DWORD thread_id);
		int restore_thread(DWORD thread_id);
	public:
		ThreadRestorerEx(DWORD process_id);
		int restore_state();
		int save_state();
		void add_thread_to_kill(DWORD threadId, HANDLE threadHandle) { this->threads_to_kill[threadId] = threadHandle; }
		void remove_thread_from_kill(DWORD threadId) { this->threads_to_kill.erase(threadId); }
		int kill_threads();
	};

	typedef std::unique_ptr<ThreadRestorerEx> UP_ThreadRestorerEx;
	typedef std::shared_ptr<ThreadRestorerEx> SP_ThreadRestorerEx;
}