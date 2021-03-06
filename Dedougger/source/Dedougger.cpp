// Dedougger.cpp : Defines the entry point for the console application.
//


#include <Windows.h>
#include <processthreadsapi.h>
#include <time.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <dbghelp.h>
#include <debugapi.h>
#include <string>
#include "dedougger.hpp"
#include "dexception.h"

#pragma comment(lib, "DbgHelp.lib")

namespace dedougger {

	enum ACCESS_VIOLATION_FLAGS {
		READ_VIOLATION = 0,
		WRITE_VIOLATION = 1,
		EXECUTION_VIOLATION = 8
	};

	Dedougger::Dedougger(const TCHAR* exec_path) {
		/* Constructor that starts a new process for the executable passed
		 *	Args:
		 *		TCHAR* exec_path - binary to start up and attach to		
		 */
		this->CommonInit();
		//
		// This constructor starts the target process itself instead of attaching
		// to an already running process
		//
		STARTUPINFO startupInfo = { 0 };
		PROCESS_INFORMATION procInfo = { 0 };
		bool success = CreateProcess(exec_path, NULL, NULL, NULL, TRUE, DEBUG_PROCESS | CREATE_SUSPENDED, NULL, NULL, &startupInfo, &procInfo);
		this->processHandle = OpenProcess(PROCESS_ALL_ACCESS, false, procInfo.dwProcessId);
		this->processId = procInfo.dwProcessId;
		printf("Debugging process with pid %d 0x%x\n", this->processId, this->processId);
		//
		// Begin executing the process.  It will hang on process start
		// debug event so it won't *actually* start running until we 
		// start debugging.
		//				
		this->startThreadHandle = procInfo.hThread;

		return;
	}

	Dedougger::Dedougger(DWORD pid) {
		/* Constructor that attaches to an already running process
		 *	Args:
		 *		DWORD pid - the process ID of the process to attach to
		 */
		bool success;
		this->CommonInit();
		this->startThreadHandle = INVALID_HANDLE_VALUE;
		//
		// This constructor attaches to an already running process
		//
		this->processId = pid;
		this->processHandle = OpenProcess(PROCESS_ALL_ACCESS, false, this->processId);
		if (this->processHandle == INVALID_HANDLE_VALUE) {
			throw OpenProcessFailedException();
		}

		success = DebugActiveProcess(this->processId);
		if (!success) {
			throw DebugActiveProcessFailedException();
		}

		return;
	}

	Dedougger::~Dedougger() {
		DebugActiveProcessStop(this->processId);
	}

	void Dedougger::CommonInit() {
		
		 
		this->firstBreakpointHit = false;
		//
		// Null out the callback array
		//
		this->eventCallbacks.fill(EventCallbackObjectPair(nullptr, nullptr));
	}

	/* Starts debugging the target process
	 *	Remarks:
	 *		Does not return.  Spins in a loop catching debug events, handling exceptions 
	 *		(such as breakpoints, etc.) and dispatches callback routines.
	 */
	void Dedougger::BeginDebugging() {
		int result;
		DWORD dwContinueStatus = DBG_CONTINUE; // exception continuation 				
		DEBUG_EVENT debugEv = { 0 };

		//
		// If we started the process ourselves, we'll have a thread handle we'll have to resume to
		// actually spin up the target process.
		//
		if (this->startThreadHandle != INVALID_HANDLE_VALUE) {
			ResumeThread(this->startThreadHandle);
		}

		while (true) {
			bool success = WaitForDebugEvent(&debugEv, INFINITE);
			if (!success) {
				printf("WaitForDebugEvent time out, breaking target\n");
				
				continue;
			}
			else {
				dwContinueStatus = this->HandleDebugEvent(&debugEv);
				bool continued = ContinueDebugEvent(debugEv.dwProcessId,
					debugEv.dwThreadId,
					dwContinueStatus);
				if (!continued) {
					printf("Continue debug event failed\n");
				}				
			}
		}
	}

	/* Forces a debug break in the target process.  
	 *	Returns:
	 *		true if break was successful, false if not
	 *	Remarks:
	 *		Keep in mind that the breakpoint will trigger an exception to be handled in
	 *		BeginDebugging, and will be handled like normal, so make sure you have a 
	 *		BREAKPOINT_CALLBACK registered if you want to catch this.
	 */
	bool Dedougger::BreakProcess() 	{
		bool broke = DebugBreakProcess(this->processHandle);
		if (!broke) {
			printf("Failed to break target process, %x\n", GetLastError());
		}
		return broke;
	}

	/* Writes hardware breakpoints to the thread represented by threadState
	 *	Args:
	 *		threadState 
	 *	Remarks:
	 *		This will write breakpoints as described in this->hwbps
	 */
	void Dedougger::WriteBreakpointsToThread(ThreadState *threadState) {
		Dr7_Fields dr7 = this->hwbpState.Dr7();		
		dr7.fields.GE = 0;
		dr7.fields.LE = 0;
		dr7.fields.bit10 = 1;		

		threadState->SetRegisterValue(CONTEXTREGISTER::DR0, this->hwbpState.Dr0());
		threadState->SetRegisterValue(CONTEXTREGISTER::DR1, this->hwbpState.Dr1());
		threadState->SetRegisterValue(CONTEXTREGISTER::DR2, this->hwbpState.Dr2());
		threadState->SetRegisterValue(CONTEXTREGISTER::DR3, this->hwbpState.Dr3());
		threadState->SetRegisterValue(CONTEXTREGISTER::DR7, dr7.int32);		
		
		printf("[d] Wrote HWBPs to thread ID %x\n", threadState->GetThreadId());
	}

	/* Enumerates all threads and applies hardware breakpoints to each one
	 *	Remarks:
	 *		This should only be used on the initial breakpoint.  Enumerating all threads
	 *		is very expensive, and is only necessary if we don't know the threads of our
	 *		target process.  This is only appropriate right after we attach to a process
	 */
	void Dedougger::ApplyHWBPs() {
		HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, this->processId);
		DWORD mainThreadId;
		//
		// Walk all threads on the system and add threads belonging to the target
		// process to the process_threads list
		//
		if (h != INVALID_HANDLE_VALUE) {
			THREADENTRY32 thread_entry = { 0 };
			thread_entry.dwSize = sizeof(thread_entry);
			if (Thread32First(h, &thread_entry)) {
				do {
					if (thread_entry.th32OwnerProcessID == this->processId) {						
						this->threads[thread_entry.th32ThreadID] = thread_entry.th32ThreadID;
						ThreadState threadEntryState(thread_entry.th32ThreadID);
						this->WriteBreakpointsToThread(&threadEntryState);
					}
				} while (Thread32Next(h, &thread_entry));
			}
		}
	}

	void Dedougger::ResolveDeferredBps(const char* moduleName, size_t moduleeBaseAddress) 	{
		auto callback = this->resolvedBpCallback.callback;
		auto callbackObject = this->resolvedBpCallback.object;
		for (auto bp : this->queued_deferred_hwbps) {
			if (!_stricmp(moduleName, bp.module_name)) {
				size_t bpAddress = bp.resolve((size_t)moduleeBaseAddress);
				this->SetHWBP(bpAddress, bp.hwbp_descriptor.condition, bp.hwbp_descriptor.len);
				printf("Resolved hwbp at %p\n", bpAddress);
				if (callback != nullptr) {
					callback(moduleName, bp.offset, bpAddress, callbackObject);
				}
			}
		}
		for (auto bp : this->queued_deferred_swbps) {
			if (!_stricmp(moduleName, bp.module_name)) {
				size_t bpAddress = bp.resolve((void*)moduleeBaseAddress);
				this->SetSWBP(bpAddress);
				printf("Resolved swbp at %p\n", bpAddress);
				if (callback != nullptr) {
					callback(moduleName, bp.offset, bpAddress, callbackObject);
				}
			}
		}
	}

	int Dedougger::SetHWBP(size_t address, BPCONDITION condition, BPLEN len) {
		int bp = -1; // -1 to be interpreted as an invalid hwbp
		// Find an unused HWBP
		for (int i = 0; i < 4; i++) {
			if (this->breakpoints[i].enabled == false) {
				bp = i;
				break;
			}
		}
		if (bp == -1) {
			throw std::exception();
		}
		this->breakpoints[bp].condition = condition;
		this->breakpoints[bp].address = address;
		this->breakpoints[bp].len = len;
		this->breakpoints[bp].enabled = true;
		this->hwbpState.WriteHWBPToState(&this->breakpoints[bp], bp);		
		return bp;
	}

	int Dedougger::SetSWBP(const std::string &moduleName, const std::string &functionName, _Out_opt_ size_t *resolvedAddress, bool replacePageProtection, bool replaceInstOnBPHit) {
		size_t funcAddress;
		SP_ExportedFunction function = this->ResolveFunction(moduleName, functionName);
		funcAddress = function->GetFunctionAddress();
		if (resolvedAddress) {
			*resolvedAddress = funcAddress;
		}
		return this->SetSWBP(funcAddress, replacePageProtection, replaceInstOnBPHit);
	}

	int Dedougger::SetHWBP(const std::string &moduleName, const std::string &functionName, _Out_opt_ size_t *resolvedAddress, BPCONDITION condition, BPLEN len) 	{
		size_t funcAddress;
		SP_ExportedFunction function = this->ResolveFunction(moduleName, functionName);
		funcAddress = function->GetFunctionAddress();
		if (resolvedAddress) {
			*resolvedAddress = funcAddress;
		}
		return this->SetHWBP(funcAddress, condition, len);
	}

	int Dedougger::SetSWBP(size_t address, bool replacePageProtection, bool replaceInstOnBPHit) {
		/* Sets a software breakpoint at address

			Args:
				address
			Return Value:
				ERROR_SUCCESS on success, nonzero on error
		*/

		SIZE_T	bytesRead;
		SIZE_T	bytesWritten;
		bool	rpmResult;
		bool	wpmResult;
		bool	virtualProtectResult;
		uint8_t originalByte;
		uint8_t bpByte = 0xCC;
		DWORD	originalProtect;
		int		result = ERROR_SUCCESS;
		
		//
		// First things first we need to get the original byte of code we're going to overwrite with the breakpoint.  We will need
		// to replace the byte when the breakpoint fires, so we have to retrieve and store it.
		//
		rpmResult = ReadProcessMemory(this->processHandle, (LPVOID)address, &originalByte, 1, &bytesRead);
		//
		// If ReadProcessMemory fails, we can't continue regardless of what the reason is.  It likely means we're trying
		// to breakpoint an invalid area of memory, or the area is unreadable.
		//
		if (!rpmResult) {
			result = GetLastError();
			goto EXIT;
		}
		//
		// To write the breakpoint byte, we have to ensure that the target memory page is writeable.
		//
		virtualProtectResult = VirtualProtectEx(this->processHandle, (LPVOID)address, sizeof(bpByte), PAGE_READWRITE, &originalProtect);
		
		if (!virtualProtectResult) {
			result = GetLastError();
			goto EXIT;
		}
		//
		// Overwrite the instruction with 0xCC because that's what a breakpoint is
		//
		wpmResult = WriteProcessMemory(this->processHandle, (LPVOID)address, &bpByte, sizeof(bpByte), &bytesWritten);
		
		if (!wpmResult) {
			result = GetLastError();
			goto EXIT;
		}
		//
		// We give the user the option to not re-protect the page the BP is on.  This is just to give them the option to 
		// enhance performance a little bit at the cost of correct-ness if a BP is expected to be hit frequently.  You might
		// think the user would be better off using a HWBP in this case - and you'd be correct.  But we're limited to 4 HWBP
		// and they might all be used for things like memory accesses, so we have this ghetto hack optionjust in ase.
		//
		if (replacePageProtection) {
			virtualProtectResult = VirtualProtectEx(this->processHandle, (LPVOID)address, sizeof(bpByte), originalProtect, &originalProtect);
			//
			// If virtual protect fails at this point, something is very fucky.  But we still can't silently fail.
			//
			if (!virtualProtectResult) {
				result = GetLastError();
				goto EXIT;
			}
		}
		//
		// At this point the breakpoint is successfully written.  We need to add the info to our BP list, which is a map keyed
		// by address with the original byte as the value.
		//
		{
			SWBP breakpoint((size_t)address, originalByte, replacePageProtection, replaceInstOnBPHit);
			this->swbps[(SIZE_T)address] = breakpoint;
		}
		
	EXIT:
		return result;
	}

	int Dedougger::ClearSWBP(size_t address) {
		/* Clears a software breakpoint at address
			
			Args:
				address
			Return Value:
				ERROR_SUCCESS on success, nonzero on error		
		*/

		SIZE_T	bytesRead;
		SIZE_T	bytesWritten;
		DWORD	originalProtect;
		int		result = ERROR_SUCCESS;
		bool	rpmResult;
		bool	wpmResult;
		bool	virtualProtectResult;
		uint8_t originalByte;
		//
		// Does the breakpoint actually exist?
		//
		SWBP const * breakpoint = nullptr;
		auto foundBp = this->swbps.find((size_t)address);
		if (foundBp == this->swbps.end()) {
			result = -1;
			goto EXIT;
		}
		breakpoint = &foundBp->second;
		originalByte = breakpoint->OverwrittenByte();		

		//
		// Removing a breakpoint is the inverse of adding one.  Make the page writeable, write the original byte, reset the page
		// permissions.
		//
		virtualProtectResult = VirtualProtectEx(this->processHandle, (LPVOID)address, sizeof(originalByte), PAGE_READWRITE, &originalProtect);

		if (!virtualProtectResult) {
			result = GetLastError();
			goto EXIT;
		}
		//
		// Restore the original byte
		//
		wpmResult = WriteProcessMemory(this->processHandle, (LPVOID)address, &originalByte, sizeof(originalByte), &bytesWritten);

		if (!wpmResult) {
			result = GetLastError();
			goto EXIT;
		}

		virtualProtectResult = VirtualProtectEx(this->processHandle, (LPVOID)address, sizeof(originalByte), originalProtect, &originalProtect);
		//
		// If virtual protect fails at this point, something is very fucky.  But we still can't silently fail.
		//
		if (!virtualProtectResult) {
			result = GetLastError();
			goto EXIT;
		}
		this->swbps.erase((SIZE_T)address);
	EXIT:
		return result;
	}


	void Dedougger::ClearHWBP(int bpIndex) {
		if (bpIndex < 0 || bpIndex > 3) {
			// invalid BP index
			throw InvalidHWBPIndexException();
		}
		this->breakpoints[bpIndex].enabled = false;
		this->hwbpState.DisableHWBP(bpIndex);
		for (auto thread : this->threads) {
			ThreadState currentThread(thread.second);
			currentThread.DisableHWBPByIndex(bpIndex);
			currentThread.FlushContext();
		}
	}

	int Dedougger::ClearHWBPByAddress(size_t address) 	{
		for (int i = 0; i < 4; i++) {
			if (this->breakpoints->address == address) {
				this->ClearHWBP(i);
				return i;
			}
		}
		return -1;
	}

	void Dedougger::SetHWBPInModule(const char* module_name, SIZE_T offset, BPCONDITION condition, BPLEN len) {
		DeferredHWBP bp(module_name, offset, condition, len);
		this->queued_deferred_hwbps.push_back(bp);
		//
		// The module may already be loaded
		//
		auto foundModule = this->modulesByName.find(module_name);
		if ( foundModule != this->modulesByName.end()) {
			this->ResolveDeferredBps(module_name, foundModule->second.GetModuleBaseAddress());
		}
	}

	void Dedougger::SetSWBPInModule(const char* module_name, SIZE_T offset) {
		DeferredSWBP bp(module_name, offset);
		this->queued_deferred_swbps.push_back(bp);
		//
		// The module may already be loaded
		//
		auto foundModule = this->modulesByName.find(module_name);
		if (foundModule != this->modulesByName.end()) {
			this->ResolveDeferredBps(module_name, foundModule->second.GetModuleBaseAddress());
		}
	}

	std::vector<STACKFRAME64> Dedougger::GetCallStack(HANDLE thread, ThreadState *threadState) {
		std::vector<STACKFRAME64> result;
		CONTEXT contextRecord = threadState->GetContextCopy();
		STACKFRAME64 stackFrame = { 0 };	
		stackFrame.AddrPC.Offset = contextRecord.Rip;
		stackFrame.AddrPC.Mode = AddrModeFlat;
		stackFrame.AddrStack.Offset = contextRecord.Rsp;
		stackFrame.AddrStack.Mode = AddrModeFlat;
		stackFrame.AddrFrame.Offset = contextRecord.Rbp;
		stackFrame.AddrFrame.Mode = AddrModeFlat;

		while (StackWalk(IMAGE_FILE_MACHINE_AMD64,
			this->processHandle,
			thread,
			&stackFrame,
			&contextRecord,
			NULL,
			SymFunctionTableAccess64,
			SymGetModuleBase64,
			NULL)) {
			//printf("Return address: %p\n", stackFrame.AddrReturn.Offset);
			result.push_back(stackFrame);
		}
		//printf("%x\n", GetLastError());
		return result;
	}



	DWORD Dedougger::ResumeFromBreakpoint(const DEBUG_EVENT *debugEv, ThreadState* threadState, bool replaceBreakpoint) {
		//
		// Is this a software breakpoint or hardware breakpoint?  
		//
		DEBUG_EVENT stepDebugEv;
		SWBP const * breakpoint = nullptr;
		bool virtualProtectResult;
		bool wpmResult;
		uint8_t originalByte;
		uint8_t bpByte = 0xCC;
		DWORD continueStatus;		
		DWORD originalProtect;
		SIZE_T bytesWritten;
		SIZE_T rip = threadState->GetRegisterValue(CONTEXTREGISTER::RIP);
		
		rip -= 1;
		threadState->SetRegisterValue(CONTEXTREGISTER::RIP, rip);
		auto foundSWBP = this->swbps.find((size_t)rip);
		
		if (foundSWBP != this->swbps.end()) {
			breakpoint = &foundSWBP->second;
			if (breakpoint->ReplaceInst()) {
				//
				// Remove the breakpoint by putting the original byte back in place
				//
				originalByte = breakpoint->OverwrittenByte();
				virtualProtectResult = VirtualProtect((LPVOID)rip, sizeof(originalByte), PAGE_READWRITE, &originalProtect);
				wpmResult = WriteProcessMemory(this->processHandle, (LPVOID)rip, &originalByte, sizeof(originalByte), &bytesWritten);
				//
				// Adjust RIP so we execute the instruction we breakpointed
				//
				threadState->FlushContext();				
				threadState->Invalidate();
				if (replaceBreakpoint) {
					//
					// single step
					//
					DWORD eFlags = (DWORD)threadState->GetRegisterValue(CONTEXTREGISTER::EFLAGS);
					eFlags |= 0x100;
					threadState->SetRegisterValue(CONTEXTREGISTER::EFLAGS, eFlags);
					threadState->FlushContext();
					ContinueDebugEvent(debugEv->dwProcessId, debugEv->dwThreadId, DBG_CONTINUE);
					bool success = WaitForDebugEvent(&stepDebugEv, INFINITE);
					assert(stepDebugEv.dwDebugEventCode == EXCEPTION_DEBUG_EVENT);
					assert(stepDebugEv.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_SINGLE_STEP);
					threadState->Invalidate();
					eFlags = (DWORD)threadState->GetRegisterValue(CONTEXTREGISTER::EFLAGS);
					eFlags &= ~(0x100);
					eFlags |= 0x1000; // resume flag
					threadState->SetRegisterValue(CONTEXTREGISTER::EFLAGS, eFlags);
					threadState->FlushContext();
					threadState->Invalidate();
					//
					// Now that we've stepped out of the BP'd instruction, replace the BP
					//
					wpmResult = WriteProcessMemory(this->processHandle, (LPVOID)rip, &bpByte, sizeof(bpByte), &bytesWritten);
				}
				if (breakpoint->ProtectPage()) {
					//
					// Restore the original page protections
					//
					virtualProtectResult = VirtualProtect((LPVOID)rip, sizeof(originalByte), originalProtect, &originalProtect);
				}
			}
			
			continueStatus = DBG_EXCEPTION_HANDLED;
		} else {
			// TODO: resolve HWBPs.  Disabled for now.
			continueStatus = DBG_CONTINUE;
		}
		return continueStatus;
	}

	void Dedougger::MapDll(ModuleInfo newDll) {
		this->modulesByName[newDll.GetModuleName()] = newDll;
		this->modulesByAddress[newDll.GetModuleBaseAddress()] = newDll;
	}

	const ModuleInfo *Dedougger::ResolveModule(std::string moduleName) const {
		auto module = this->modulesByName.find(moduleName);
		if (module == this->modulesByName.end()) {
			throw(ModuleNotFoundException());
		}
		return &module->second;
	}

	SP_ExportedFunction Dedougger::ResolveFunction(const std::string &moduleName, const std::string &functionName) const {
		const ModuleInfo *module = this->ResolveModule(moduleName);
		auto peInfo = module->GetPEInfo();
		SP_ExportedFunction function = peInfo->GetExportedFunction(functionName);
		return function;
	}

	DBG_CONTINUE_STATUS Dedougger::HandleBreakpointEvent(ThreadState * threadState, const DEBUG_EVENT * debugEv)
	{
		static size_t entryPointAddress;

		DBG_CONTINUE_STATUS dwContinueStatus = DBG_CONTINUE;		
		if (this->firstBreakpointHit) {
			size_t address = (size_t)debugEv->u.Exception.ExceptionRecord.ExceptionAddress;
			if (address == entryPointAddress) {
				ThreadState threadEntryState(debugEv->dwThreadId);
				this->WriteBreakpointsToThread(&threadEntryState);
			}
			CALLBACKRESULT bpCallbackResult = BP_HANDLE;
			EventCallbackObjectPair event = this->eventCallbacks[BREAKPOINT_CALLBACK];
			if (event.callback) {
				bpCallbackResult = event.callback(BREAKPOINT_CALLBACK,
					threadState, debugEv, &dwContinueStatus, event.object);
			}
			if (bpCallbackResult == BP_HANDLE) {				
				dwContinueStatus = this->ResumeFromBreakpoint(debugEv, threadState);
				this->WriteBreakpointsToThread(threadState);
			}
		}
		else {
			printf("Initial breakpoint hit, setting up.\n");
			size_t moduleBase = (size_t)this->GetMainModuleBase();
			entryPointAddress = this->mainModule.GetPEInfo()->GetEntryPointOffset() + moduleBase;
			printf("Continuing to entry point\n");
			this->SetSWBP(entryPointAddress);
			HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, this->processId);
			DWORD mainThreadId;
			//
			// Walk all threads on the system and add threads belonging to the target
			// process to the process_threads list
			//
			if (h != INVALID_HANDLE_VALUE) {
				THREADENTRY32 thread_entry = { 0 };				
				thread_entry.dwSize = sizeof(thread_entry);
				if (Thread32First(h, &thread_entry)) {
					do {
						if (thread_entry.th32OwnerProcessID == this->processId) {	
							this->threads[thread_entry.th32ThreadID] = thread_entry.th32ThreadID;
							if (thread_entry.th32ThreadID == debugEv->dwThreadId) {								
								mainThreadId = debugEv->dwThreadId;
								this->WriteBreakpointsToThread(threadState);
								this->threads[mainThreadId] = mainThreadId;
							}
							else {
								ThreadState threadEntryState(thread_entry.th32ThreadID);
								this->WriteBreakpointsToThread(&threadEntryState);
							}
						}
					} while (Thread32Next(h, &thread_entry));
				}
			}			
			EventCallbackObjectPair event = this->eventCallbacks[INITIAL_BREAKPOINT_CALLBACK];
			if (event.callback) {
				event.callback(INITIAL_BREAKPOINT_CALLBACK,
					threadState, debugEv, &dwContinueStatus, event.object);
			}
			firstBreakpointHit = true;
		}			
		return dwContinueStatus;
	}

	DBG_CONTINUE_STATUS Dedougger::HandleCreateThreadEvent(ThreadState * threadState, const DEBUG_EVENT * debugEv) {
		DBG_CONTINUE_STATUS dwContinueStatus = DBG_CONTINUE;
		DWORD thread_id = debugEv->dwThreadId;
		
		printf("Thread created - ID %X\n", thread_id);
		
		dwContinueStatus = DBG_CONTINUE;
		
		this->WriteBreakpointsToThread(threadState);
		EventCallbackObjectPair event = this->eventCallbacks[THREAD_CREATE_EVENT_CALLBACK];
		if (event.callback) {
			event.callback(THREAD_CREATE_EVENT_CALLBACK,
				threadState, debugEv, &dwContinueStatus, event.object);
		}
		//
		// Unsure if this is necessary.  The thread is handed to us by Windows without our requesting, so intuitively
		// it seems like not closing it would leak handles, but I don't know if this handle is expected to be duplicated
		// and then cleaned up by the OS.
		//
		CloseHandle(debugEv->u.CreateThread.hThread);
		return dwContinueStatus;
	}

	DBG_CONTINUE_STATUS Dedougger::HandleLoadDllEvent(ThreadState * threadState, const DEBUG_EVENT * debugEv){
		DBG_CONTINUE_STATUS dwContinueStatus = DBG_CONTINUE;
		char imageNameBuffer[256];
		SIZE_T bytesRead;
		HMODULE imageBase = (HMODULE)debugEv->u.LoadDll.lpBaseOfDll;
		HANDLE imageHandle = debugEv->u.LoadDll.hFile;
		bool handleClosed;
		if (imageHandle != NULL) {
			bytesRead = GetFinalPathNameByHandleA(imageHandle, imageNameBuffer, sizeof(imageNameBuffer), FILE_NAME_NORMALIZED);			
			if (bytesRead > 0) {
				ModuleInfo newDll((size_t)imageBase, imageNameBuffer, this->processHandle);
				this->MapDll(newDll);
				printf("Image loaded: %s\n", imageNameBuffer);
				this->ResolveDeferredBps(imageNameBuffer, (size_t)imageBase);
			}
			else {
				printf("New module loaded, but we were unable to read its name.  Error: 0x%X\n", GetLastError());
			}
			handleClosed = CloseHandle(imageHandle);			
		}
		else {
			printf("No handle to new module\n");
		}
		dwContinueStatus = DBG_CONTINUE;
		EventCallbackObjectPair event = this->eventCallbacks[LOAD_DLL_EVENT_CALLBACK];
		if (event.callback) {
			ThreadState threadState(debugEv->dwThreadId);
			event.callback(LOAD_DLL_EVENT_CALLBACK,
				&threadState, debugEv, &dwContinueStatus, event.object);
		}
		return dwContinueStatus;
	}

	DBG_CONTINUE_STATUS Dedougger::HandleExitThreadEvent(ThreadState * threadState, const DEBUG_EVENT * debugEv) {
		DBG_CONTINUE_STATUS dwContinueStatus = DBG_CONTINUE;
		printf("Thread closed: %x\n", debugEv->dwThreadId);
		dwContinueStatus = DBG_CONTINUE;
		return dwContinueStatus;
	}

	DBG_CONTINUE_STATUS Dedougger::HandleOutputDebugEvent(ThreadState * threadState, const DEBUG_EVENT * debugEv){
		DBG_CONTINUE_STATUS dwContinueStatus = DBG_CONTINUE;
		dwContinueStatus = DBG_CONTINUE;
		EventCallbackObjectPair event = this->eventCallbacks[OUTPUT_DEBUG_STRING_CALLBACK];
		if (event.callback) {
			ThreadState threadState(debugEv->dwThreadId);
			event.callback(OUTPUT_DEBUG_STRING_CALLBACK,
				&threadState, debugEv, &dwContinueStatus, event.object);
		}
		printf("[D] - %s\n", debugEv->u.DebugString.fUnicode);
		return dwContinueStatus;
	}

	DBG_CONTINUE_STATUS Dedougger::HandleCreateProcessEvent(ThreadState * threadState, const DEBUG_EVENT * debugEv){
		DBG_CONTINUE_STATUS dwContinueStatus = DBG_CONTINUE;
		dwContinueStatus = DBG_CONTINUE;
		EventCallbackObjectPair event = this->eventCallbacks[CREATE_PROCESS_EVENT_CALLBACK];
		if (event.callback) {
			ThreadState threadState(debugEv->dwThreadId);
			event.callback(CREATE_PROCESS_EVENT_CALLBACK,
				&threadState, debugEv, &dwContinueStatus, event.object);
		}
		printf("CreateProcess event handled thread ID %X\n", debugEv->dwThreadId);
		return dwContinueStatus;
	}

	DBG_CONTINUE_STATUS Dedougger::HandleExitProcessEvent(ThreadState * threadState, const DEBUG_EVENT * debugEv) {
		DBG_CONTINUE_STATUS dwContinueStatus = DBG_CONTINUE;
		EventCallbackObjectPair event = this->eventCallbacks[EXIT_PROCESS_EVENT_CALLBACK];
		if (event.callback) {
			ThreadState threadState(debugEv->dwThreadId);
			event.callback(EXIT_PROCESS_EVENT_CALLBACK,
				&threadState, debugEv, &dwContinueStatus, event.object);
		}
		printf("Exit event handled\n");
		return dwContinueStatus;
	}

	DBG_CONTINUE_STATUS Dedougger::HandleUnloadDllEvent(ThreadState * threadState, const DEBUG_EVENT * debugEv) {
		DBG_CONTINUE_STATUS dwContinueStatus = DBG_CONTINUE;
		EventCallbackObjectPair event = this->eventCallbacks[UNLOAD_DLL_EVENT_CALLBACK];
		if (event.callback) {
			ThreadState threadState(debugEv->dwThreadId);
			event.callback(UNLOAD_DLL_EVENT_CALLBACK,
				&threadState, debugEv, &dwContinueStatus, event.object);
		}
		printf("DLL Unload event handled\n");
		return dwContinueStatus;
	}

	

	void* Dedougger::GetMainModuleBase() {
		//
		// This wall fail with partial copy if the process hasn't gotten through startup yet
		// 
		char exePathMb[MAX_PATH];
		HANDLE h = INVALID_HANDLE_VALUE;
		do {
			h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, this->processId);
		} while (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_BAD_LENGTH);
		
		MODULEENTRY32 moduleEntry;
		moduleEntry.dwSize = sizeof(moduleEntry);
		void* result = (void*)0;
		size_t len = 0;
		if (h != INVALID_HANDLE_VALUE) {
			if (Module32First(h, &moduleEntry)) {
				do {
					len = wcslen(moduleEntry.szExePath);
					if (len > 4) {
						printf("Found module %S\n", moduleEntry.szExePath);
						SIZE_T numCharsConverted;
						wcstombs_s(&numCharsConverted, exePathMb, moduleEntry.szExePath, sizeof(exePathMb));													
						ModuleInfo newDll((size_t)moduleEntry.hModule, exePathMb, this->processHandle);						
						this->MapDll(newDll);
						if (!wcscmp(moduleEntry.szExePath + (len - 4), L".exe")) {
							printf("Found main module %S\n", moduleEntry.szExePath);
							this->mainModule = newDll;
							result = moduleEntry.modBaseAddr;
							char* imageFileName = strrchr(exePathMb, '\\');
							if (imageFileName == nullptr) {								
								imageFileName = exePathMb;
							}
							else {								
								imageFileName++;
							}
							this->ResolveDeferredBps(imageFileName, (size_t)result);
						}
					}
				} while (Module32Next(h, &moduleEntry));
			}
			else {
				printf("Failed to find any modules in target process %x\n", GetLastError());
			}
		}
		else {
			printf("Failed to create snapshot for module search %x\n", GetLastError());
		}
		return result;
	}

	void* Dedougger::GetModuleByName(wchar_t* module_name) {
		//
		// This waill fail with partial copy if the process hasn't gotten through startup yet
		// 
		HANDLE h = INVALID_HANDLE_VALUE;
		do {
			h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, this->processId);
		} while (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_BAD_LENGTH);

		MODULEENTRY32 moduleEntry;
		moduleEntry.dwSize = sizeof(moduleEntry);
		void* result = (void*)0;
		size_t len = 0;
		wchar_t* found_module_name;
		if (h != INVALID_HANDLE_VALUE) {
			if (Module32First(h, &moduleEntry)) {
				do {
					len = wcslen(moduleEntry.szExePath);
					if (len > 0) {
						found_module_name = wcsrchr((wchar_t*)moduleEntry.szExePath, '\\');
						if (found_module_name != nullptr) {
							printf("Found module %S\n", module_name);
							if (!_wcsicmp(found_module_name, module_name)) {
								printf("Found module %S\n", moduleEntry.szExePath);
								result = moduleEntry.modBaseAddr;
							}
						}
					}
				} while (Module32Next(h, &moduleEntry));
			}
			else {
				printf("Failed to find any modules in target process %x\n", GetLastError());
			}
		}
		else {
			printf("Failed to create snapshot for module search %x\n", GetLastError());
		}
		return result;
	}

	EventCallbackObjectPair Dedougger::RegisterEventCallback(DEBUGEVENTCALLBACKID eventId, EventCallback callback, void *object) {
		EventCallbackObjectPair old = this->eventCallbacks[eventId];
		EventCallbackObjectPair newCallbackPair(callback, object);
		this->eventCallbacks[eventId] = newCallbackPair;
		return old;
	}

	ResolvedCallbackObjectPair Dedougger::RegisterBreakpointResolvedCallback(DeferredBpResolvedCallback callback, void* object){
		ResolvedCallbackObjectPair old = this->resolvedBpCallback;
		ResolvedCallbackObjectPair newCallbackPair(callback, object);
		this->resolvedBpCallback = newCallbackPair;
		return old;
	}

	DBG_CONTINUE_STATUS Dedougger::HandleDebugEvent(const DEBUG_EVENT * debugEv) {
		DBG_CONTINUE_STATUS dwContinueStatus = DBG_CONTINUE;
		ThreadState threadState(debugEv->dwThreadId);
		switch (debugEv->dwDebugEventCode)
		{
		case EXCEPTION_DEBUG_EVENT:			
			switch (debugEv->u.Exception.ExceptionRecord.ExceptionCode)
			{
			case EXCEPTION_ACCESS_VIOLATION: {
				EventCallbackObjectPair event = this->eventCallbacks[ACCESS_VIOLATION_CALLBACK];
				if (event.callback) {
					event.callback(ACCESS_VIOLATION_CALLBACK, &threadState, debugEv, &dwContinueStatus, event.object);
				}				
				break;
			}
			case EXCEPTION_SINGLE_STEP: {
				//
				// HWBPs manifest as triggering a single step exception at the point of hitting the breakpoint
				//
				dwContinueStatus = DBG_CONTINUE;
				EventCallbackObjectPair event = this->eventCallbacks[SINGLE_STEP_CALLBACK];
				if (event.callback) {
					event.callback(SINGLE_STEP_CALLBACK, &threadState, debugEv, &dwContinueStatus, event.object);
				}
				size_t address = (size_t)debugEv->u.Exception.ExceptionRecord.ExceptionAddress;
				bool isHwbp = false;
				for (int i = 0; i < 4; i++) {
					HWBPDescriptor& hwbpDesc = this->breakpoints[i];
					if (hwbpDesc.address == address) {
						isHwbp = true;
						break;
					}
				}
				if (isHwbp) {
					//
					// To get past the hwbp, we have to disable the hwbp, single step, and then re-enable the hwbp.
					// To achieve this, we'll snag Dr7, clear the enable bits, single step, then restore the dr7 value.
					//
					DWORD originalDr7 = threadState.GetRegisterValue(CONTEXTREGISTER::DR7);
					Dr7_Fields dr7Fields;
					dr7Fields.int32 = originalDr7;
					dr7Fields.fields.Dr0_Local = false;
					dr7Fields.fields.Dr1_Local = false;
					dr7Fields.fields.Dr2_Local = false;
					dr7Fields.fields.Dr3_Local = false;
					DWORD eFlags = threadState.GetRegisterValue(CONTEXTREGISTER::EFLAGS);
					eFlags |= 0x100;
					threadState.SetRegisterValue(CONTEXTREGISTER::DR7, dr7Fields.int32);
					threadState.SetRegisterValue(CONTEXTREGISTER::EFLAGS, eFlags);
					threadState.FlushContext();
					threadState.Invalidate();
					DEBUG_EVENT stepDebugEv;
					ContinueDebugEvent(debugEv->dwProcessId, debugEv->dwThreadId, DBG_CONTINUE);
					bool success = WaitForDebugEvent(&stepDebugEv, INFINITE);
					assert(stepDebugEv.dwDebugEventCode == EXCEPTION_DEBUG_EVENT);
					assert(stepDebugEv.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_SINGLE_STEP);
					threadState.Invalidate();
					eFlags = (DWORD)threadState.GetRegisterValue(CONTEXTREGISTER::EFLAGS);
					eFlags &= ~(0x100);
					eFlags |= 0x1000; // resume flag
					threadState.SetRegisterValue(CONTEXTREGISTER::EFLAGS, eFlags);
					threadState.SetRegisterValue(CONTEXTREGISTER::DR7, originalDr7);
					threadState.FlushContext();
				}
				break;
			}
			case EXCEPTION_BREAKPOINT: {
				dwContinueStatus = this->HandleBreakpointEvent(&threadState, debugEv);
				break;
			}
			default:
				// Handle other exceptions. 
				if (debugEv->u.Exception.dwFirstChance) {
					dwContinueStatus = DBG_EXCEPTION_NOT_HANDLED;
					printf("Unhandled first chance exception debug event %x\n", debugEv->u.Exception.ExceptionRecord.ExceptionCode);
				}
				else {
					printf("Unhandled exception debug event %x\n", debugEv->u.Exception.ExceptionRecord.ExceptionCode);
					dwContinueStatus = DBG_CONTINUE;
				}
				break;
			}
			break;
		case CREATE_THREAD_DEBUG_EVENT:
			dwContinueStatus = this->HandleCreateThreadEvent(&threadState, debugEv);
			break;
		case LOAD_DLL_DEBUG_EVENT:
			dwContinueStatus = this->HandleLoadDllEvent(&threadState, debugEv);
			break;
		case EXIT_THREAD_DEBUG_EVENT:
			dwContinueStatus = this->HandleExitThreadEvent(&threadState, debugEv);
			break;
		case OUTPUT_DEBUG_STRING_EVENT:
			dwContinueStatus = this->HandleOutputDebugEvent(&threadState, debugEv);
			break;
		case CREATE_PROCESS_DEBUG_EVENT:
			dwContinueStatus = this->HandleCreateProcessEvent(&threadState, debugEv);
			break;
		case EXIT_PROCESS_DEBUG_EVENT:
			dwContinueStatus = this->HandleExitProcessEvent(&threadState, debugEv);
			break;
		case UNLOAD_DLL_DEBUG_EVENT:
			dwContinueStatus = this->HandleUnloadDllEvent(&threadState, debugEv);
			break;
		case RIP_EVENT:
		default:
			printf("Unhandled exception %x\n", debugEv->dwDebugEventCode);
			dwContinueStatus = DBG_CONTINUE;
			break;
		}
		return dwContinueStatus;
	}
}



