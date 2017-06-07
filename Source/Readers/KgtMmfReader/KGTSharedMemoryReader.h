
//#define COPY_MINIBATCH_MEMORY

#include "SequenceEnumerator.h"
#include "TextConfigHelper.h"
#include "KGTUtilities.h"

using namespace Microsoft::MSR::CNTK;
using namespace KGT::Utilities;

namespace KGT {	namespace Readers {

//			class MiniBatchOfSingleStream
//			{
//			public:
//				MiniBatchOfSingleStream() { }
//
//				MiniBatchOfSingleStream(size_t miniBatchSize, size_t singleVectorSize)
//					:
//					m_miniBatchSize(miniBatchSize),
//					m_singleVectorSize(singleVectorSize),
//					m_buffer(miniBatchSize, vector<float>(singleVectorSize, 0))
//					//m_buffer = vector<vector<float>>(miniBatchSize, vector<float>(singleVectorSize, 0));
//				{ }
//
//				std::vector<float *> * CreateSharedMatrixPtr()
//				{
//					for (int idx = 0; idx < this->m_miniBatchSize; idx++)
//					{
//						m_bufferPtrsVector.push_back(&(m_buffer[idx][0]));
//					}
//
//					return &m_bufferPtrsVector;
//				}
//
//				float *GetDataPtrOfNthVector(int n)
//				{
//					return m_buffer[n].data();
//				}
//
//#ifdef COPY_MINIBATCH_MEMORY
//
//#pragma warning(push)
//#pragma warning(disable : 4996) // std::copy doesn't check the length of passed buffer and so can write past the boundaries
//				float *GetDataCopyOfNthVector(int n)
//				{
//					float *buffer = new float[m_buffer[n].size()];
//					std::copy(m_buffer[n].begin(), m_buffer[n].end(), buffer);
//					return buffer;
//				}
//#pragma warning(pop)
//
//#endif
//
//			private:
//				size_t m_miniBatchSize;
//				size_t m_singleVectorSize;
//				vector<vector<float>> m_buffer;
//				//need to keep this so that the matrix in manged code is still valid
//				std::vector<float *> m_bufferPtrsVector;
//			};

			class KGTSharedMemoryReader : public SequenceEnumerator
			{
			public:
				KGTSharedMemoryReader(const ConfigParameters& config)
					:KGTSharedMemoryReader(TextConfigHelper(config))
				{ }

				// Describes streams the sequence enumerator produces.
				virtual std::vector<StreamDescriptionPtr> GetStreamDescriptions() const override;

				// Sets current epoch configuration.
				virtual void StartEpoch(const EpochConfiguration& config) override;

				// Sets current configuration.
				virtual void SetConfiguration(const ReaderConfiguration& config) override;

				// Gets next sequences up to a maximum count of samples.
				virtual Sequences GetNextSequences(size_t globalSampleCount, size_t localSampleCount) override;

				virtual void SetCurrentSamplePosition(size_t currentSamplePosition) override;

				// Returns current position in the global timeline. The returned value is in samples.
				virtual size_t GetCurrentSamplePosition() override;

				virtual ~KGTSharedMemoryReader();
			private:
				//this is just temporary workaround until we have proper config parsing
				KGTSharedMemoryReader(const TextConfigHelper& helper);

				// Streams this data deserializer can produce.
				std::vector<StreamDescriptionPtr> m_streams;

				//std::vector<MiniBatchOfSingleStream> m_sharedMinibatches;
				std::vector<KGT::Utilities::SharedBuffer *> _sharedBuffers;
				KGT::Utilities::CrossProcessSignal _buffersRequestSignal;
				KGT::Utilities::CrossProcessSignal _buffersResponseSignal;
				KGT::Utilities::CrossProcessSignal _resetReadingAnnouncedSignal;
				KGT::Utilities::CrossProcessSignal _resetReadingDoneSignal;

				// Epoch configuration
				EpochConfiguration m_config;

				size_t m_miniBatchSize = 0;

				void InitializeBuffersIfNeeded(size_t miniBatchSize);

				size_t m_samplesSentInThisEpoch;
				int m_minibatchesSentInThisEpoch;

				size_t m_totalNumberOfSamplesPerSingleEpoch;

				StopWatch _epochTotalDuration;
				StopWatch _epochDataReadingDuration;
				StopWatch _epochNativeReaderDuration;

				struct KGTDenseInputStreamBuffer : DenseSequenceData
				{
					// capacity = expected number of samples * sample size
					KGTDenseInputStreamBuffer(float const * const dataPtr)
						:m_dataPtr(dataPtr)
					{ }

					const void* GetDataBuffer() override
					{
						return m_dataPtr;
					}

#ifdef COPY_MINIBATCH_MEMORY

					//This is needed if we copy the internal buffer
					~KGTDenseInputStreamBuffer() override
					{
						delete(m_dataPtr);
					}

#endif

				private:

					float const * const m_dataPtr;
				};

				struct SharedBufferInitializationInfo
				{
					SharedBufferInitializationInfo(std::string vectorName, size_t vectorSize)
						:m_vectorName(vectorName), m_vectorSize(vectorSize)
					{ }

					std::string m_vectorName;
					size_t m_vectorSize;
				};

				std::vector<SharedBufferInitializationInfo> _buffersInitializationInfo;
				string _sharedName;
			};

} }
