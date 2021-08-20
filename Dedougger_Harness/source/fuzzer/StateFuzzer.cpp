#include "StateFuzzer.hpp"
namespace dedougger {

	//
	// Private/protected methods
	//
	void StateFuzzer::CommonInit() {
		this->stateSaved = false;
		this->dedougger->RegisterEventCallback(DEBUGEVENTCALLBACKID::THREAD_CREATE_EVENT_CALLBACK, ThreadCreateCallbackStatic, (void*)this);
		this->dedougger->RegisterEventCallback(DEBUGEVENTCALLBACKID::EXIT_THREAD_EVENT_CALLBACK, ExitThreadCallbackStatic, (void*)this);
		this->dedougger->RegisterEventCallback(DEBUGEVENTCALLBACKID::BREAKPOINT_CALLBACK, BreakpointCallbackStatic, (void*)this);
		this->dedougger->RegisterEventCallback(DEBUGEVENTCALLBACKID::ACCESS_VIOLATION_CALLBACK, ExceptionCallbackStatic, (void*)this);
		this->dedougger->RegisterEventCallback(DEBUGEVENTCALLBACKID::SINGLE_STEP_CALLBACK, BreakpointCallbackStatic, (void*)this);
		
		this->dedougger->RegisterBreakpointResolvedCallback(this->DeferredBpResolvedCallbackStatic, (void*)this);
	}

	//
	// Dedougger takes an opaque pointer to be passed back to its callbacks.  We have to have static function to
	// interpret this opaque pointer as what it is - a StateFuzzer pointer that should have its callback routine 
	// invoked
	//
	CALLBACKRESULT StateFuzzer::ThreadCreateCallbackStatic(const DEBUGEVENTCALLBACKID eventId, ThreadState * threadState, const DEBUG_EVENT * debugEv, DBG_CONTINUE_STATUS * dwContinueStatus, void *opaque) {
		StateFuzzer *t = (StateFuzzer*)opaque;
		return t->ThreadCreateCallback(eventId, threadState, debugEv, dwContinueStatus);
	}

	CALLBACKRESULT StateFuzzer::ExitThreadCallbackStatic(const DEBUGEVENTCALLBACKID eventId, ThreadState * threadState, const DEBUG_EVENT * debugEv, DBG_CONTINUE_STATUS * dwContinueStatus, void * opaque) 	{
		StateFuzzer *t = (StateFuzzer*)opaque;
		return t->ExitThreadCallback(eventId, threadState, debugEv, dwContinueStatus);
	}

	CALLBACKRESULT StateFuzzer::BreakpointCallbackStatic(const DEBUGEVENTCALLBACKID eventId, ThreadState* threadState, const DEBUG_EVENT* debugEv, DBG_CONTINUE_STATUS* dwContinueStatus, void* opaque)	{
		StateFuzzer* t = (StateFuzzer*)opaque;
		return t->BreakpointCallback(eventId, threadState, debugEv, dwContinueStatus);
	}

	CALLBACKRESULT StateFuzzer::ExceptionCallbackStatic(const DEBUGEVENTCALLBACKID eventId, ThreadState* threadState, const DEBUG_EVENT* debugEv, DBG_CONTINUE_STATUS* dwContinueStatus, void* opaque){
		StateFuzzer* t = (StateFuzzer*)opaque;
		return t->ExceptionCallback(eventId, threadState, debugEv, dwContinueStatus);
	}

	void StateFuzzer::DeferredBpResolvedCallbackStatic(const char* moduleName, size_t offset, size_t resolvedAddress, void* opaque) {
		StateFuzzer* t = (StateFuzzer*)opaque;
		return t->DeferredBpResolvedCallback(moduleName, offset, resolvedAddress);
	}

	CALLBACKRESULT StateFuzzer::ThreadCreateCallback(const DEBUGEVENTCALLBACKID eventId, ThreadState * threadState, const DEBUG_EVENT * debugEv, DBG_CONTINUE_STATUS * dwContinueStatus) 	{
		if (this->stateSaved) {
			HANDLE deadThreadHandle = debugEv->u.CreateThread.hThread;
			HANDLE handleCopy;
			this->dedougger->DuplicateThreadHandle(deadThreadHandle, &handleCopy);
			this->threadRestorer->add_thread_to_kill(threadState->GetThreadId(), handleCopy);
		}

		return CALLBACKRESULT::BP_HANDLE;
	}

	CALLBACKRESULT StateFuzzer::ExitThreadCallback(const DEBUGEVENTCALLBACKID eventId, ThreadState * threadState, const DEBUG_EVENT * debugEv, DBG_CONTINUE_STATUS * dwContinueStatus)	{
		this->threadRestorer->remove_thread_from_kill(threadState->GetThreadId());
		return CALLBACKRESULT::BP_HANDLE;
	}

	CALLBACKRESULT StateFuzzer::BreakpointCallback(const DEBUGEVENTCALLBACKID eventId, ThreadState* threadState, const DEBUG_EVENT* debugEv, DBG_CONTINUE_STATUS* dwContinueStatus) 	{
		//
		// This is where hits to our save state and reset state points will come through.  
		//
		size_t address = (size_t)debugEv->u.Exception.ExceptionRecord.ExceptionAddress;
		if (address == this->stateSavePoint && !this->stateSaved) {
			int hwbpIndex = this->dedougger->ClearHWBPByAddress(address);
			this->SaveState();
		}
		else {
			if (this->stateResetPoints.find(address) != this->stateResetPoints.end()) {
				assert(false);
			}
		}
		return CALLBACKRESULT::BP_HANDLE;
	}

	CALLBACKRESULT StateFuzzer::ExceptionCallback(const DEBUGEVENTCALLBACKID eventId, ThreadState* threadState, const DEBUG_EVENT* debugEv, DBG_CONTINUE_STATUS* dwContinueStatus) {
		CALLBACKRESULT result = CALLBACKRESULT::BP_DONT_HANDLE;
		size_t rip = (size_t)debugEv->u.Exception.ExceptionRecord.ExceptionAddress;
		size_t exceptingAddress = debugEv->u.Exception.ExceptionRecord.ExceptionInformation[1];
		if (debugEv->u.Exception.dwFirstChance && this->pageRestorer->touch_address((LPVOID)exceptingAddress)) {
			result = CALLBACKRESULT::BP_HANDLE;
		}  else {			
			this->RestoreState();			
			result = CALLBACKRESULT::BP_HANDLE;
		}
		return result;
	}

	void StateFuzzer::DeferredBpResolvedCallback(const char* moduleName, size_t offset, size_t resolvedAddress) {
		DeferredPoint resolvedPoint(moduleName, offset);
		//
		// We don't know, initially, if this is one of our callbacks.  And if it is, we don't know if it's the start
		// point or one of the reset points.
		//
		if (this->stateSavePointDeferred == resolvedPoint) {
			this->stateSavePoint = resolvedAddress;
			// Clear out the deferred point just in case.  If it's accidentally used in the future, it should crash.
			this->stateSavePointDeferred = DeferredPoint();
		}
		
		//
		// It's possible that the user has defined duplicate reset points.  For that reason, we will iterate across
		// the entire vector of reset points to delete any duplicates that may have been registered.  This is for
		// debugging/sanity purposes - once the debugger has started and all relevant modules have been loaded, there
		// should be no unresolved points left.
		//
		for (auto iter = this->stateResetPointsDeferred.begin(); iter != this->stateResetPointsDeferred.end();) {
			if (*iter == resolvedPoint) {
				this->stateResetPoints.insert(resolvedAddress);
				iter = this->stateResetPointsDeferred.erase(iter);
			}
			else {
				iter++;
			}
		}
	}

	//
	// Public methods
	//
	SaveStateResults StateFuzzer::SaveState() {
		SaveStateResults results;
		results.pagesSaved = this->pageRestorer->save_state();
		results.threadsSaved = this->threadRestorer->save_state();
		this->stateSaved = true;
		return results;
	}

	RestoreStateResults StateFuzzer::RestoreState() {
		RestoreStateResults results;
		results.pagesRestored = this->pageRestorer->restore_state();
		results.threadsRestored = this->threadRestorer->restore_state();
		this->restoreCount++;
		if (tickStart == 0) {
			tickStart = GetTickCount64();
		}
		if (this->restoreCount % 10000 == 0) {
			uint64_t currentTick = GetTickCount64();
			uint64_t elapsedTicks = currentTick - this->tickStart;
			float elapsedSeconds = (float)elapsedTicks / 1000.0;
			printf("%f cases per second\n", (float)this->restoreCount / elapsedSeconds);
		}
		return results;
	}

	void StateFuzzer::SetStateSavePointDeferred(const char* moduleName, size_t offset)	{
		this->stateSavePointDeferred = DeferredPoint(moduleName, offset);
		//this->dedougger->SetHWBPInModule(moduleName, offset, BPCONDITION::EXECUTION, BPLEN::ONE);
		this->dedougger->SetHWBPInModule(moduleName, offset, BPCONDITION::EXECUTION, BPLEN::ONE);
	}

	void StateFuzzer::AddStateResetPointDeferred(const char* moduleName, size_t offset)	{
		this->stateResetPointsDeferred.push_back(DeferredPoint(moduleName, offset));
		this->dedougger->SetHWBPInModule(moduleName, offset, BPCONDITION::EXECUTION, BPLEN::ONE);
	}

	void StateFuzzer::BeginDebugging() {
		this->dedougger->BeginDebugging();
	}
	
	bool DeferredPoint::eq(const DeferredPoint& other) {
		return (_strcmpi(this->moduleName, other.moduleName) == 0 && this->offset == other.offset);
	}
}