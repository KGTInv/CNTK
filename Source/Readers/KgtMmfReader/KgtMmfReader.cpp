//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//

#include "stdafx.h"

/////////////////////////////////////////////////////////////
//
//  THIS IS BASICALLY ADPTED COPY OF CNTKTextFormatReader
/////////////////////////////////////////////////////////////

//needed due to std::copy usage in KGTExperimentalDirectMemoryAccessReader.h
#define _SCL_SECURE_NO_WARNINGS

#include "KgtMmfReader.h"
#include "Config.h"
#include "TextConfigHelper.h"
#include "BlockRandomizer.h"
#include "SequencePacker.h"
#include "FramePacker.h"

//#include "KGTExperimentalReader.h"
//#include "KGTExperimentalDirectMemoryAccessReader.h"
#include "KGTSharedMemoryReader.h"
#include "ProgressTracing.h"


namespace KGT {
	namespace Readers {

		using namespace std;

		KgtMmfReader::KgtMmfReader(const ConfigParameters& config)
		{
			TextConfigHelper configHelper(config);

			try
			{
				m_sequenceEnumerator = std::make_shared<KGT::Readers::KGTSharedMemoryReader>(config);

				if (configHelper.IsInFrameMode())
				{
					m_packer = std::make_shared<FramePacker>(
						m_sequenceEnumerator,
						ReaderBase::GetStreamDescriptions());
				}
				else
				{
					m_packer = std::make_shared<SequencePacker>(
						m_sequenceEnumerator,
						ReaderBase::GetStreamDescriptions());
				}
			}
			catch (const std::runtime_error& e)
			{
				RuntimeError("KgtMmfReader: While reading shared objects '%ls': %s", configHelper.GetSharedInMemoryObjectsNamespace().c_str(), e.what());
			}
		}
	}
}
