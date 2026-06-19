#pragma once
#include <sdk/cpp/types.hpp>
#include <string>
#include <variant>

namespace iris::irsh {

// irsh-level types — what the type checker assigns to every expression.
// Distinct from PrimitiveKind (engine scalar) and TypeDescriptor (engine struct).

struct VoidType    {};
struct AnyType     {};                               // TypeId unknown at parse time
struct ScalarType  { PrimitiveKind kind; };          // I32, CStr, Bool, ...
struct StructType  { TypeId id; std::string name; }; // DirEntry, Commit, ...
struct StreamType  { TypeId elem_id; };              // LazyStream<T>
struct VecType     { TypeId elem_id; };              // Vec<T> after collect
struct TextLineType{};                               // @os.exec output
struct AliasType   { /* pipeline fragment — input/output types TBD */ };

using IrType = std::variant<
    VoidType, AnyType, ScalarType, StructType, StreamType, VecType, TextLineType, AliasType
>;

inline bool is_stream(const IrType& t) {
    return std::holds_alternative<StreamType>(t) ||
           std::holds_alternative<TextLineType>(t) ||
           std::holds_alternative<AnyType>(t);
}

inline bool is_wire_safe(TypeId id, const struct TypeDescriptor* desc);

} // namespace iris::irsh
