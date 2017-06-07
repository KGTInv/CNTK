//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//

#pragma once

#include "TextParser.h"
#include "ReaderBase.h"

namespace KGT { namespace Readers {
// TODO: Should be deprecated, use composite reader instead.
// Implementation of the text reader.
// Effectively the class represents a factory for connecting the packer,
// transformers and the deserializer together.
class KgtMmfReader : public Microsoft::MSR::CNTK::ReaderBase
{
public:
	KgtMmfReader(const Microsoft::MSR::CNTK::ConfigParameters& parameters);
};

}}
