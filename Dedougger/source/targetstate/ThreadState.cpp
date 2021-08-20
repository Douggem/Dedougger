
#include "ThreadState.hpp"

/*
While the return type is a size_t/general purpose register width, the register requested may be smaller.
The caller must take care to know the actual size of the register they're requesting.
*/

void ThreadState::PullThreadContext() {
	if (this->threadHandle == INVALID_HANDLE_VALUE) {
		this->threadHandle = OpenThread(THREAD_ALL_ACCESS, false, this->threadId);
	}
	int result = GetThreadContext(this->threadHandle, &this->threadContext);
	if (!result) {
		throw(GetThreadContextFailureException());
	}
	this->ownThreadHandle = true;
	this->contextRead = true;
}

size_t ThreadState::GetRegisterValue(const CONTEXTREGISTER reg) {
	size_t result;
	//
	// Let's trigger people that don't like hacky code
	//
	uint8_t *regAddress = (uint8_t*)(&this->threadContext) + reg;	
	if (!this->contextRead) {
		this->PullThreadContext();
	}

	switch (reg) {
#ifdef _AMD64_
	case P1HOME:
	case P2HOME:
	case P3HOME:
	case P4HOME:
	case P5HOME:
	case P6HOME:
	case DR0:
	case DR1:
	case DR2:
	case DR3:
	case DR6:
	case DR7:
	case RAX:
	case RCX:
	case RDX:
	case RBX:
	case RSP:
	case RBP:
	case RSI:
	case RDI:
	case R8:
	case R9:
	case R10:
	case R11:
	case R12:
	case R13:
	case R14:
	case R15:
	case RIP:
	case DEBUGCONTROL:
	case LASTBRANCHTORIP:
	case LASTBRANCHFROMRIP:
	case LASTEXCEPTIONTORIP:
	case LASTEXCEPTIONFROMRIP:
		result = *(size_t*)regAddress;
		break;
	case SEGCS:
	case SEGDS:
	case SEGES:
	case SEGFS:
	case SEGGS:
	case SEGSS:
		result = *(WORD*)regAddress;
		break;	
	case MXCSR:
	case EFLAGS:
		result = *(DWORD*)regAddress;
		break;
#endif
	default:
		throw(InvalidRegisterException());
	}
	return result;
}

void ThreadState::SetRegisterValue(const CONTEXTREGISTER reg, size_t value) {
	uint8_t *regAddress = (uint8_t*)(&this->threadContext) + reg;
	if (!this->contextRead) {
		this->PullThreadContext();
	}

	switch (reg) {
#ifdef _AMD64_
	case P1HOME:
	case P2HOME:
	case P3HOME:
	case P4HOME:
	case P5HOME:
	case P6HOME:
	case DR0:
	case DR1:
	case DR2:
	case DR3:
	case DR6:
	case DR7:
	case RAX:
	case RCX:
	case RDX:
	case RBX:
	case RSP:
	case RBP:
	case RSI:
	case RDI:
	case R8:
	case R9:
	case R10:
	case R11:
	case R12:
	case R13:
	case R14:
	case R15:
	case RIP:
	case DEBUGCONTROL:
	case LASTBRANCHTORIP:
	case LASTBRANCHFROMRIP:
	case LASTEXCEPTIONTORIP:
	case LASTEXCEPTIONFROMRIP:
		*(size_t*)regAddress = value;
		break;
	case SEGCS:
	case SEGDS:
	case SEGES:
	case SEGFS:
	case SEGGS:
	case SEGSS:
		*(WORD*)regAddress = value;
		break;	
	case MXCSR:
	case EFLAGS:
		*(DWORD*)regAddress = value;
		break;
#endif
	default:
		throw(InvalidRegisterException());
	}
	this->dirty = true;
}

CONTEXT ThreadState::GetContextCopy() {
	if (!this->contextRead) {
		this->PullThreadContext();
	}
	return this->threadContext;
}

void ThreadState::FlushContext() {
	if (this->dirty && this->contextRead) {
		int result;
		//
		// If context has been read, threadHandle has been gathered/is not invalid_handle_value, 
		// no need to recheck here
		//
		result = SetThreadContext(this->threadHandle, &this->threadContext);
		if (!result) {
			throw(SetThreadContextFailureException());
		}
		this->contextRead = false;
		this->PullThreadContext();
		this->dirty = false;
	}
}

void ThreadState::DisableHWBPByIndex(int index) {
	DWORD dr7 = this->GetRegisterValue(CONTEXTREGISTER::DR7);
	dedougger::Dr7_Fields fields;
	fields.int32 = dr7;
	switch (index) {
	case 0:
		fields.fields.Dr0_Local = false;
		break;
	case 1:
		fields.fields.Dr1_Local = false;
		break;
	case 2:
		fields.fields.Dr2_Local = false;
		break;
	case 3:
		fields.fields.Dr3_Local = false;
		break;
	}
	this->SetRegisterValue(CONTEXTREGISTER::DR7, fields.int32);
}
