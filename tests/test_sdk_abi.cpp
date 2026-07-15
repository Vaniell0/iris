/// @file   tests/test_sdk_abi.cpp
/// @brief  Pin the SDK C ABI constants to the C++ PrimitiveKind enum.
///
/// Three headers redeclare the primitive-kind ordering for different consumers:
///   - sdk/cpp/types.hpp        (enum class iris::PrimitiveKind — the truth)
///   - sdk/iris_registry.h      (iris_kind_t   — for registry C ABI callers)
///   - sdk/irsh_backend.h       (iris_prim_kind_t — for irsh plugin ABI)
///
/// These must agree numerically or a plugin compiled against one header will
/// misclassify fields registered against another. This test freezes every
/// value at both compile time (static_assert) and run time (EXPECT_EQ), and
/// verifies that TypeIds computed via the C ABI match those via the C++ API.

#include <gtest/gtest.h>

#include <sdk/cpp/types.hpp>
#include <sdk/iris_registry.h>
#include <sdk/irsh_backend.h>

using iris::PrimitiveKind;

// ── Compile-time pins ─────────────────────────────────────────────────────────

static_assert(static_cast<int>(PrimitiveKind::Void)  == 0);
static_assert(static_cast<int>(PrimitiveKind::Bool)  == 1);
static_assert(static_cast<int>(PrimitiveKind::I8)    == 2);
static_assert(static_cast<int>(PrimitiveKind::I16)   == 3);
static_assert(static_cast<int>(PrimitiveKind::I32)   == 4);
static_assert(static_cast<int>(PrimitiveKind::I64)   == 5);
static_assert(static_cast<int>(PrimitiveKind::F32)   == 6);
static_assert(static_cast<int>(PrimitiveKind::F64)   == 7);
static_assert(static_cast<int>(PrimitiveKind::Str)   == 8);
static_assert(static_cast<int>(PrimitiveKind::Bytes) == 9);
static_assert(static_cast<int>(PrimitiveKind::CStr)  == 10);

static_assert(IRIS_KIND_VOID  == static_cast<int>(PrimitiveKind::Void));
static_assert(IRIS_KIND_BOOL  == static_cast<int>(PrimitiveKind::Bool));
static_assert(IRIS_KIND_I8    == static_cast<int>(PrimitiveKind::I8));
static_assert(IRIS_KIND_I16   == static_cast<int>(PrimitiveKind::I16));
static_assert(IRIS_KIND_I32   == static_cast<int>(PrimitiveKind::I32));
static_assert(IRIS_KIND_I64   == static_cast<int>(PrimitiveKind::I64));
static_assert(IRIS_KIND_F32   == static_cast<int>(PrimitiveKind::F32));
static_assert(IRIS_KIND_F64   == static_cast<int>(PrimitiveKind::F64));
static_assert(IRIS_KIND_STR   == static_cast<int>(PrimitiveKind::Str));
static_assert(IRIS_KIND_BYTES == static_cast<int>(PrimitiveKind::Bytes));
static_assert(IRIS_KIND_CSTR  == static_cast<int>(PrimitiveKind::CStr));

static_assert(IRIS_PRIM_VOID  == static_cast<int>(PrimitiveKind::Void));
static_assert(IRIS_PRIM_BOOL  == static_cast<int>(PrimitiveKind::Bool));
static_assert(IRIS_PRIM_I8    == static_cast<int>(PrimitiveKind::I8));
static_assert(IRIS_PRIM_I16   == static_cast<int>(PrimitiveKind::I16));
static_assert(IRIS_PRIM_I32   == static_cast<int>(PrimitiveKind::I32));
static_assert(IRIS_PRIM_I64   == static_cast<int>(PrimitiveKind::I64));
static_assert(IRIS_PRIM_F32   == static_cast<int>(PrimitiveKind::F32));
static_assert(IRIS_PRIM_F64   == static_cast<int>(PrimitiveKind::F64));
static_assert(IRIS_PRIM_STR   == static_cast<int>(PrimitiveKind::Str));
static_assert(IRIS_PRIM_BYTES == static_cast<int>(PrimitiveKind::Bytes));
static_assert(IRIS_PRIM_CSTR  == static_cast<int>(PrimitiveKind::CStr));

// ── Runtime pins (guard against silent header divergence in dependent builds) ─

TEST(SdkAbi, RegistryKindsMatchPrimitiveKind) {
    EXPECT_EQ(IRIS_KIND_VOID,  static_cast<int>(PrimitiveKind::Void));
    EXPECT_EQ(IRIS_KIND_BOOL,  static_cast<int>(PrimitiveKind::Bool));
    EXPECT_EQ(IRIS_KIND_I8,    static_cast<int>(PrimitiveKind::I8));
    EXPECT_EQ(IRIS_KIND_I16,   static_cast<int>(PrimitiveKind::I16));
    EXPECT_EQ(IRIS_KIND_I32,   static_cast<int>(PrimitiveKind::I32));
    EXPECT_EQ(IRIS_KIND_I64,   static_cast<int>(PrimitiveKind::I64));
    EXPECT_EQ(IRIS_KIND_F32,   static_cast<int>(PrimitiveKind::F32));
    EXPECT_EQ(IRIS_KIND_F64,   static_cast<int>(PrimitiveKind::F64));
    EXPECT_EQ(IRIS_KIND_STR,   static_cast<int>(PrimitiveKind::Str));
    EXPECT_EQ(IRIS_KIND_BYTES, static_cast<int>(PrimitiveKind::Bytes));
    EXPECT_EQ(IRIS_KIND_CSTR,  static_cast<int>(PrimitiveKind::CStr));
}

TEST(SdkAbi, IrshPluginKindsMatchPrimitiveKind) {
    EXPECT_EQ(IRIS_PRIM_VOID,  static_cast<int>(PrimitiveKind::Void));
    EXPECT_EQ(IRIS_PRIM_BOOL,  static_cast<int>(PrimitiveKind::Bool));
    EXPECT_EQ(IRIS_PRIM_I8,    static_cast<int>(PrimitiveKind::I8));
    EXPECT_EQ(IRIS_PRIM_I16,   static_cast<int>(PrimitiveKind::I16));
    EXPECT_EQ(IRIS_PRIM_I32,   static_cast<int>(PrimitiveKind::I32));
    EXPECT_EQ(IRIS_PRIM_I64,   static_cast<int>(PrimitiveKind::I64));
    EXPECT_EQ(IRIS_PRIM_F32,   static_cast<int>(PrimitiveKind::F32));
    EXPECT_EQ(IRIS_PRIM_F64,   static_cast<int>(PrimitiveKind::F64));
    EXPECT_EQ(IRIS_PRIM_STR,   static_cast<int>(PrimitiveKind::Str));
    EXPECT_EQ(IRIS_PRIM_BYTES, static_cast<int>(PrimitiveKind::Bytes));
    EXPECT_EQ(IRIS_PRIM_CSTR,  static_cast<int>(PrimitiveKind::CStr));
}

TEST(SdkAbi, RegistryAndIrshHeadersAgree) {
    EXPECT_EQ(IRIS_KIND_VOID,  IRIS_PRIM_VOID);
    EXPECT_EQ(IRIS_KIND_BOOL,  IRIS_PRIM_BOOL);
    EXPECT_EQ(IRIS_KIND_I8,    IRIS_PRIM_I8);
    EXPECT_EQ(IRIS_KIND_I16,   IRIS_PRIM_I16);
    EXPECT_EQ(IRIS_KIND_I32,   IRIS_PRIM_I32);
    EXPECT_EQ(IRIS_KIND_I64,   IRIS_PRIM_I64);
    EXPECT_EQ(IRIS_KIND_F32,   IRIS_PRIM_F32);
    EXPECT_EQ(IRIS_KIND_F64,   IRIS_PRIM_F64);
    EXPECT_EQ(IRIS_KIND_STR,   IRIS_PRIM_STR);
    EXPECT_EQ(IRIS_KIND_BYTES, IRIS_PRIM_BYTES);
    EXPECT_EQ(IRIS_KIND_CSTR,  IRIS_PRIM_CSTR);
}

// Two clients (one holding the C ABI values, one holding the C++ enum) must
// hash the same struct to the same TypeId — otherwise a plugin-side registration
// silently disagrees with a host-side registration of the same layout.
TEST(SdkAbi, TypeIdIdenticalViaCAbiAndCppEnum) {
    struct SampleEntry {
        int64_t size;
        int32_t mode;
        char    name[64];
    };

    iris_field_t c_fields[] = {
        {"size", IRIS_KIND_I64,  offsetof(SampleEntry, size), sizeof(int64_t), nullptr},
        {"mode", IRIS_KIND_I32,  offsetof(SampleEntry, mode), sizeof(int32_t), nullptr},
        {"name", IRIS_KIND_CSTR, offsetof(SampleEntry, name), sizeof(((SampleEntry*)0)->name), nullptr},
    };
    uint64_t c_id = iris_type_id_compute("SdkAbi::SampleEntry", c_fields, 3);

    std::vector<iris::FieldDesc> cpp_fields = {
        {"size", PrimitiveKind::I64,  offsetof(SampleEntry, size), sizeof(int64_t), ""},
        {"mode", PrimitiveKind::I32,  offsetof(SampleEntry, mode), sizeof(int32_t), ""},
        {"name", PrimitiveKind::CStr, offsetof(SampleEntry, name), sizeof(((SampleEntry*)0)->name), ""},
    };
    uint64_t cpp_id = iris::compute_type_id("SdkAbi::SampleEntry", cpp_fields);

    EXPECT_NE(c_id, 0u);
    EXPECT_EQ(c_id, cpp_id);
}
