#pragma once
#include <stdint.h>
namespace dedougger {
	class SWBP {
		size_t  address;
		uint8_t overwrittenByte;
		bool    protectPage;
		bool    replaceInst;
	public:
		SWBP() {
			this->address = 0;
			this->overwrittenByte = 0;
			this->protectPage = 0;
			this->replaceInst = 0;					   		
		}

		SWBP(size_t address, uint8_t overwrittenByte, bool protectPage, bool replaceInst) {
			this->address = address;
			this->overwrittenByte = overwrittenByte;
			this->protectPage = protectPage;
			this->replaceInst = replaceInst;
		}

		SWBP(SWBP& other) {
			this->address = other.address;
			this->overwrittenByte = other.overwrittenByte;
			this->protectPage = other.protectPage;
			this->replaceInst = other.replaceInst;
		}

		const size_t Address() const { return this->address; }
		const uint8_t OverwrittenByte() const { return this->overwrittenByte; }
		const bool ProtectPage() const { return this->protectPage; }
		const bool ReplaceInst() const { return this->replaceInst; }
	};
}