//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//
// Exports.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#define DATAREADER_EXPORTS
#include "DataReader.h"
#include "ReaderShim.h"
#include "KgtMmfReader.h"
#include "HeapMemoryProvider.h"
#include "StringUtil.h"

using namespace Microsoft::MSR::CNTK;

namespace KGT { namespace Readers {

// TODO: Memory provider should be injected by SGD.

auto factory = [](const ConfigParameters& parameters) -> ReaderPtr
{
    return std::make_shared<KgtMmfReader>(parameters);
};

extern "C" DATAREADER_API void GetReaderF(IDataReader** preader)
{
    *preader = new ReaderShim<float>(factory);
}

extern "C" DATAREADER_API void GetReaderD(IDataReader** preader)
{
    *preader = new ReaderShim<double>(factory);
}

// TODO: Not safe from the ABI perspective. Will be uglified to make the interface ABI.
// A factory method for creating text deserializers.
extern "C" DATAREADER_API bool CreateDeserializer(IDataDeserializer**, const std::wstring&, const ConfigParameters&, CorpusDescriptorPtr, bool)
{
	InvalidArgument("CreateDeserializer unsupported in KgtMmfReader (this reader is already bundling randomization and minibatching)");
}


}}
