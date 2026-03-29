#pragma once

#include <UIlib.h>
#include <string>
#include <vector>

#include "common/archive_types.h"

namespace MultiThreadedInstaller {

void InitializeComponentBindingsUI(DuiLib::CTabLayoutUI* tabPages,
                                   const ExtendedInstallationMetadata& metadata,
                                   std::vector<std::string>& warnings);

std::vector<std::string> CollectSelectedComponentIdsFromUI(DuiLib::CTabLayoutUI* tabPages,
                                                           const ExtendedInstallationMetadata& metadata,
                                                           std::vector<std::string>& warnings);

} // namespace MultiThreadedInstaller
