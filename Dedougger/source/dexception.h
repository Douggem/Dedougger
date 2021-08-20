#pragma once
#include <exception>
#include <stdexcept>

namespace dedougger {
	class UnknownProtectionException : public std::exception {};
	class VirtualQueryFailedException : public std::exception {};
	class VirtualAllocFailedException : public std::exception {};
	class VirtualFreeFailedException : public std::exception {};
	class VirtualProtectFailedException : public std::exception {};
	class DuplicateHandleFailedException : public std::exception {};
	class AttemptToRestoreUnTrackedThread : public std::exception {};
	class OpenThreadFailedException :public std::exception {};
	class WriteProcessMemoryFailedException :public std::exception {};
	class ReadProcessMemoryFailedException :public std::exception {};
	class DebugActiveProcessFailedException :public std::exception {};
	class OpenProcessFailedException :public std::exception {};
	class SetThreadContextFailedException :public std::exception {};

}