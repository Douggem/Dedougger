#pragma once
#include "breakpoints\deferredhwbp.h"
#include "breakpoints\DeferredSWBP.h"
#include "breakpoints\HwbpDescriptor.h"
#include "breakpoints\swbp.hpp"
#include "targetstate\peinfo.hpp"
#include "targetstate\ThreadState.hpp"
#include "targetstate\moduleinfo.hpp"

#include <array>
#include <assert.h>
#include <map>
#include <memory>
#include <vector>

#include <Windows.h>
#include <processthreadsapi.h>
#include <DbgHelp.h>
#include <stdio.h>
#include <tchar.h>

namespace dedougger {
	class ModuleNotFoundException : public std::exception {};
	class FunctionNotFoundException : public std::exception {};

	enum DEBUGEVENTCALLBACKID {
		THREAD_CREATE_EVENT_CALLBACK = 0,
		CREATE_PROCESS_EVENT_CALLBACK,
		EXIT_THREAD_EVENT_CALLBACK,
		EXIT_PROCESS_EVENT_CALLBACK,
		LOAD_DLL_EVENT_CALLBACK,
		UNLOAD_DLL_EVENT_CALLBACK,
		OUTPUT_DEBUG_STRING_CALLBACK,
		RIP_EVENT_CALLBACK,
		ACCESS_VIOLATION_CALLBACK,
		SINGLE_STEP_CALLBACK,
		BREAKPOINT_CALLBACK,
		INITIAL_BREAKPOINT_CALLBACK // leave this as the last enum so the size can always be determined by this value
	};

	enum CALLBACKRESULT {
		BP_HANDLE = 0, // tells the debugger to 'handle' the breakpoint, i.e. replace the instruction, single step, etc.
		BP_DONT_HANDLE // tells the debugger not to handle the breakpoint, just continue execution
	};
		
	typedef DWORD DBG_CONTINUE_STATUS;
	typedef CALLBACKRESULT (*EventCallback)(const DEBUGEVENTCALLBACKID eventId, ThreadState* threadState, const DEBUG_EVENT *debugEv, DBG_CONTINUE_STATUS* dwContinueStatus, void *callbackObject);
	typedef void (*DeferredBpResolvedCallback)(const char* moduleName, size_t offset, size_t resolvedAddress, void *callbackObject);

	struct EventCallbackObjectPair {
		EventCallback	callback;
		void*			object;

		EventCallbackObjectPair() {
			this->callback = nullptr;
			this->object = nullptr;
		}

		EventCallbackObjectPair(EventCallback callback, void* object) {
			this->callback = callback;
			this->object = object;
		}
	};

	struct ResolvedCallbackObjectPair {
		DeferredBpResolvedCallback callback = nullptr;
		void* object						= nullptr;

		ResolvedCallbackObjectPair() {};
		ResolvedCallbackObjectPair(DeferredBpResolvedCallback callback, void* object) : callback(callback), object(object) {};
	};


	/**
	 * Dedougger - a 'flexible' debugger designed to be reasonably performant and modular enough to facilitate its use
	 *	in fuzzing applications.  
	 *
	 *	Methods:
	 *		Dedougger(pid) - attach to a running process
	 *		Dedougger(executablePath) - spin up and debug a new process
	 *		BeginDebugging() - start debugging the process.  This method does not return, but communicates through registered
	 *			event callbacks.
	 *		BreakProcess() - triggers a breakpoint event in the target process
	 *		GetCallStack(thread, threadState) - returns a vector of STACKFRAMEs representing the call stack of the given
	 *			thread.  This should only be called when the process is in a broken state, i.e. from one of the event
	 *			callbacks.
	 *		ClearHWBP(index) - remove a hardware breakpoint
	 *		ClearSWBP(address) - remove a software breakpoint
	 *		SetSWBP(address, replacePageProtection, replaceInstOnBPHit) - set a software breakpoint.  replacePageProtection, 
	 *			if true, means that the page the breakpoint is on will have its original page protections (i.e. not writeable)
	 *			reset after the breakpoint is hit and resumed from.  This is the default, and is able to be turned off only 
	 *			because in certain situations the performance hit of the extra call to VirtualProtect may be not worth it.  
	 *			replaceInstOnBPHit, if true, will replace the 0xCC byte with the original instruction byte after the BP is
	 *			hit.  This is able to be turned off because you may want to not resume execution after your BP is hit, and 
	 *			the performance hit of two virtualprotects may not be desisreable.
	 *		SetHWBP(address, condition, len) - set hardware breakpoint.  For execution breakpoints, set len to ONE.
	 *		SetSWBPInModule(module_name, offset) - set software breakpoint at offset into module.  If module isn't loaded,
	 *			breakpoint will be deferred.
	 *		SetHWBPInModule(module_name, offset) - set hardware breakpoint at offset into module.  If module isn't loaded,
	 *			breakpoint will be deferred.
	 *		GetMainModuleBase() - get the base address of the main module
	 *		GetModuleByName(moduleName) - get the base address of a module by its executable name
	 *		RegisterEventCallback(eventId, callback, callbackObject) - registers an event callback.  The callback will be called
	 *			when the eventId event (such as THREAD_CREATE, etc.) is triggered in the debugger.
	 *		ProcessId() - gets the debugged process ID
	 *		DuplicateThreadHandle(threadHandle, newHandle) - duplicates a thread handle for a thread in the debugged process
	 *
	 */

	class Dedougger {			
		DWORD	processId;
		bool	firstBreakpointHit;		
		HANDLE	processHandle;
		HANDLE	startThreadHandle;		
		//
		// Breakpoints
		//
		HWBPDescriptor				breakpoints[4] = { 0 };
		HWBPRegisterState			hwbpState;
		std::pair<SIZE_T, uint8_t>	resettingBp;
		std::map<SIZE_T, SWBP>		swbps;
		std::vector<DeferredHWBP>	queued_deferred_hwbps;
		std::vector<DeferredSWBP>	queued_deferred_swbps;
		//
		// Target process info - modules, threads, etc.
		//
		std::map<std::string, ModuleInfo>	modulesByName;
		std::map<size_t, ModuleInfo>		modulesByAddress;
		std::map<DWORD, DWORD>				threads;
		ModuleInfo							mainModule;
		
		// Array of callbacks that will be called on triggering of each debug event		
		std::array<EventCallbackObjectPair, INITIAL_BREAKPOINT_CALLBACK + 1>	eventCallbacks;
		ResolvedCallbackObjectPair												resolvedBpCallback;
	public:
		/* Constructor that starts a new process for the executable passed
		 *	Args:
		 *		TCHAR* exec_path - binary to start up and attach to
		 */
		Dedougger(const TCHAR* exec_path);
		/* Constructor that attaches to an already running process
		 *	Args:
		 *		DWORD pid - the process ID of the process to attach to
		 */
		Dedougger(DWORD pid);	
		~Dedougger();
		void BeginDebugging();
		bool BreakProcess();
		std::vector<STACKFRAME64> GetCallStack(HANDLE thread, ThreadState *threadState);

		void ClearHWBP(int bpIndex);
		int ClearHWBPByAddress(size_t address);
		int  ClearSWBP(size_t address);
		int  SetSWBP(size_t address, bool replacePageProtection = true, bool replaceInstOnBPHit = true);
		int  SetHWBP(size_t address, BPCONDITION condition, BPLEN len);
		int  SetSWBP(const std::string &moduleName, const std::string &functionName, _Out_opt_ size_t *resolvedAddress, bool replacePageProtection = true, bool replaceInstOnBPHit = true);
		int  SetHWBP(const std::string &moduleName, const std::string &functionName, _Out_opt_ size_t *resolvedAddress, BPCONDITION condition, BPLEN len);
		void SetSWBPInModule(const char* symbol_name, SIZE_T offset);
		void SetHWBPInModule(const char* symbol_name, SIZE_T offset, BPCONDITION condition, BPLEN len);

		void* GetMainModuleBase();
		void* GetModuleByName(wchar_t* module_name);
		EventCallbackObjectPair RegisterEventCallback(DEBUGEVENTCALLBACKID eventId, EventCallback callback, void *callbackObject);
		ResolvedCallbackObjectPair RegisterBreakpointResolvedCallback(DeferredBpResolvedCallback callback, void* object);

		DWORD ProcessId() { return this->processId; }
		bool DuplicateThreadHandle(HANDLE threadHandle, HANDLE* newHandle) {
			return DuplicateHandle(this->processHandle, threadHandle, this->processHandle, newHandle, THREAD_ALL_ACCESS, false, NULL); 
		}


	private:
		void  CommonInit();		
		void  ApplyHWBPs();
		void  ResolveDeferredBps(const char* moduleName, size_t moduleeBaseAddress);
		void  WriteBreakpointsToThread(ThreadState *threadState);
		void  MapDll(ModuleInfo newDll);
		const ModuleInfo *ResolveModule(std::string moduleName) const;
		SP_ExportedFunction ResolveFunction(const std::string &moduleName, const std::string &functionName) const;
		DWORD ResumeFromBreakpoint(const DEBUG_EVENT *debugEv, ThreadState* threadState, bool replaceBreakpoint = true);
		//
		// Internal Debug event handlers
		//
		DBG_CONTINUE_STATUS HandleDebugEvent		(const DEBUG_EVENT* debugEv);
		DBG_CONTINUE_STATUS HandleLoadDllEvent		(ThreadState* threadState, const DEBUG_EVENT *debugEv);
		DBG_CONTINUE_STATUS HandleUnloadDllEvent	(ThreadState* threadState, const DEBUG_EVENT *debugEv);
		DBG_CONTINUE_STATUS HandleBreakpointEvent	(ThreadState* threadState, const DEBUG_EVENT *debugEv);
		DBG_CONTINUE_STATUS HandleExitThreadEvent	(ThreadState* threadState, const DEBUG_EVENT *debugEv);
		DBG_CONTINUE_STATUS HandleExitProcessEvent	(ThreadState* threadState, const DEBUG_EVENT *debugEv);
		DBG_CONTINUE_STATUS HandleOutputDebugEvent	(ThreadState* threadState, const DEBUG_EVENT *debugEv);
		DBG_CONTINUE_STATUS HandleCreateThreadEvent	(ThreadState* threadState, const DEBUG_EVENT *debugEv);
		DBG_CONTINUE_STATUS HandleCreateProcessEvent(ThreadState* threadState, const DEBUG_EVENT *debugEv);

	};
	typedef std::shared_ptr<Dedougger> SP_Dedougger;
	typedef std::unique_ptr<Dedougger> UP_Dedougger;
}

