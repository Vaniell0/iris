/// @file   src/backend/java/java_backend.cpp

#include "java_backend.hpp"
#include <cstring>

namespace iris {

// ── JVM lifecycle ────────────────────────────────────────────────────────────

std::expected<void, IrisError> JavaBackend::connect(const char* classpath) {
    /// Reuse an existing JVM if one is already live in this process —
    /// only one JavaVM is allowed per process by the JNI specification.
    jsize count = 0;
    JNI_GetCreatedJavaVMs(&jvm_, 1, &count);
    if (count > 0) return {};

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
    return {};
}

void JavaBackend::disconnect() {
    if (jvm_ && owns_jvm_) {
        jvm_->DestroyJavaVM();
        jvm_      = nullptr;
        owns_jvm_ = false;
    }
}

// ── Internal helpers ─────────────────────────────────────────────────────────

JNIEnv* JavaBackend::attach() {
    if (!jvm_) return nullptr;
    JNIEnv* env = nullptr;
    jint rc = jvm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8);
    if (rc == JNI_EDETACHED)
        jvm_->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr);
    return env;
}

bool JavaBackend::check(JNIEnv* env, std::string_view) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionDescribe();
    env->ExceptionClear();
    return true;
}

std::string JavaBackend::to_jni_class_name(const std::string& canonical) {
    std::string s = canonical;
    for (char& c : s) if (c == '.') c = '/';
    return s;
}

/// Called by OpaqueHandle destructor to release the JVM global reference.
static void release_java_ref(void* ptr, void* ctx) {
    auto*   jvm = static_cast<JavaVM*>(ctx);
    JNIEnv* env = nullptr;
    jvm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr);
    if (env) env->DeleteGlobalRef(static_cast<jobject>(ptr));
}

/// Populate jclass + jmethodID(ctor) + jfieldID[] in the descriptor.
/// Results are cached — subsequent calls on the same descriptor are no-ops.
bool JavaBackend::ensure_handles(JNIEnv* env, const TypeDescriptor& desc,
                                  const std::string& jni_name) {
    if (desc.java_class && desc.java_ctor) return true;

    jclass cls = desc.java_class
        ? reinterpret_cast<jclass>(desc.java_class)
        : env->FindClass(jni_name.c_str());

    if (check(env) || !cls) return false;

    if (!desc.java_class) {
        desc.java_class = env->NewGlobalRef(cls);
        env->DeleteLocalRef(cls);
        cls = reinterpret_cast<jclass>(desc.java_class);
    }

    if (!desc.java_ctor) {
        jmethodID ctor = env->GetMethodID(cls, "<init>", "()V");
        if (check(env) || !ctor) return false;
        desc.java_ctor = ctor;
    }

    desc.java_fields.resize(desc.fields.size(), nullptr);
    for (size_t i = 0; i < desc.fields.size(); ++i) {
        if (desc.java_fields[i]) continue;
        auto& f = desc.fields[i];

        std::string sig;
        switch (f.kind) {
            case PrimitiveKind::I8:   sig = "B"; break;
            case PrimitiveKind::I16:  sig = "S"; break;
            case PrimitiveKind::I32:  sig = "I"; break;
            case PrimitiveKind::I64:  sig = "J"; break;
            case PrimitiveKind::F32:  sig = "F"; break;
            case PrimitiveKind::F64:  sig = "D"; break;
            case PrimitiveKind::Bool: sig = "Z"; break;
            case PrimitiveKind::Str:  sig = "Ljava/lang/String;"; break;
            default:                  sig = "Ljava/lang/Object;"; break;
        }

        jfieldID fid = env->GetFieldID(cls, f.effective_jni_name().c_str(), sig.c_str());
        if (check(env)) return false;
        desc.java_fields[i] = fid;
    }
    return true;
}

// ── Type discovery ────────────────────────────────────────────────────────────

TypeId JavaBackend::register_class(JNIEnv* env, std::string_view class_name) {
    auto& reg = TypeRegistry::global();
    if (auto* existing = reg.find(class_name)) return existing->id;

    std::string jni_name(class_name);
    for (char& c : jni_name) if (c == '.') c = '/';

    jclass cls = env->FindClass(jni_name.c_str());
    if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
    if (!cls) return 0;

    jclass    class_cls    = env->FindClass("java/lang/Class");
    jclass    field_cls    = env->FindClass("java/lang/reflect/Field");
    jmethodID get_fields   = env->GetMethodID(class_cls, "getDeclaredFields",
                                               "()[Ljava/lang/reflect/Field;");
    jmethodID get_name     = env->GetMethodID(field_cls, "getName", "()Ljava/lang/String;");
    jmethodID get_type     = env->GetMethodID(field_cls, "getType", "()Ljava/lang/Class;");
    jmethodID get_mods     = env->GetMethodID(field_cls, "getModifiers", "()I");
    jmethodID cls_name     = env->GetMethodID(class_cls, "getName", "()Ljava/lang/String;");
    if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }

    constexpr jint STATIC = 8; // java.lang.reflect.Modifier.STATIC

    auto field_arr = reinterpret_cast<jobjectArray>(env->CallObjectMethod(cls, get_fields));
    if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }

    jint n = env->GetArrayLength(field_arr);
    std::vector<FieldDesc> fields;
    fields.reserve(static_cast<size_t>(n));
    size_t offset = 0;

    for (jint i = 0; i < n; ++i) {
        jobject jfield = env->GetObjectArrayElement(field_arr, i);

        jint mods = env->CallIntMethod(jfield, get_mods);
        if (mods & STATIC) { env->DeleteLocalRef(jfield); continue; }

        auto        jname  = reinterpret_cast<jstring>(env->CallObjectMethod(jfield, get_name));
        const char* cn     = env->GetStringUTFChars(jname, nullptr);
        std::string fname(cn);
        env->ReleaseStringUTFChars(jname, cn);

        jobject     ftype  = env->CallObjectMethod(jfield, get_type);
        auto        jtname = reinterpret_cast<jstring>(env->CallObjectMethod(ftype, cls_name));
        const char* ctn    = env->GetStringUTFChars(jtname, nullptr);
        std::string tname(ctn);
        env->ReleaseStringUTFChars(jtname, ctn);

        PrimitiveKind kind = PrimitiveKind::Void;
        size_t        fsz  = 0;
        if      (tname == "int"     || tname == "java.lang.Integer") { kind = PrimitiveKind::I32;  fsz = 4; }
        else if (tname == "long"    || tname == "java.lang.Long"   ) { kind = PrimitiveKind::I64;  fsz = 8; }
        else if (tname == "double"  || tname == "java.lang.Double" ) { kind = PrimitiveKind::F64;  fsz = 8; }
        else if (tname == "float"   || tname == "java.lang.Float"  ) { kind = PrimitiveKind::F32;  fsz = 4; }
        else if (tname == "boolean" || tname == "java.lang.Boolean") { kind = PrimitiveKind::Bool; fsz = 1; }
        else if (tname == "short"   || tname == "java.lang.Short"  ) { kind = PrimitiveKind::I16;  fsz = 2; }
        else if (tname == "byte"    || tname == "java.lang.Byte"   ) { kind = PrimitiveKind::I8;   fsz = 1; }
        else if (tname == "java.lang.String")                         { kind = PrimitiveKind::Str;  fsz = sizeof(void*); }
        else                                                           { kind = PrimitiveKind::Bytes; fsz = sizeof(void*); }

        fields.push_back(FieldDesc{ .name = fname, .kind = kind,
                                    .offset = offset, .size = fsz, .jni_name = "" });
        offset += fsz;
        env->DeleteLocalRef(jfield);
        env->DeleteLocalRef(ftype);
    }
    env->DeleteLocalRef(field_arr);

    TypeDescriptor desc;
    desc.name       = std::string(class_name);
    desc.fields     = std::move(fields);
    desc.total_size = offset;
    desc.java_class = env->NewGlobalRef(cls);
    desc.java_fields.resize(desc.fields.size(), nullptr);
    env->DeleteLocalRef(cls);

    return reg.register_type(std::move(desc));
}

// ── C → Java ─────────────────────────────────────────────────────────────────

std::expected<IrisValue, IrisError> JavaBackend::c_to_java(const IrisValue& c_val) {
    auto* desc = TypeRegistry::global().find(c_val.type_id);
    if (!desc)            return std::unexpected(IrisError::TypeNotFound);
    if (!c_val.is_raw())  return std::unexpected(IrisError::SizeMismatch);

    JNIEnv* env = attach();
    if (!env) return std::unexpected(IrisError::JniException);

    if (!ensure_handles(env, *desc, to_jni_class_name(desc->name)))
        return std::unexpected(IrisError::JniClassNotFound);

    auto cls  = reinterpret_cast<jclass>(desc->java_class);
    auto ctor = reinterpret_cast<jmethodID>(desc->java_ctor);

    jobject obj = env->NewObject(cls, ctor);
    if (check(env) || !obj) return std::unexpected(IrisError::JniException);

    const auto& raw = c_val.raw();
    for (size_t i = 0; i < desc->fields.size(); ++i) {
        auto& f   = desc->fields[i];
        auto  fid = reinterpret_cast<jfieldID>(desc->java_fields[i]);
        if (!fid || f.offset + f.size > raw.size()) continue;

        const void* src = raw.data() + f.offset;
        switch (f.kind) {
            case PrimitiveKind::I32:  { int32_t  v; std::memcpy(&v, src, 4); env->SetIntField(obj,     fid, v); break; }
            case PrimitiveKind::I64:  { int64_t  v; std::memcpy(&v, src, 8); env->SetLongField(obj,    fid, v); break; }
            case PrimitiveKind::F32:  { float    v; std::memcpy(&v, src, 4); env->SetFloatField(obj,   fid, v); break; }
            case PrimitiveKind::F64:  { double   v; std::memcpy(&v, src, 8); env->SetDoubleField(obj,  fid, v); break; }
            case PrimitiveKind::Bool: { uint8_t  v; std::memcpy(&v, src, 1); env->SetBooleanField(obj, fid, v); break; }
            case PrimitiveKind::I16:  { int16_t  v; std::memcpy(&v, src, 2); env->SetShortField(obj,   fid, v); break; }
            case PrimitiveKind::I8:   { int8_t   v; std::memcpy(&v, src, 1); env->SetByteField(obj,    fid, v); break; }
            default: break;
        }
        if (check(env)) return std::unexpected(IrisError::JniException);
    }

    IrisValue result;
    result.type_id = c_val.type_id;
    result.payload = OpaqueHandle(env->NewGlobalRef(obj), jvm_, release_java_ref);
    env->DeleteLocalRef(obj);
    return result;
}

// ── Java → C ─────────────────────────────────────────────────────────────────

std::expected<IrisValue, IrisError>
JavaBackend::java_to_c(const IrisValue& java_val, TypeId target_c_type) {
    if (!java_val.is_opaque()) return std::unexpected(IrisError::SizeMismatch);

    TypeId dst_id = target_c_type ? target_c_type : java_val.type_id;
    auto*  desc   = TypeRegistry::global().find(dst_id);
    if (!desc) return std::unexpected(IrisError::TypeNotFound);

    JNIEnv* env = attach();
    if (!env) return std::unexpected(IrisError::JniException);

    if (!ensure_handles(env, *desc, to_jni_class_name(desc->name)))
        return std::unexpected(IrisError::JniClassNotFound);

    jobject                obj = static_cast<jobject>(java_val.opaque().ptr);
    std::vector<std::byte> raw(desc->total_size, std::byte{0});

    for (size_t i = 0; i < desc->fields.size(); ++i) {
        auto& f   = desc->fields[i];
        auto  fid = reinterpret_cast<jfieldID>(desc->java_fields[i]);
        if (!fid || f.offset + f.size > raw.size()) continue;

        void* dst = raw.data() + f.offset;
        switch (f.kind) {
            case PrimitiveKind::I32:  { int32_t  v = env->GetIntField(obj,     fid); std::memcpy(dst, &v, 4); break; }
            case PrimitiveKind::I64:  { int64_t  v = env->GetLongField(obj,    fid); std::memcpy(dst, &v, 8); break; }
            case PrimitiveKind::F32:  { float    v = env->GetFloatField(obj,   fid); std::memcpy(dst, &v, 4); break; }
            case PrimitiveKind::F64:  { double   v = env->GetDoubleField(obj,  fid); std::memcpy(dst, &v, 8); break; }
            case PrimitiveKind::Bool: { jboolean v = env->GetBooleanField(obj, fid); std::memcpy(dst, &v, 1); break; }
            case PrimitiveKind::I16:  { int16_t  v = env->GetShortField(obj,   fid); std::memcpy(dst, &v, 2); break; }
            case PrimitiveKind::I8:   { int8_t   v = env->GetByteField(obj,    fid); std::memcpy(dst, &v, 1); break; }
            default: break;
        }
        if (check(env)) return std::unexpected(IrisError::JniException);
    }

    IrisValue result;
    result.type_id = dst_id;
    result.payload = std::move(raw);
    return result;
}

// ── Dry-run inspection ────────────────────────────────────────────────────────

JavaBackend::BridgeDryRun JavaBackend::dry_run(TypeId id) const {
    BridgeDryRun result{};
    result.type_id = id;

    const TypeDescriptor* desc = TypeRegistry::global().find(id);
    if (!desc) return result;

    result.class_name   = desc->name;
    result.class_cached = (desc->java_class != nullptr);

    for (const auto& f : desc->fields) {
        FieldMapping fm;
        fm.name = f.name;

        switch (f.kind) {
            case PrimitiveKind::I8:   fm.jni_sig = "B"; fm.mappable = true; break;
            case PrimitiveKind::I16:  fm.jni_sig = "S"; fm.mappable = true; break;
            case PrimitiveKind::I32:  fm.jni_sig = "I"; fm.mappable = true; break;
            case PrimitiveKind::I64:  fm.jni_sig = "J"; fm.mappable = true; break;
            case PrimitiveKind::F32:  fm.jni_sig = "F"; fm.mappable = true; break;
            case PrimitiveKind::F64:  fm.jni_sig = "D"; fm.mappable = true; break;
            case PrimitiveKind::Bool: fm.jni_sig = "Z"; fm.mappable = true; break;
            case PrimitiveKind::Str:
                fm.jni_sig  = "Ljava/lang/String;";
                fm.mappable = true;
                break;
            case PrimitiveKind::Bytes:
                fm.mappable = false;
                fm.reason   = "no JNI setter for Bytes";
                break;
            case PrimitiveKind::Void:
                fm.mappable = false;
                fm.reason   = "no JNI setter for Void";
                break;
            default:
                fm.mappable = false;
                fm.reason   = "unknown kind";
                break;
        }

        if (fm.mappable) ++result.mappable_count;
        else             ++result.skipped_count;

        result.fields.push_back(std::move(fm));
    }

    return result;
}

// ── Backend concept stubs ─────────────────────────────────────────────────────

bool      JavaBackend::can_handle(const TypeDescriptor& td) const { return jvm_ && !td.name.empty(); }
void      JavaBackend::emit(IrisValue&& v) { if (out_) out_->push(std::move(v)); }
IrisValue JavaBackend::recv()              { return in_ ? in_->pop() : IrisValue{}; }

} // namespace iris
