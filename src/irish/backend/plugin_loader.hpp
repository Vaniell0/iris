#pragma once
#include "backend_registry.hpp"
#include <string>
#include <vector>

namespace iris::irsh {

/// Scan plugin_dir for *.so files, dlopen each, verify, and register with registry.
/// If plugin_dir is empty, defaults to ~/.iris/plugins/.
/// Returns one error string per failed plugin (empty = all OK).
/// Does NOT call registry.freeze() — caller does that after load_plugins returns.
std::vector<std::string> load_plugins(BackendRegistry& registry,
                                      const std::string& plugin_dir = "");

} // namespace iris::irsh
