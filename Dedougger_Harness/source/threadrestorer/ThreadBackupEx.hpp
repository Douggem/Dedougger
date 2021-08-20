#pragma once
#include <Windows.h>
#include <processthreadsapi.h>
#include <stdio.h>
#include "dexception.h"

namespace dedougger {
	class ThreadBackupEx {
		DWORD thread_id;
		HANDLE thread_handle;
		CONTEXT context;
	public:
		ThreadBackupEx(DWORD remote_thread_id);
		int restore(HANDLE processHandle);
		int backup();
	};
}