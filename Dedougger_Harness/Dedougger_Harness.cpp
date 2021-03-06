// Dedougger_Harness.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "fuzzer/statefuzzer.hpp"
#include "dedougger.hpp"

using namespace dedougger;

DWORD getProcessPidByName(const WCHAR* processName) {
	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

	if (Process32First(snapshot, &entry) == TRUE)
	{
		while (Process32Next(snapshot, &entry) == TRUE)
		{
			if (_wcsicmp(entry.szExeFile, processName) == 0)
			{
				return entry.th32ProcessID;
			}
		}
	}
	return 0;
}

int main(int argc, char** argv)
{
	const TCHAR* target_process = L"C:\\Users\\User1\\source\\Dedougger\\ExceptionTest\\x64\\Release\\ExceptionTest.exe";
	//DWORD target_process = getProcessPidByName(L"dayzserver_x64.exe");
	assert(target_process != 0);
	dedougger::StateFuzzer dougger(target_process);
	dougger.SetStateSavePointDeferred("ExceptionTest.exe", 0x1029);
	//dougger.SetHWBPInModule((char*)"ExceptionTest.exe", 0x1000, dedougger::BPCONDITION::EXECUTION, dedougger::BPLEN::ONE);
	//dougger.SetSWBPInModule((char*)"dayzserver_x64.exe", 0x1000);
	//dougger.SetHWBPInModule((char*)"dayzserver_x64.exe", 0x1010, BPCONDITION::EXECUTION, BPLEN::ONE);
	dougger.BeginDebugging();
    return 0;
}

DWORD find_process_by_module_name(const TCHAR* processName) {
	int threads_saved = 0;

	HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	//
	// Walk all threads on the system and add threads belonging to the target
	// process to the process_threads list
	//
	if (h != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32 procEntry = { 0 };
		procEntry.dwSize = sizeof(procEntry);
		if (Process32First(h, &procEntry)) {
			do {
				if (_tcscmp(processName, procEntry.szExeFile) == 0) {
					return procEntry.th32ProcessID;
				}
				printf("%S\n", procEntry.szExeFile);
			} while (Process32Next(h, &procEntry));
		}
	}

	return 0;
}