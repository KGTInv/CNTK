#pragma once
#include "stdafx.h"
#include <windows.h>
#include <string>
#include <iostream>
#include <sstream>
#include <codecvt>

namespace KGT {	namespace Utilities {
		
	void Fail(std::stringstream& errMsg);

	std::wstring StringToWString(std::string str);
	std::string WstringToString(std::wstring wstr);

	bool FileExists(const std::string& name);
	bool FileExists(const std::wstring& wname);

	class CrossProcessSignal
	{
	public:
		CrossProcessSignal()
			:_eventHandle(INVALID_HANDLE_VALUE)
		{  }

		//separating this form constructor - so we have guarantee of dtor called
		void Initialize(std::string signalName);

		void SetSignal() const;

		void WaitForSignal() const;

		~CrossProcessSignal();
	private:
		std::string _signalName;
		HANDLE _eventHandle;
	};

	class SharedBuffer
	{
	public:

		SharedBuffer()
			:_mappedFileBufferPtr(NULL), _mmfHandle(INVALID_HANDLE_VALUE)
		{  }

		void PrintRowToFile(int rowIdZeroBased, FILE *flushStream) const
		{
			const float* nums = this->GetDataPtrOfNthVector(rowIdZeroBased);
			for (unsigned int idx = 0; idx < this->_singleLineSize; idx++)
			{
				fprintf(flushStream, "%f ", nums[idx]);
			}
		}

		//separating this form constructor - so we have guarantee of dtor called
		void Initialize(std::string mmfName, unsigned int singleLineSize, unsigned int reservedLinesCount);

		unsigned int GetReservedLinesCount() const;

		unsigned int GetSingleLineSize() const;

		void CheckAndIncrementExpectedVersion();

		void VerifyLinesWritten(unsigned int *samplesPopulated) const;

		unsigned int GetLinesWritten() const;

		float const * const GetBufferPtr() const;

		float const * const GetDataPtrOfNthVector(int rowIdZeroBased) const;

		float const * const GetDataCopyOfNthVector(int rowIdZeroBased) const;

		~SharedBuffer();

	private:

		std::string _mmfName;
		HANDLE _mmfHandle;
		void *_mappedFileBufferPtr;
		unsigned int const * _versionPtr;
		unsigned int const * _linesCountPtr;
		float const *_dataPtr;
		unsigned int _expectedVersion;
		unsigned int _reservedLinesCount;
		unsigned int _singleLineSize;
	};


	class StopWatch
	{
	public:
		StopWatch() 
			: _totalDurationTicks(0), _lastStartTicks(0)
		{  }

		void Start();

		void Stop();

		void Reset();

		double GetElapsedSeconds() const;

		static LONGLONG QueryPerformanceCounterFrequency();

	private:

		static LONGLONG _performanceCounterFrequency;
		LONGLONG _totalDurationTicks;
		LONGLONG _lastStartTicks;
	};

} }
