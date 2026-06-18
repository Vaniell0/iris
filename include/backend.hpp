/// @file   include/backend.hpp
/// @brief  Backend concept and C ABI entry point for .so plugins.

#pragma once

#include <sdk/cpp/types.hpp>
#include <value.hpp>
#include <concepts>
#include <string_view>

namespace iris {

/// A backend satisfies this concept — no base class, no vtable overhead.
/// static_assert(Backend<MyBackend>) verifies at compile time.
template<typename T>
concept Backend = requires(T b, IrisValue v, const TypeDescriptor& td) {
    { b.runtime_name() } -> std::convertible_to<std::string_view>;
    { b.can_handle(td)  } -> std::convertible_to<bool>;
    { b.emit(std::move(v)) } -> std::same_as<void>;
    { b.recv()          } -> std::same_as<IrisValue>;
};

/// C ABI entry point exported by every .so plugin.
/// Iris dlopen()s the plugin and resolves `iris_create_backend`.
extern "C" {
    struct IrisBackendHandle {
        std::string_view (*runtime_name)(void* self);
        bool             (*can_handle)(void* self, const TypeDescriptor*);
        void             (*emit)(void* self, IrisValue*);
        IrisValue        (*recv)(void* self);
        void             (*destroy)(void* self);
        void*            self;
    };
}

} // namespace iris
