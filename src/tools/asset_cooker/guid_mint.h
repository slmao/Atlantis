#pragma once

#include <atlantis/asset_system/guid_mint.h>

namespace atlantis::tools::asset_cooker {

// Plan 0065 J8 (ADR-0130 D1): the mint moved to Atlantis Asset System
// (atlantis/asset_system/guid_mint.h), still the only minting code. These
// names keep the cooker's --kind=mint-guid call site and its tests
// (tests/tools/asset_cooker/guid_mint_tests.cpp) unchanged; nothing is
// implemented here.
using atlantis::asset_system::mintAssetGuid;
using atlantis::asset_system::mintAssetGuids;

}  // namespace atlantis::tools::asset_cooker
