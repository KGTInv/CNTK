
#include "stdafx.h"
#include "KGTSharedMemoryReader.h"
#include "ProgressTracing.h"
#include "DataReader.h"

using namespace KGT::Utilities;

namespace KGT {
	namespace Readers {

		const string _kgtReaderVersion = "0.1";

#ifdef COPY_MINIBATCH_MEMORY
		const string _subVersion = "MemoryCopying";
#else
		const string _subVersion = "MemorySharing";
#endif

		//Ad 'Global/' for cross session visibility (e.g service to interactive; user to user)
		const std::string requestEvtName = "bufferRequestEvent_";
		const std::string responseEvtName = "bufferResponseEvent_";
		const std::string resetReadingAnnounceEvtName = "resetAnnounceEvent_";
		const std::string resetReadingDoneEvtName = "resetDoneEvent_";
		const std::string mmf = "mmf_";


		//this is just temporary workaround until we have proper config parsing
		KGTSharedMemoryReader::KGTSharedMemoryReader(const TextConfigHelper& configHelper)
		{
			cerr << "----------- Initializing KGTSharedMemoryReader (Version: " << _kgtReaderVersion << " (" << _subVersion << "); build: " << __DATE__ << " " << __TIME__ << ") -----------------------";

			_sharedName = KGT::Utilities::WstringToString(configHelper.GetSharedInMemoryObjectsNamespace());

			_buffersRequestSignal.Initialize(requestEvtName + _sharedName);
			_buffersResponseSignal.Initialize(responseEvtName + _sharedName);
			_resetReadingAnnouncedSignal.Initialize(resetReadingAnnounceEvtName + _sharedName);
			_resetReadingDoneSignal.Initialize(resetReadingDoneEvtName + _sharedName);

			//Adopted from TestParser ctor
			const vector<StreamDescriptor>& streams = configHelper.GetStreams();

			assert(streams.size() > 0);
			m_totalNumberOfSamplesPerSingleEpoch = 0;

			//m_fileReaders.reserve(streams.size());
			for (size_t i = 0; i < streams.size(); ++i)
			{
				const StreamDescriptor& stream = streams[i];
				auto streamDescription = std::make_shared<StreamDescription>(stream);
				streamDescription->m_sampleLayout = std::make_shared<TensorShape>(stream.m_sampleDimension);
				this->m_streams.push_back(streamDescription);

				this->_buffersInitializationInfo.push_back(SharedBufferInitializationInfo(stream.m_alias, stream.m_sampleDimension));
			}
		}

		void KGTSharedMemoryReader::InitializeBuffersIfNeeded(size_t miniBatchSize)
		{
			if (this->m_miniBatchSize > 0)
			{
				return;
			}

			this->m_miniBatchSize = miniBatchSize;

			for (auto const & bufferInfo : this->_buffersInitializationInfo)
			{
				SharedBuffer *sharedBuffer = new SharedBuffer();
				//Careful!! - the sharedBuffer is copied here - value type would be copied and thrown away
				//    alternative would be to use smart ptrs (ordinary ptrs would not guarantee lifetime of pointed objects)
				this->_sharedBuffers.push_back(sharedBuffer);

				sharedBuffer->Initialize(
					mmf + bufferInfo.m_vectorName + "_" + _sharedName, 
					static_cast<unsigned int>(bufferInfo.m_vectorSize),
					static_cast<unsigned int>(miniBatchSize));
			}
		}

		void KGTSharedMemoryReader::SetConfiguration(const ReaderConfiguration& config)
		{
			*((ReaderConfiguration*)&m_config) = config;

			if (m_config.m_numberOfWorkers > 1)
			{
				std::stringstream errMsg;
				errMsg << "Number of workers :" << m_config.m_numberOfWorkers << ". This is just to explicitly see that we are in parallel training scenario and to make sure that we are code-ready";
				throw exception(errMsg.str().c_str());
			}

			InitializeBuffersIfNeeded(config.m_minibatchSizeInSamples);

			// TODO: should be removed.
			// Currently no restriction on the epoch size at all when SetConfiguration is used.
			m_config.m_totalEpochSizeInSamples = std::numeric_limits<size_t>().max() / 2; // Make sure we do not exceed size_t
			m_config.m_epochIndex = 0;
		}

		void KGTSharedMemoryReader::SetCurrentSamplePosition(size_t currentSamplePosition)
		{
			if (m_totalNumberOfSamplesPerSingleEpoch != 0 && currentSamplePosition != 0 &&
				currentSamplePosition % m_totalNumberOfSamplesPerSingleEpoch != 0)
			{
				std::stringstream errMsg;
				errMsg << "Attempt to set sample position to [" << currentSamplePosition << "]. This is not aligned to samples count in single epoch: " << m_config.m_totalEpochSizeInSamples;
				throw exception(errMsg.str().c_str());
			}

			//reset
			this->StartEpoch(this->m_config);
		}

		// Returns current position in the global timeline. The returned value is in samples.
		size_t KGTSharedMemoryReader::GetCurrentSamplePosition()
		{
			return m_samplesSentInThisEpoch + m_totalNumberOfSamplesPerSingleEpoch * m_config.m_epochIndex;
		}

		std::vector<StreamDescriptionPtr> KGTSharedMemoryReader::GetStreamDescriptions() const
		{
			return this->m_streams;
		}

		bool _flushToFile = false;
		FILE *flushStream;

		void KGTSharedMemoryReader::StartEpoch(const EpochConfiguration& config)
		{
			SetConfiguration(config);
			this->m_config = config;
			m_samplesSentInThisEpoch = 0;
			m_minibatchesSentInThisEpoch = 0;

			_epochTotalDuration.Reset();
			_epochDataReadingDuration.Reset();
			_epochNativeReaderDuration.Reset();
			_epochTotalDuration.Start();

			LOGPRINTF(stderr, "KGTSharedMemoryReader: Next epoch started");

			//This is ugly - but it is already designed this way in other readers 
			// (m_totalEpochSizeInSamples is not intialized prior first StartEpoch)
			if (m_config.m_totalEpochSizeInSamples == requestDataSize && m_totalNumberOfSamplesPerSingleEpoch != 0)
			{
				m_config.m_totalEpochSizeInSamples = m_totalNumberOfSamplesPerSingleEpoch;
			}

			if(_flushToFile)
			{
				flushStream = fopen((string("CntkInputFile_") + _sharedName).c_str(), "w");
			}

			this->_resetReadingAnnouncedSignal.SetSignal();
			this->_resetReadingDoneSignal.WaitForSignal();
		}

		double _nextLogTimeStamp = 0;

		//Global is for all (distributed) workers
		//Local is for current worker (m_globalSequencePosition % m_config.m_numberOfWorkers == m_config.m_workerRank)
		Sequences KGTSharedMemoryReader::GetNextSequences(size_t globalSampleCount, size_t localSampleCount)
		{
			_epochDataReadingDuration.Start();
			_epochNativeReaderDuration.Start();

			Sequences result;

			//asserts are not part of release
			if (globalSampleCount == 0)
				LogicError("Global sample count must not be zero.");

			if (localSampleCount == 0)
				LogicError("Local sample count must not be zero.");

			
			//Following is the communication with managed code
			for(auto &buffer: _sharedBuffers)
			{
				buffer->CheckAndIncrementExpectedVersion();
			}
			
			_epochNativeReaderDuration.Stop();

			_buffersRequestSignal.SetSignal();
			_buffersResponseSignal.WaitForSignal();

			_epochNativeReaderDuration.Start();

			unsigned int samplesPopulated = 0;
			for (auto &buffer : _sharedBuffers)
			{
				buffer->VerifyLinesWritten(&samplesPopulated);
			}

			//in case we'd need counter for actually send samples (in parallel env.), than we also need a counter 
			//  for samples in current epoch across all workers - this is because we do not have knowledge of samples count per epoch
			m_samplesSentInThisEpoch += samplesPopulated;

			if (samplesPopulated <= 0)
			{
				result.m_endOfEpoch = true;

				if (_flushToFile)
				{
					fclose(flushStream);
				}

				_epochDataReadingDuration.Stop();
				_epochNativeReaderDuration.Stop();
				_epochTotalDuration.Stop();

				double durationReaderInSecondsTotal = _epochDataReadingDuration.GetElapsedSeconds();
				double durationReaderInSecondsExclusive = _epochNativeReaderDuration.GetElapsedSeconds();
				double epochDurationInSeconds = _epochTotalDuration.GetElapsedSeconds();

				LOGPRINTF(stderr, "KGTSharedMemoryReader: Next epoch finished. Total time [s]: %f Total reader (c#) inclusive time [s]: %f (%.2f %%), Total reader exclusive (c++ only) time [s]: %f (%.2f %%)",
					epochDurationInSeconds,
					durationReaderInSecondsTotal,
					(durationReaderInSecondsTotal / epochDurationInSeconds) * 100.0,
					durationReaderInSecondsExclusive,
					(durationReaderInSecondsExclusive / epochDurationInSeconds) * 100.0);
				fflush(stderr);

				if (m_totalNumberOfSamplesPerSingleEpoch <= 0)
				{
					m_totalNumberOfSamplesPerSingleEpoch = m_samplesSentInThisEpoch;
				}
				else if (m_samplesSentInThisEpoch != m_totalNumberOfSamplesPerSingleEpoch)
				{
					LOGPRINTF(stderr, "Unexpected divergence in number of samples sent in this epoch (%zu) compared to number of samples sent in first epoch (%zu)",
						m_samplesSentInThisEpoch, m_totalNumberOfSamplesPerSingleEpoch);
					if (abs((int)(m_samplesSentInThisEpoch - m_totalNumberOfSamplesPerSingleEpoch)) > 0.1 * m_totalNumberOfSamplesPerSingleEpoch)
					{
						LogicError("Unexpected divergence (over 10%) in number of samples sent in this epoch (%zu) compared to number of samples sent in first epoch (%zu)",
							m_samplesSentInThisEpoch, m_totalNumberOfSamplesPerSingleEpoch);
					}
				}

				//singla to C# that we are completely done
				_buffersRequestSignal.SetSignal();
				return result;
			}

			result.m_data.resize(m_streams.size(), std::vector<SequenceDataPtr>(samplesPopulated));

			int streamIdx = 0;
			for (auto const & stream : m_streams)
			{
				SharedBuffer *sharedBuffer = this->_sharedBuffers[streamIdx];
				if(stream->m_sampleLayout->GetNumElements() != sharedBuffer->GetSingleLineSize())
				{
					std::stringstream errMsg;
					errMsg << "shared buffer line size: " << sharedBuffer->GetSingleLineSize() << ". Expected stream layout size: " << stream->m_sampleLayout->GetNumElements();
					Fail(errMsg);
				}

				for (size_t rowIdZeroBased = 0; rowIdZeroBased < samplesPopulated; rowIdZeroBased++)
				{
					//not supporting other than dense samples
					KGTDenseInputStreamBuffer *denseBuffer =
						new KGTDenseInputStreamBuffer(sharedBuffer->
#ifdef COPY_MINIBATCH_MEMORY
							//TODO: we do COPY here! - this is very suboptimal; but let's try to see the results
							GetDataCopyOfNthVector((int)rowIdZeroBased)
#else
							GetDataPtrOfNthVector((int)rowIdZeroBased)
#endif
						);

					//metadata
					denseBuffer->m_sampleLayout = stream->m_sampleLayout;
					//denseBuffer->m_id = rowIdZeroBased;
					++denseBuffer->m_numberOfSamples;
					result.m_data[streamIdx][rowIdZeroBased] = unique_ptr<KGTDenseInputStreamBuffer>(denseBuffer);
				}

				streamIdx++;
			}

			if(_flushToFile)
			{
				SharedBuffer *targetsBuffer = this->_sharedBuffers[1];
				SharedBuffer *featuressBuffer = this->_sharedBuffers[0];

				for (size_t rowIdZeroBased = 0; rowIdZeroBased < samplesPopulated; rowIdZeroBased++)
				{
					fprintf(flushStream, "|target ");
					targetsBuffer->PrintRowToFile((int)rowIdZeroBased, flushStream);
					fprintf(flushStream, "|features ");
					featuressBuffer->PrintRowToFile((int)rowIdZeroBased, flushStream);
					//carefull - only \n - as in text mode all LF are converted to CRLF
					fprintf(flushStream, "\n");
				}
			}

			if (_epochTotalDuration.GetElapsedSeconds() > _nextLogTimeStamp || samplesPopulated < this->m_miniBatchSize)
			{
				LOGPRINTF(stderr, "KGTSharedMemoryReader: Next Minibatch (%d) received and translated (records: %d)", m_minibatchesSentInThisEpoch, samplesPopulated);
				fflush(stderr);

				_nextLogTimeStamp = _epochTotalDuration.GetElapsedSeconds() + 5;
			}

			m_minibatchesSentInThisEpoch++;

			_epochDataReadingDuration.Stop();
			_epochNativeReaderDuration.Stop();

			return result;
		}

		KGTSharedMemoryReader::~KGTSharedMemoryReader()
		{

		}

} }
