// SPDX-License-Identifier: MIT
/// @file   sdk/iris.hpp
/// @brief  Iris SDK — umbrella header for type registration and value helpers.
///
/// Include this single header to get everything needed to describe types,
/// register them with the global TypeRegistry, wrap/unwrap C++ structs
/// into IrisValue, and use IRIS_TYPE / IRIS_FIELD macros.

#pragma once

#include <sdk/types.hpp>
#include <sdk/macros.hpp>
