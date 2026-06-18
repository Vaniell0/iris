/// @file   tests/test_reflection.cpp
/// Tests for IRIS_REFLECT — P2996 auto-derived TypeDescriptors.

#include <gtest/gtest.h>
#include <sdk/cpp/reflect.hpp>
#include <registry.hpp>

// ── Test structs ──────────────────────────────────────────────────────────────

struct Vec3 { float x; float y; float z; };
IRIS_REFLECT(Vec3)

struct Stats { int64_t count; double mean; bool valid; };
IRIS_REFLECT(Stats)

// Mixed: scalar + char array (→ CStr)
struct Sensor {
    int32_t id;
    double  reading;
    char    label[32];
};
IRIS_REFLECT(Sensor)

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(Reflection, FieldCount) {
    auto* d = iris::TypeRegistry::global().find("Vec3");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->fields.size(), 3u);
}

TEST(Reflection, FieldNames) {
    auto* d = iris::TypeRegistry::global().find("Vec3");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->fields[0].name, "x");
    EXPECT_EQ(d->fields[1].name, "y");
    EXPECT_EQ(d->fields[2].name, "z");
}

TEST(Reflection, FieldKinds) {
    auto* d = iris::TypeRegistry::global().find("Vec3");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->fields[0].kind, iris::PrimitiveKind::F32);
    EXPECT_EQ(d->fields[1].kind, iris::PrimitiveKind::F32);
    EXPECT_EQ(d->fields[2].kind, iris::PrimitiveKind::F32);
}

TEST(Reflection, FieldOffsets) {
    auto* d = iris::TypeRegistry::global().find("Vec3");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->fields[0].offset, offsetof(Vec3, x));
    EXPECT_EQ(d->fields[1].offset, offsetof(Vec3, y));
    EXPECT_EQ(d->fields[2].offset, offsetof(Vec3, z));
}

TEST(Reflection, FieldSizes) {
    auto* d = iris::TypeRegistry::global().find("Vec3");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->fields[0].size, sizeof(float));
    EXPECT_EQ(d->total_size, sizeof(Vec3));
}

TEST(Reflection, MixedKinds) {
    auto* d = iris::TypeRegistry::global().find("Stats");
    ASSERT_NE(d, nullptr);
    ASSERT_EQ(d->fields.size(), 3u);
    EXPECT_EQ(d->fields[0].kind, iris::PrimitiveKind::I64);
    EXPECT_EQ(d->fields[1].kind, iris::PrimitiveKind::F64);
    EXPECT_EQ(d->fields[2].kind, iris::PrimitiveKind::Bool);
}

TEST(Reflection, CharArrayIsCStr) {
    auto* d = iris::TypeRegistry::global().find("Sensor");
    ASSERT_NE(d, nullptr);
    ASSERT_EQ(d->fields.size(), 3u);
    EXPECT_EQ(d->fields[0].kind, iris::PrimitiveKind::I32);
    EXPECT_EQ(d->fields[1].kind, iris::PrimitiveKind::F64);
    EXPECT_EQ(d->fields[2].kind, iris::PrimitiveKind::CStr);
    EXPECT_EQ(d->fields[2].size, 32u);
}

TEST(Reflection, TypeIdNonZero) {
    EXPECT_NE(iris::type_id_of<Vec3>(),  0u);
    EXPECT_NE(iris::type_id_of<Stats>(), 0u);
    EXPECT_NE(iris::type_id_of<Vec3>(), iris::type_id_of<Stats>());
}

TEST(Reflection, TypeIdStable) {
    EXPECT_EQ(iris::type_id_of<Vec3>(), iris::type_id_of<Vec3>());
}

TEST(Reflection, FindById) {
    auto id = iris::type_id_of<Stats>();
    auto* d = iris::TypeRegistry::global().find(id);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->name, "Stats");
}

TEST(Reflection, WrapUnwrap) {
    Vec3 v{1.0f, 2.0f, 3.0f};
    auto val = iris::wrap(v);
    EXPECT_EQ(val.type_id, iris::type_id_of<Vec3>());
    auto v2 = iris::unwrap<Vec3>(val);
    EXPECT_FLOAT_EQ(v2.x, 1.0f);
    EXPECT_FLOAT_EQ(v2.y, 2.0f);
    EXPECT_FLOAT_EQ(v2.z, 3.0f);
}
