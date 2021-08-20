#pragma once
#include "dedougger.hpp"
#include "pagerestorer\PageRestorerEx.h"
#include "threadrestorer\ThreadRestorerEx.hpp"

#include <vector>

namespace dedougger {

	struct SaveStateResults {
		uint16_t pagesSaved;
		uint16_t threadsSaved;
	};

	struct RestoreStateResults {
		uint16_t pagesRestored;
		uint16_t threadsRestored;
	};
	
	struct DeferredPoint {
		const char* moduleName = nullptr;
		size_t offset = 0;
		DeferredPoint(const char* moduleName, size_t offset) : moduleName(moduleName), offset(offset) {};
		DeferredPoint() {};
		bool eq(const DeferredPoint& other);
		bool operator==(const DeferredPoint& other) { return this->eq(other); }
	};

	/**
	 * StateFuzzer - a base class for a fuzzer that needs to save and reset the state of the fuzzed process.
	 *	A file fuzzer, network fuzzer, etc. would save the state at the beginning of the fuzz iteration (SaveState())
	 *	and restore the state either at the end of the iteration or in the instance of a crash (RestoreState()).  This
	 *	class registers callbacks to handle the tracking of threads through the debugger but the handling of all other
	 *	exceptions is up to the child class inheriting from StateFuzzer.  All methods and members are protected so they
	 *	can be utilized by the child class.
	 *
	 *	Methods:
	 *		StateFuzzer(pid) - attach to a running process
	 *		StateFuzzer(executablePath) - spin up and debug a new process
	 *		SaveState() - saves the state of all memory pages and threads
	 *		RestoreState() - restores the state of all memory pages and threads.  Keep in mind handles and other things
	 *			are not tracked and not restored.
	 *		BeginDebugging() - starts debugging the target process.  This method does not return - the debugger will 
	 *			communicate with the object through event callbacks for various exceptions.
	 *	Members: - all protected, not intended for use but available to child classes just in case
	 *		threadRestorer
	 *		pageRestorer
	 *		dedougger
	 *		stateSaved - true if the state has been saved, false otherwise.  Used internally in the thread create/thread exit
	 *			event callbacks to determine when to track new threads.
	 */

	class StateFuzzer {
	protected:
		UP_ThreadRestorerEx threadRestorer;
		UP_PageRestorerEx	pageRestorer;
		UP_Dedougger		dedougger;
		bool				stateSaved;
		size_t				stateSavePoint = 0;
		DeferredPoint		stateSavePointDeferred;
		uint64_t			restoreCount = 0;
		uint64_t			tickStart = 0;
		std::set<size_t>	stateResetPoints;
		std::vector<DeferredPoint> stateResetPointsDeferred;


		void CommonInit();
		static CALLBACKRESULT ThreadCreateCallbackStatic(const DEBUGEVENTCALLBACKID eventId, ThreadState * threadState, const DEBUG_EVENT * debugEv, DBG_CONTINUE_STATUS * dwContinueStatus, void *opaque);
		static CALLBACKRESULT ExitThreadCallbackStatic(const DEBUGEVENTCALLBACKID eventId, ThreadState * threadState, const DEBUG_EVENT * debugEv, DBG_CONTINUE_STATUS * dwContinueStatus, void *opaque);
		static CALLBACKRESULT BreakpointCallbackStatic(const DEBUGEVENTCALLBACKID eventId, ThreadState * threadState, const DEBUG_EVENT * debugEv, DBG_CONTINUE_STATUS * dwContinueStatus, void *opaque);
		static CALLBACKRESULT ExceptionCallbackStatic(const DEBUGEVENTCALLBACKID eventId, ThreadState * threadState, const DEBUG_EVENT * debugEv, DBG_CONTINUE_STATUS * dwContinueStatus, void *opaque);
		static void DeferredBpResolvedCallbackStatic(const char* moduleName, size_t offset, size_t resolvedAddress, void* opaque);
		CALLBACKRESULT ThreadCreateCallback(const DEBUGEVENTCALLBACKID eventId, ThreadState* threadState, const DEBUG_EVENT *debugEv, DBG_CONTINUE_STATUS* dwContinueStatus);
		CALLBACKRESULT ExitThreadCallback(const DEBUGEVENTCALLBACKID eventId, ThreadState* threadState, const DEBUG_EVENT *debugEv, DBG_CONTINUE_STATUS* dwContinueStatus);
		CALLBACKRESULT BreakpointCallback(const DEBUGEVENTCALLBACKID eventId, ThreadState* threadState, const DEBUG_EVENT *debugEv, DBG_CONTINUE_STATUS* dwContinueStatus);
		CALLBACKRESULT ExceptionCallback(const DEBUGEVENTCALLBACKID eventId, ThreadState* threadState, const DEBUG_EVENT *debugEv, DBG_CONTINUE_STATUS* dwContinueStatus);
		void DeferredBpResolvedCallback(const char* moduleName, size_t offset, size_t resolvedAddress);

	public:
		StateFuzzer();

		/* Constructor for an already started process
			Args:
				pid - the process ID of the process to debug/fuzz			
		 */
		StateFuzzer(DWORD pid) {
			this->dedougger			= std::make_unique<Dedougger>(pid);
			this->pageRestorer		= std::make_unique<PageRestorerEx>(pid);
			this->threadRestorer	= std::make_unique<ThreadRestorerEx>(pid);
			this->CommonInit();
		}

		/* Constructor for starting a new process
			Args:
				execPath - the path to the binary executable to spin up a new process for
		 */
		StateFuzzer(const TCHAR* execPath) {
			this->dedougger = std::make_unique<Dedougger>(execPath);
			DWORD pid = this->dedougger->ProcessId();
			this->pageRestorer = std::make_unique<PageRestorerEx>(pid);
			this->threadRestorer = std::make_unique<ThreadRestorerEx>(pid);
			this->CommonInit();
		}

		SaveStateResults SaveState();
		RestoreStateResults RestoreState();		
		void SetStateSavePointDeferred(const char* moduleName, size_t offset);
		void AddStateResetPointDeferred(const char* moduleName, size_t offset);
		void BeginDebugging();

	};
};