#pragma once

#include <exception>
#include <stdint.h>

namespace dedougger {
	class InvalidHWBPIndexException : public std::exception {};

	enum BPCONDITION {
		EXECUTION = 0,
		DATA_WRITE = 1,
		DATA_READ_WRITE = 3
	};

	enum BPLEN {
		ONE = 0,
		TWO = 1,
		FOUR  = 3,
		EIGHT = 2
	};

	union Dr7_Fields {
		struct {
			unsigned int Dr0_Local : 1;		// 0
			unsigned int Dr0_Global : 1;	// 1
			unsigned int Dr1_Local : 1;		// 2
			unsigned int Dr1_Global : 1;	// 3
			unsigned int Dr2_Local : 1;		// 4
			unsigned int Dr2_Global : 1;	// 5
			unsigned int Dr3_Local : 1;		// 6
			unsigned int Dr3_Global : 1;	// 7
			unsigned int LE : 1;			// 8
			unsigned int GE : 1;			// 9
			unsigned int bit10 : 1;			// 10
			int Reserved : 5;				// 11
			BPCONDITION Dr0_Condition : 2;	// 16
			BPLEN Dr0_Len : 2;				// 18
			BPCONDITION Dr1_Condition : 2;	// 20
			BPLEN Dr1_Len : 2;				// 22
			BPCONDITION Dr2_Condition : 2;	// 24
			BPLEN Dr2_Len : 2;				// 26
			BPCONDITION Dr3_Condition : 2;	// 28
			BPLEN Dr3_Len : 2;				// 30
		} fields;
		uint32_t int32;
	};

	class HWBPDescriptor {
	public:
		size_t address;
		BPCONDITION condition;
		BPLEN len;
		bool enabled;
	};
	
	class HWBPRegisterState {
		size_t		dr0;
		size_t		dr1;
		size_t		dr2;
		size_t		dr3;
		Dr7_Fields	dr7;

	public:
		HWBPRegisterState() {
			this->dr0		= 0;
			this->dr1		= 0;
			this->dr2		= 0;
			this->dr3		= 0;
			this->dr7.int32 = 0;
		}

		void WriteHWBPToState(const HWBPDescriptor *hwbp, int index) {
			switch (index) {
			case(0):
				this->dr7.fields.Dr0_Condition	= hwbp->condition;
				this->dr7.fields.Dr0_Local		= 1;
				this->dr7.fields.Dr0_Global		= 0;
				this->dr7.fields.Dr0_Len		= hwbp->len;
				this->dr0						= hwbp->address;
				break;
			case(1):
				this->dr7.fields.Dr1_Condition = hwbp->condition;
				this->dr7.fields.Dr1_Local = 1;
				this->dr7.fields.Dr1_Len = hwbp->len;
				this->dr1 = hwbp->address;
				break;
			case(2):
				this->dr7.fields.Dr2_Condition = hwbp->condition;
				this->dr7.fields.Dr2_Local = 1;
				this->dr7.fields.Dr2_Len = hwbp->len;
				this->dr2 = hwbp->address;
				break;
			case(3):
				this->dr7.fields.Dr3_Condition = hwbp->condition;
				this->dr7.fields.Dr3_Local = 1;
				this->dr7.fields.Dr3_Len = hwbp->len;
				this->dr3 = hwbp->address;
				break;
			default:
				throw(InvalidHWBPIndexException());
				break;
			}
		}

		void DisableHWBP(int index) {
			switch (index) {
			case(0):
				this->dr7.fields.Dr0_Local = 0;
				break;
			case(1):
				this->dr7.fields.Dr1_Local = 0;
				break;
			case(2):
				this->dr7.fields.Dr2_Local = 0;
				break;
			case(3):
				this->dr7.fields.Dr3_Local = 0;
				break;
			default:
				throw(InvalidHWBPIndexException());
				break;
			}
		}

		void EnsableHWBP(int index) {
			switch (index) {
			case(0):
				this->dr7.fields.Dr0_Local = 1;
				break;
			case(1):
				this->dr7.fields.Dr1_Local = 1;
				break;
			case(2):
				this->dr7.fields.Dr2_Local = 1;
				break;
			case(3):
				this->dr7.fields.Dr3_Local = 1;
				break;
			default:
				throw(InvalidHWBPIndexException());
				break;
			}
		}

		/* Field getters */
		Dr7_Fields	Dr7() { return this->dr7; }
		size_t		Dr0() { return this->dr0; }
		size_t		Dr1() { return this->dr1; }
		size_t		Dr2() { return this->dr2; }
		size_t		Dr3() { return this->dr3; }
	};
}