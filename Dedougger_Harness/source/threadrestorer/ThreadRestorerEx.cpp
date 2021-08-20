#include "ThreadRestorerEx.hpp"


namespace dedougger {

	ThreadRestorerEx::ThreadRestorerEx(DWORD process_id) {
		this->process_id = process_id;
		this->processHandle = OpenProcess(PROCESS_ALL_ACCESS, false, process_id);

		if (this->processHandle == INVALID_HANDLE_VALUE) {
			throw OpenProcessFailedException();
		}
	}


	/* Saves the state of all threads in the target process 
		Returns:
			Number of threads found/saved
	 */
	int ThreadRestorerEx::save_state()
	{
		printf("Saving thread state\n");
		int threads_saved = 0;
		std::vector<DWORD> process_threads;
		HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		//
		// Walk all threads on the system and add threads belonging to the target
		// process to the process_threads list
		//
		if (h != INVALID_HANDLE_VALUE) {
			THREADENTRY32 thread_entry = { 0 };
			thread_entry.dwSize = sizeof(thread_entry);
			if (Thread32First(h, &thread_entry)) {
				do {
					if (thread_entry.th32OwnerProcessID == process_id) {
						process_threads.push_back(thread_entry.th32ThreadID);
					}
				} while (Thread32Next(h, &thread_entry));
			}
		}
		//
		// Save the state of each thread belonging to the target process
		//
		for (int i = 0; i < process_threads.size(); i++) {			
			if (this->save_thread(process_threads[i])) {
				threads_saved++;
			}
		}
		return threads_saved;
	}

	/* Kills all threads not 'saved'/tracked by the ThreadRestorer in the target process
		Returns:
			The number of theads terminated
	 */
	int ThreadRestorerEx::kill_threads() {
		int killed_threads = 0;
		for (int i = 0; i < this->threads_to_kill.size(); i++) {
			HANDLE threadHandle = this->threads_to_kill[i];
			TerminateThread(this->threads_to_kill[i], 0);
			CloseHandle(threadHandle);
			killed_threads++;
		}
		this->threads_to_kill.clear();
		return killed_threads;
	}

	/* Restores the state of all threads saved/tracked by the ThreadRestorer in the target process
		Returns:
			The number of threads restored
	 */
	int ThreadRestorerEx::restore_state() {
		int threads_restored = 0;		
		this->kill_threads();
	
		for (auto it = this->threads.begin(); it != this->threads.end(); it++) {
			it->second->restore(this->processHandle);
			threads_restored++;
		}
		
		return threads_restored;
	}

	/* Saves the state of a single thread and tracks it
		Args:
			thread_id - ID of the target thread
		Returns:
			Non-zero on success, zero on failure
	 */
	int ThreadRestorerEx::save_thread(DWORD thread_id) {
		ThreadBackupEx* thread;
		int result;

		auto existingThread = this->threads.find(thread_id);
		if (existingThread != this->threads.end()) {
			thread= this->threads.at(thread_id);
		}
		else {
			thread = new ThreadBackupEx(thread_id);
			this->threads[thread_id] = thread;
		}
		result = thread->backup();
		return result;
	}

	/* Restores a thread to its saved state if tracked by the ThreadRestorer
		Args:
			thread_id - ID of the target thread
		Returns:
			Non-zero on success, zero on failure
	 */
	int ThreadRestorerEx::restore_thread(DWORD thread_id) {
		ThreadBackupEx* thread;
		int result = 0;
		auto existingThread = this->threads.find(thread_id);
		if (existingThread != this->threads.end()) {
			thread = this->threads.at(thread_id);
			thread->restore(this->processHandle);			
			result = 1;
		}
		else {
			//
			// Attempt to restore a thread that we aren't tracking.  
			// This means  the thread was created *after* our snapshot,
			// and should be yeeted.  This should be done by the calling 
			// function, so we'll throw an exception here.
			//
			throw AttemptToRestoreUnTrackedThread();
		}
		return result;
	}

	/* Terminates an individual thread
		Args:
			thread_id - ID of the target thread
		Returns:
			Non-zero on success, zero on failure
	 */
	int ThreadRestorerEx::kill_thread(DWORD thread_id) {
		int result = 0;
		HANDLE thread_handle = OpenThread(THREAD_TERMINATE, false, thread_id);
		if (thread_handle != INVALID_HANDLE_VALUE) {
			TerminateThread(thread_handle, 0);
			result = 1;			
		} else {
			throw OpenThreadFailedException();
		}
		return result;
	}
}
