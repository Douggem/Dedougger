#pragma once
#include <exception>
#include <stdint.h>
#include <Windows.h>
#include "breakpoints/HwbpDescriptor.h"


class InvalidRegisterException			: public std::exception {};
class GetThreadContextFailureException	: public std::exception {};
class SetThreadContextFailureException	: public std::exception {};


//
// The CONTEXTREGISTER enum is used to identify registers to read or manipulate in the ThreadState.
// Each entry is the offset into the CONTEXT data structure of the register, which greatly increases
// the jankiness of this codebase but also decreased the amount of typing I had to do.
//

enum CONTEXTREGISTER {
#ifdef _AMD64_
	P1HOME = offsetof(CONTEXT, P1Home),
	P2HOME = offsetof(CONTEXT, P2Home),
	P3HOME = offsetof(CONTEXT, P3Home),
	P4HOME = offsetof(CONTEXT, P4Home),
	P5HOME = offsetof(CONTEXT, P5Home),
	P6HOME = offsetof(CONTEXT, P6Home),
		
	MXCSR  = offsetof(CONTEXT, MxCsr),
	
	SEGCS  = offsetof(CONTEXT, SegCs),	
	SEGDS  = offsetof(CONTEXT, SegDs),
	SEGES  = offsetof(CONTEXT, SegEs),
	SEGFS  = offsetof(CONTEXT, SegFs),
	SEGGS  = offsetof(CONTEXT, SegGs),
	SEGSS  = offsetof(CONTEXT, SegSs),
	EFLAGS = offsetof(CONTEXT, EFlags),
	
	DR0 = offsetof(CONTEXT, Dr0),
	DR1 = offsetof(CONTEXT, Dr1),
	DR2 = offsetof(CONTEXT, Dr2),
	DR3 = offsetof(CONTEXT, Dr3),
	DR6 = offsetof(CONTEXT, Dr6),
	DR7 = offsetof(CONTEXT, Dr7),

	RAX = offsetof(CONTEXT, Rax),
	RCX = offsetof(CONTEXT, Rcx),
	RDX = offsetof(CONTEXT, Rdx),
	RBX = offsetof(CONTEXT, Rbx),
	RSP = offsetof(CONTEXT, Rsp),
	RBP = offsetof(CONTEXT, Rbp),
	RSI = offsetof(CONTEXT, Rsi),
	RDI = offsetof(CONTEXT, Rdi),
	R8  = offsetof(CONTEXT, R8),
	R9  = offsetof(CONTEXT, R9),
	R10 = offsetof(CONTEXT, R10),
	R11 = offsetof(CONTEXT, R11),
	R12 = offsetof(CONTEXT, R12),
	R13 = offsetof(CONTEXT, R13),
	R14 = offsetof(CONTEXT, R14),
	R15 = offsetof(CONTEXT, R15),

	RIP = offsetof(CONTEXT, Rip),

	DEBUGCONTROL = offsetof(CONTEXT, DebugControl),
	LASTBRANCHTORIP = offsetof(CONTEXT, LastBranchToRip),
	LASTBRANCHFROMRIP = offsetof(CONTEXT, LastBranchFromRip),
	LASTEXCEPTIONTORIP = offsetof(CONTEXT, LastExceptionToRip),
	LASTEXCEPTIONFROMRIP = offsetof(CONTEXT, LastExceptionFromRip),
	
	//
	// Floating point registers left out for now TODO
	//

#endif // _AMD64_
	
};

/**
 * Thread State - given a ThreadId or handle, can read and set the registers of a given thread context.  
 *	Will flush changes to the thread on destruction, and will lazily read the context.  Can be passed around
 *	plugins and modules to minimize calls to get/set thread context
 *
 *	Methods:
 *		GetRegisterValue(CONTEXTREGISTER) - will get the value of a register from the thread context, calling 
 *			GetThreadContext if necessary.
 *		SetRegisterValue(CONTEXTREGISTER, size_t) - will set the value of a register in a context, does not 
 *			immediately write the value to the target thread
 *		FlushContext() - flushes changes to the context to the target thread
 *		Invalidate() - invalidates the contents of the class, the next register get/set will re-fetch the thread context
 *		GetContextCopy() - returns a copy of the CONTEXT structure, calls GetThreadContext if necessary
 *		GetMutableContext() - returns a pointer to this class's internal CONTEXT instance for mutation.  Marks the 
 *			context as 'dirty' so the context will be flushed on object destruction.
 */
class ThreadState {
	DWORD threadId;
	HANDLE threadHandle;
	HANDLE processHandle;
	CONTEXT threadContext;	
	bool dirty; // the context is 'dirty' and needs to be written back to the target process
	bool contextRead; // the context has been read and values can be retrieved from it
	bool ownThreadHandle; // the thread state 'owns' the handle, and will close it on destruction
	bool ownProcessHandle; // the thread state 'owns' the handle, and will close it on destruction

	void PullThreadContext();
public:
	ThreadState(DWORD threadId) {
		this->threadId = threadId;
		this->threadHandle = INVALID_HANDLE_VALUE;
		this->processHandle = INVALID_HANDLE_VALUE;		
		this->dirty = false;
		this->contextRead = false;
		this->ownThreadHandle = false;
		this->ownProcessHandle = false;

		this->threadContext.ContextFlags = CONTEXT_ALL;
	}
	
	ThreadState(DWORD threadId, HANDLE threadHandle) {
		this->threadId = threadId;
		this->threadHandle = threadHandle;
		this->processHandle = INVALID_HANDLE_VALUE;
		this->dirty = false;
		this->contextRead = false;
		this->ownThreadHandle = false;
		this->ownProcessHandle = false;
		this->threadContext.ContextFlags = CONTEXT_ALL;
	}

	ThreadState(DWORD threadId, HANDLE threadHandle, const CONTEXT* threadContext) {
		this->threadId = threadId;
		this->threadHandle = threadHandle;
		this->processHandle = INVALID_HANDLE_VALUE;
		this->dirty = false;
		this->contextRead = true;
		this->ownThreadHandle = false;
		this->ownProcessHandle = false;
		this->threadContext = *threadContext;
	}

	~ThreadState() {
		if (this->dirty) {
			this->FlushContext();
		}
		if (this->ownThreadHandle) {
			CloseHandle(this->threadHandle);
		}
		if (this->ownProcessHandle) {
			CloseHandle(this->processHandle);
		}
	}
		
	size_t		GetRegisterValue(const CONTEXTREGISTER reg);
	void		SetRegisterValue(const CONTEXTREGISTER reg, size_t value);
	CONTEXT		GetContextCopy();
	CONTEXT*	GetMutableContext() { this->dirty = true; return &this->threadContext; }
	void		FlushContext();
	void		Invalidate() { this->dirty = false; this->contextRead = false; }
	const DWORD GetThreadId() const { return this->threadId; }
	void DisableHWBPByIndex(int index);
};