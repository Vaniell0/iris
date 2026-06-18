/// @file   src/iris_backend_handle.cpp
/// @brief  Type-erased destroy for the Iris C ABI.
///
/// Each factory sets handle->_destroy_impl. This file knows nothing about
/// the concrete Impl type — no JNI, no OS headers required.

#include <sdk/iris_backend.h>

extern "C" {

void iris_backend_destroy(iris_backend_t* handle) {
    if (!handle) return;
    if (handle->_destroy_impl) handle->_destroy_impl(handle->impl);
    delete handle;
}

} // extern "C"
