#pragma once
#include "StateFuzzer.hpp"

namespace dedougger {
	class FileFuzzer : public StateFuzzer {
		TCHAR *fileName;
	public:
		FileFuzzer(const TCHAR *fileName, DWORD pid) : StateFuzzer(pid) {
			this->fileName = _tcsdup(fileName);
		}

		FileFuzzer(const TCHAR *fileName, const TCHAR *execPath) : StateFuzzer(execPath) {
			this->fileName = _tcsdup(fileName);
		}


	};
}
