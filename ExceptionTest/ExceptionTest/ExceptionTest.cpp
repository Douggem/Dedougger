// ExceptionTest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <intrin.h>

LONG WINAPI ExceptionHandler(LPEXCEPTION_POINTERS exceptionInfo) {	
	printf("Exception handler\n");
	DWORD exceptionCode = exceptionInfo->ExceptionRecord->ExceptionCode;
	printf("Caught exception - code 0x%X\n", exceptionCode);
	PCONTEXT contextRecord = exceptionInfo->ContextRecord;
	printf("---------------------\n");
	printf("Context:\n");
	printf(" esp - %p", contextRecord->Rsp);
	return 0;
}

int main()
{
	
	printf("lma0o\n");
	int number = 0;	
	int* test = nullptr;
	for (int i = 0; i < 5; i++) {
		number++;
	}
	//printf("lmao %d ", *test);
	int lmao = *test;
	
	BYTE test2;
	while (true) {		
		if (rand() % 1000 == 1) {
			test2 = 0;
		}
		else {
			test2 = 0xCC;
		}		
	}
	
	printf("ayy lmao %x\n", test2);
    return 0;
}

