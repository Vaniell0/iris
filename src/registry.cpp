/// @file   src/registry.cpp

#include <registry.hpp>
#include <mutex>

namespace iris {

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string jni_sig_for(PrimitiveKind k) {
    switch (k) {
        case PrimitiveKind::Bool:  return "Z";
        case PrimitiveKind::I8:    return "B";
        case PrimitiveKind::I16:   return "S";
        case PrimitiveKind::I32:   return "I";
        case PrimitiveKind::I64:   return "J";
        case PrimitiveKind::F32:   return "F";
        case PrimitiveKind::F64:   return "D";
        case PrimitiveKind::Str:   return "Ljava/lang/String;";
        case PrimitiveKind::Bytes: return "Ljava/lang/Object;";
        default:                   return "";
    }
}

// ── Singleton ─────────────────────────────────────────────────────────────────

TypeRegistry& TypeRegistry::global() {
    static TypeRegistry instance;
    return instance;
}

// ── Registration ──────────────────────────────────────────────────────────────

TypeId TypeRegistry::register_type(TypeDescriptor desc) {
    if (frozen_.load(std::memory_order_relaxed)) return 0;

    desc.id = compute_type_id(desc.name, desc.fields);
    desc.java_fields.resize(desc.fields.size(), nullptr);

    std::unique_lock lock(mu_);
    if (types_.contains(desc.id)) return desc.id;

    TypeId id = desc.id;
    by_name_[desc.name] = id;
    types_.emplace(id, std::move(desc));
    return id;
}

TypeId TypeRegistry::from_fields(std::string_view name,
                                  std::vector<FieldDesc> fields,
                                  size_t total_size) {
    if (frozen_.load(std::memory_order_relaxed)) return 0;

    TypeDescriptor desc;
    desc.name       = std::string(name);
    desc.fields     = std::move(fields);
    desc.total_size = total_size;
    if (desc.total_size == 0)
        for (auto& f : desc.fields) desc.total_size += f.size;
    return register_type(std::move(desc));
}

// ── Lookup ────────────────────────────────────────────────────────────────────

const TypeDescriptor* TypeRegistry::find(TypeId id) const {
    std::shared_lock lock(mu_);
    auto it = types_.find(id);
    return it != types_.end() ? &it->second : nullptr;
}

const TypeDescriptor* TypeRegistry::find(std::string_view n) const {
    std::shared_lock lock(mu_);
    auto it = by_name_.find(std::string(n));
    if (it == by_name_.end()) return nullptr;
    auto jt = types_.find(it->second);
    return jt != types_.end() ? &jt->second : nullptr;
}

bool TypeRegistry::contains(TypeId id) const {
    std::shared_lock lock(mu_);
    return types_.contains(id);
}

// ── Inspection ────────────────────────────────────────────────────────────────

std::optional<TypeInspection> TypeRegistry::inspect(TypeId id) const {
    std::shared_lock lock(mu_);
    auto it = types_.find(id);
    if (it == types_.end()) return std::nullopt;

    const auto& desc = it->second;
    TypeInspection result;
    result.id                 = desc.id;
    result.name               = desc.name;
    result.total_size         = desc.total_size;
    result.java_handles_cached = (desc.java_class != nullptr);

    result.fields.reserve(desc.fields.size());
    for (auto& f : desc.fields) {
        result.fields.push_back(FieldInspection{
            .name    = f.name,
            .kind    = f.kind,
            .offset  = f.offset,
            .size    = f.size,
            .jni_sig = jni_sig_for(f.kind),
        });
    }
    return result;
}

// ── Freeze ────────────────────────────────────────────────────────────────────

void TypeRegistry::freeze() {
    frozen_.store(true, std::memory_order_release);
}

} // namespace iris
