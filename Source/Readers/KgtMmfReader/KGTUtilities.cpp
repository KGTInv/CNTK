
#include "stdafx.h"
#include "KGTUtilities.h"

namespace KGT { namespace Utilities {

	/*
	* Global utilities
	*
	*/

	void Fail(std::stringstream& errMsg)
	{
		errMsg << "\r\n";
		//Carefull! We cannot join it in on statement - like this
		// char const* cstr_errMsg = errMsg.str().c_str();
		// as destructor of str() would be called prior calling c_str() and we'd hold garbage
		std::string str = errMsg.str();
		char const* cstr_errMsg = str.c_str();
		fprintf(stderr, cstr_errMsg);
		fflush(stderr);
		//sleep for a second to give buffers and redirections chance to flush and propagate
		Sleep(1000);
		throw std::exception(cstr_errMsg);
	}

	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

	std::wstring StringToWString(std::string str)
	{
		return converter.from_bytes(str);
	}

	std::string WstringToString(std::wstring wstr)
	{
		return converter.to_bytes(wstr);
	}

	bool FileExists(const std::string& name)
	{
		if (FILE *file = fopen(name.c_str(), "r"))
		{
			fclose(file);
			return true;
		}
		else
		{
			return false;
		}
	}

	bool FileExists(const std::wstring& wname)
	{
		return FileExists(WstringToString(wname));
	}

	/*
	* End of global utilities
	*
	*/

	/*
	 * CrossProcessSignal
	 *
	 */

	void CrossProcessSignal::Initialize(std::string signalName)
	{
		_signalName = signalName;

		//bufferRequestEvent = CreateEvent(
		//	NULL,               // default security attributes
		//	FALSE,               // auto-reset event
		//	FALSE,              // initial state is nonsignaled
		//	(requestEvtName + sharedName).c_str()  // object name
		//);
		_eventHandle = OpenEvent(
			EVENT_MODIFY_STATE | SYNCHRONIZE,  // access
			FALSE,               // inherit handle
			StringToWString(signalName).c_str()  // object name
		);

		if (_eventHandle == NULL)
		{
			std::stringstream errMsg;
			errMsg << "CreateEvent for event [" << _signalName << "]  failed. GLE: " << GetLastError();
			Fail(errMsg);
		}
	}

	void CrossProcessSignal::SetSignal() const
	{
		if (!SetEvent(_eventHandle))
		{
			std::stringstream errMsg;
			errMsg << "SetEvent for event [" << _signalName << "]  failed. GLE: " << GetLastError();
			Fail(errMsg);
		}
	}

	void CrossProcessSignal::WaitForSignal() const
	{
		DWORD dwWaitResult = WaitForSingleObject(
			_eventHandle, // event handle
			INFINITE);    // indefinite wait

		if (dwWaitResult != WAIT_OBJECT_0)
		{
			std::stringstream errMsg;
			errMsg << "Wait error for event [" << _signalName << "]. GLE: " << GetLastError();
			Fail(errMsg);
		}
	}

	CrossProcessSignal::~CrossProcessSignal()
	{
		if (_eventHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(_eventHandle);
			_eventHandle = INVALID_HANDLE_VALUE;
		}
	}

	/*
	* End of CrossProcessSignal
	*
	*/

	/*
	* SharedBuffer
	*
	*/


	void SharedBuffer::Initialize(std::string mmfName, unsigned int singleLineSize, unsigned int reservedLinesCount)
	{
		_singleLineSize = singleLineSize;
		_reservedLinesCount = reservedLinesCount;
		_mmfName = mmfName;

		_mmfHandle = OpenFileMapping(
			FILE_MAP_READ,   // access
			FALSE,                 // do not inherit the name
			StringToWString(mmfName).c_str());

		if (_mmfHandle == NULL)
		{
			std::stringstream errMsg;
			errMsg << "OpenFileMapping on [" << mmfName << "] failed GLE: " << GetLastError();
			Fail(errMsg);
		}

		size_t mmfSize = 2 * sizeof(unsigned int) + singleLineSize * reservedLinesCount * sizeof(float);

		_mappedFileBufferPtr = MapViewOfFile(_mmfHandle, // handle to map object
			FILE_MAP_READ,  //  permission
			0,   // high DWORD
			0,   // low DWOR of an offset in the file
			mmfSize);

		if (_mappedFileBufferPtr == NULL)
		{
			std::stringstream errMsg;
			errMsg << "MapViewOfFile on [" << mmfName << "] failed GLE: " << GetLastError();
			Fail(errMsg);
		}

		_versionPtr = static_cast<unsigned int *>(_mappedFileBufferPtr);
		_linesCountPtr = (_versionPtr + 1);
		_dataPtr = (float *)(_linesCountPtr + 1);
		_expectedVersion = 0;

		if (*_linesCountPtr != reservedLinesCount)
		{
			std::stringstream errMsg;
			errMsg << "Shared buffer [" << mmfName << "]  has unexpected size info: " << *_linesCountPtr << ", expected: " << reservedLinesCount;
			Fail(errMsg);
		}
	}

	unsigned int SharedBuffer::GetReservedLinesCount() const
	{
		return _reservedLinesCount;
	}

	unsigned int SharedBuffer::GetSingleLineSize() const
	{
		return _singleLineSize;
	}

	void SharedBuffer::CheckAndIncrementExpectedVersion()
	{
		if (_expectedVersion != *_versionPtr)
		{
			std::stringstream errMsg;
			errMsg << "Expected version of buffer [" << _mmfName << "]: " << _expectedVersion << ". Actual: " << *_versionPtr;
			Fail(errMsg);
		}
		_expectedVersion++;
	}

	void SharedBuffer::VerifyLinesWritten(unsigned int *samplesPopulated) const
	{
		if (*_linesCountPtr > _reservedLinesCount)
		{
			std::stringstream errMsg;
			errMsg << "Lines written into buffer [" << _mmfName << "]: " << *_linesCountPtr << ". Max capacity: " << _reservedLinesCount;
			Fail(errMsg);
		}

		if(*samplesPopulated == 0)
		{
			*samplesPopulated = *_linesCountPtr;
		}
		else if(*samplesPopulated != *_linesCountPtr)
		{
			std::stringstream errMsg;
			errMsg << "Lines written into [" << _mmfName << "] buffer: " << *_linesCountPtr << ". Expected (other buffer read): " << *samplesPopulated;
			Fail(errMsg);
		}
	}

	unsigned int SharedBuffer::GetLinesWritten() const
	{
		return *_linesCountPtr;
	}

	float const * const SharedBuffer::GetBufferPtr() const
	{
		return _dataPtr;
	}

	float const * const SharedBuffer::GetDataPtrOfNthVector(int rowIdZeroBased) const
	{
		return _dataPtr + rowIdZeroBased * _singleLineSize;
	}

	float const * const SharedBuffer::GetDataCopyOfNthVector(int rowIdZeroBased) const
	{
		float *buffer = new float[_singleLineSize];
		memcpy(buffer, GetDataPtrOfNthVector(rowIdZeroBased), _singleLineSize);
		return buffer;
	}

	SharedBuffer::~SharedBuffer()
	{
		if (_mappedFileBufferPtr != NULL)
		{
			UnmapViewOfFile(_mappedFileBufferPtr);
			_mappedFileBufferPtr = NULL;
		}
		if (_mmfHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(_mmfHandle);
			_mmfHandle = INVALID_HANDLE_VALUE;
		}
	}

	/*
	* End of SharedBuffer
	*
	*/

	/*
	* StopWatch
	*
	*/

	LONGLONG StopWatch::QueryPerformanceCounterFrequency()
	{
		LARGE_INTEGER freq;
		::QueryPerformanceFrequency(&freq);
		return freq.QuadPart;
	}

	LONGLONG StopWatch::_performanceCounterFrequency = StopWatch::QueryPerformanceCounterFrequency();

	void StopWatch::Start()
	{
		if (_lastStartTicks == 0)
		{
			LARGE_INTEGER start;
			::QueryPerformanceCounter(&start);
			_lastStartTicks = start.QuadPart;
		}
	}

	void StopWatch::Stop()
	{
		if (_lastStartTicks != 0)
		{
			LARGE_INTEGER end;
			::QueryPerformanceCounter(&end);
			_totalDurationTicks += end.QuadPart - _lastStartTicks;
			_lastStartTicks = 0;
		}
	}

	void StopWatch::Reset()
	{
		_lastStartTicks = 0;
		_totalDurationTicks = 0;
	}

	double StopWatch::GetElapsedSeconds() const
	{
		LONGLONG elapsed = _totalDurationTicks;

		if(_lastStartTicks != 0)
		{
			LARGE_INTEGER end;
			::QueryPerformanceCounter(&end);
			elapsed += end.QuadPart - _lastStartTicks;
		}

		return static_cast<double>(elapsed) / _performanceCounterFrequency;
	}

	/*
	* End of StopWatch
	*
	*/
} }