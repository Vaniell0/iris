/// @file   src/backend/java/java_backend.hpp
/// @brief  JVM backend — bridges IrisValue between C raw bytes and Java objects.
///
/// Owns all JNI lifecycle details (JavaVM*, jclass/jmethodID/jfieldID caching,
/// global reference management). The core knows nothing about JNI — it only
/// sees OpaqueHandle with a release callback set by this backend.
/// The Backend concept is satisfied; the static_assert below verifies this.

#pragma once

#include <backend.hpp>
#include <channel.hpp>
#include <registry.hpp>
#include <runtime_manager.hpp>
#include <expected>
#include <string>
#include <jni.h>

namespace iris {

class JavaBackend {
    JavaVM*  jvm_      = nullptr;
    bool     owns_jvm_ = false;
    Channel* in_       = nullptr;  ///< source channel — recv() reads from here
    Channel* out_      = nullptr;  ///< sink channel   — emit() writes here

    JNIEnv* attach();
    bool    check(JNIEnv* env, std::string_view ctx = "");
    bool    ensure_handles(JNIEnv* env, const TypeDescriptor& desc,
                           const std::string& jni_class_name);

    static std::string to_jni_class_name(const std::string& canonical);

public:
    /// Connect to an existing JVM in the process or create a new one.
    /// A non-null @p classpath is passed as -Djava.class.path= when creating.
    std::expected<void, IrisError> connect(const char* classpath = nullptr);
    void disconnect();

    ~JavaBackend() { disconnect(); }

    // ── Channel wiring ────────────────────────────────────────────────────────

    /// Connect a channel that recv() will drain.
    void set_input(Channel* ch)  { in_  = ch; }
    /// Connect a channel that emit() will fill.
    void set_output(Channel* ch) { out_ = ch; }

    // ── Backend concept ───────────────────────────────────────────────────────

    std::string_view runtime_name() const { return "jvm"; }
    bool             can_handle(const TypeDescriptor& td) const;
    void             emit(IrisValue&& v);
    IrisValue        recv();

    // ── Dry-run inspection ────────────────────────────────────────────────────

    /// Per-field mapping result — computed from TypeDescriptor alone, no JNI.
    struct FieldMapping {
        std::string name;
        std::string jni_sig;
        bool        mappable;   ///< false for Void/Bytes — no JNI setter exists
        std::string reason;     ///< non-empty when !mappable
    };

    /// Result of analysing a bridge without executing it.
    struct BridgeDryRun {
        TypeId                   type_id;
        std::string              class_name;
        bool                     class_cached;   ///< jclass already resolved
        std::vector<FieldMapping> fields;
        size_t                   mappable_count;
        size_t                   skipped_count;
    };

    /// Inspect what c_to_java / java_to_c would do for this type —
    /// reads only the TypeDescriptor, never touches the JVM.
    BridgeDryRun dry_run(TypeId id) const;

    // ── Type discovery ────────────────────────────────────────────────────────

    /// Reflect a Java class into a TypeDescriptor and register it globally.
    /// Walks getDeclaredFields() — inherited fields are excluded.
    /// Returns 0 if the class is not found or a JNI error occurs.
    TypeId register_class(JNIEnv* env, std::string_view class_name);

    // ── Core bridges ─────────────────────────────────────────────────────────

    /// Produce a live Java object from C raw bytes using the TypeDescriptor
    /// to map field offsets → JNI SetXField calls.
    std::expected<IrisValue, IrisError> c_to_java(const IrisValue& c_val);

    /// Extract C raw bytes from a Java object using GetXField per field.
    /// @p target_c_type may be 0 to reuse the source TypeId.
    std::expected<IrisValue, IrisError> java_to_c(const IrisValue& java_val,
                                                   TypeId target_c_type = 0);

    /// Full round-trip: c_to_java → optional static Java method call → java_to_c.
    /// When @p java_method is empty the value passes through Java unchanged.
    std::expected<IrisValue, IrisError> pipe(const IrisValue& c_val,
                                              std::string_view java_method = "");

    /// Call a static Java method with an IrisValue argument.
    /// @p class_name  JNI class name (slashes, e.g. "java/lang/String")
    /// @p method      static method name
    /// @p method_sig  JNI method descriptor, e.g. "(Ljava/lang/Object;)Ljava/lang/String;"
    /// @p arg         value passed as the sole argument (must be opaque, i.e. a Java object)
    std::expected<IrisValue, IrisError> invoke(std::string_view class_name,
                                               std::string_view method,
                                               std::string_view method_sig,
                                               const IrisValue& arg);

    JavaVM*       jvm()      { return jvm_; }
    JNIEnv*       env()      { return attach(); }
    TypeRegistry& registry() { return TypeRegistry::global(); }
};

static_assert(Backend<JavaBackend>);

} // namespace iris
