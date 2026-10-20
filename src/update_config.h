#pragma once

#include <filesystem>

#include "updater/update_controller.h"

namespace crackdown {

trg::update::UpdateConfig MakeUpdateConfig(const std::filesystem::path& install_dir);

}  // namespace crackdown
