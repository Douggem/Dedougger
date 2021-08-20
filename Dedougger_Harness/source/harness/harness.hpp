#pragma once
#include <memory>
#include <string>

#include "dedougger.hpp"
using namespace dedougger;

class DedouggerHarness {
	UP_Dedougger dedougger;

public:
	DedouggerHarness(DWORD pid);
	DedouggerHarness(std::string executablePath) {
		this->dedougger = std::make_unique<Dedougger>(executablePath);
	}
};
