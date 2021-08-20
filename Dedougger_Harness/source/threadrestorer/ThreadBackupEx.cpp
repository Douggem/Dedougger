#include "ThreadBackupEx.hpp"
namespace dedougger {
	ThreadBackupEx::ThreadBackupEx(DWORD remote_thread_id)	{
		this->thread_id = remote_thread_id;
		//
		// Maintain our thread handle in case one is killed during the iteration		
		//
		this->thread_handle = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, false, this->thread_id);
		if (this->thread_handle == NULL) {
			printf("ThreadBackup construction failed for thread ID %p", remote_thread_id);
		}
		this->context.ContextFlags = CONTEXT_ALL;		
	}

	int ThreadBackupEx::backup() {		
		bool result = GetThreadContext(this->thread_handle, &this->context);
		if (!result) {
			printf("Backup failed for handle %d", this->thread_handle);
		}
		else {
			
		}
		return result;
	}

	int ThreadBackupEx::restore(HANDLE processHandle) {
		bool result = SetThreadContext(this->thread_handle, &this->context);
		if (!result) {
			//
			// If the thread has been yeeted, we need to re-create it.
			//
			DWORD newThreadId;
			HANDLE newHandle;
			newHandle = CreateRemoteThreadEx(processHandle, NULL,  NULL, (LPTHREAD_START_ROUTINE)this->context.Rip, NULL, NULL, NULL, &newThreadId);
			this->thread_handle = newHandle;
			this->thread_id = newThreadId;
			result = SetThreadContext(newHandle, &this->context);
			if (!result) {
				throw SetThreadContextFailedException();
			}
		}		
		return result;
	}
}
