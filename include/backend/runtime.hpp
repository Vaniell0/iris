/// @file   include/backend/runtime.hpp
/// @brief  Process-wide JVM singleton — one JavaVM per process.
///
/// Wraps JNI_CreateJavaVM / JNI_GetCreatedJavaVMs so that any number of
/// JavaBackend instances share a single JVM without coordination. Thread-safe
/// at acquire() time; reads after acquisition need no lock (jvm_ is immutable
/// once set).

#pragma once

#include <value.hpp>
#include <expected>
#include <mutex>
#include <string>
#include <jni.h>

namespace iris {

class RuntimeManager {
    JavaVM*    jvm_      = nullptr;
    bool       owns_jvm_ = false;
    std::mutex mu_;

    RuntimeManager() = default;

public:
    static RuntimeManager& global() {
        static RuntimeManager inst;
        return inst;
    }

    RuntimeManager(const RuntimeManager&)            = delete;
    RuntimeManager& operator=(const RuntimeManager&) = delete;

    /// Return the live JVM, creating one if none exists yet.
    /// Thread-safe; subsequent calls return the same pointer.
    std::expected<JavaVM*, IrisError> acquire(const char* classpath = nullptr) {
        std::lock_guard lock(mu_);
        if (jvm_) return jvm_;

        jsize count = 0;
        JNI_GetCreatedJavaVMs(&jvm_, 1, &count);
        if (count > 0) return jvm_;

        JavaVMInitArgs args{};
        args.version            = JNI_VERSION_1_8;
        args.ignoreUnrecognized = JNI_FALSE;

        JavaVMOption opts[1]{};
        std::string  cp_opt;
        if (classpath) {
            cp_opt               = std::string("-Djava.class.path=") + classpath;
            opts[0].optionString = cp_opt.data();
            args.nOptions        = 1;
            args.options         = opts;
        }

        JNIEnv* env = nullptr;
        if (JNI_CreateJavaVM(&jvm_, reinterpret_cast<void**>(&env), &args) != JNI_OK)
            return std::unexpected(IrisError::JniException);

        owns_jvm_ = true;
        return jvm_;
    }

    JavaVM* jvm() const noexcept { return jvm_; }

    /// Return the JNIEnv for the calling thread, attaching if needed.
    JNIEnv* env() {
        if (!jvm_) return nullptr;
        JNIEnv* e = nullptr;
        jint rc = jvm_->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_8);
        if (rc == JNI_EDETACHED)
            jvm_->AttachCurrentThread(reinterpret_cast<void**>(&e), nullptr);
        return e;
    }
};

} // namespace iris
