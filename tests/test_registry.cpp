/// @file   tests/test_registry.cpp

#include <gtest/gtest.h>
#include <sdk.hpp>
#include <registry.hpp>

// ── Test structs ──────────────────────────────────────────────────────────────

struct Point {
    int32_t x;
    int32_t y;
};

IRIS_TYPE(Point,
    IRIS_FIELD(Point, x),
    IRIS_FIELD(Point, y)
)

struct FileEntry {
    int64_t size;
    int32_t mode;
};

IRIS_TYPE(FileEntry,
    IRIS_FIELD(FileEntry, size),
    IRIS_FIELD(FileEntry, mode)
)

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(Registry, TypeIdStable) {
    auto id1 = iris::type_id_of<Point>();
    auto id2 = iris::type_id_of<Point>();
    EXPECT_EQ(id1, id2);
    EXPECT_NE(id1, 0u);
}

TEST(Registry, DifferentTypesHaveDifferentIds) {
    auto pid = iris::type_id_of<Point>();
    auto fid = iris::type_id_of<FileEntry>();
    EXPECT_NE(pid, fid);
}

TEST(Registry, FindById) {
    auto  id   = iris::type_id_of<Point>();
    auto* desc = iris::TypeRegistry::global().find(id);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->name, "Point");
    EXPECT_EQ(desc->fields.size(), 2u);
    EXPECT_EQ(desc->fields[0].name, "x");
    EXPECT_EQ(desc->fields[1].name, "y");
}

TEST(Registry, FindByName) {
    auto* desc = iris::TypeRegistry::global().find("FileEntry");
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->fields.size(), 2u);
    EXPECT_EQ(desc->fields[0].name, "size");
    EXPECT_EQ(desc->fields[0].kind, iris::PrimitiveKind::I64);
}

TEST(Registry, FieldOffsets) {
    auto* desc = iris::TypeRegistry::global().find("Point");
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->fields[0].offset, offsetof(Point, x));
    EXPECT_EQ(desc->fields[1].offset, offsetof(Point, y));
}

TEST(Registry, DynamicFromFields) {
    std::vector<iris::FieldDesc> fields = {
        { "lat", iris::PrimitiveKind::F64, 0, 8, "" },
        { "lon", iris::PrimitiveKind::F64, 8, 8, "" },
    };
    auto id = iris::TypeRegistry::global().from_fields("GeoPoint", fields, 16);
    EXPECT_NE(id, 0u);

    auto* desc = iris::TypeRegistry::global().find(id);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->name, "GeoPoint");
    EXPECT_EQ(desc->fields[0].name, "lat");
}

TEST(Registry, WrapUnwrap) {
    Point p{3, 7};
    auto  val = iris::wrap(p);

    EXPECT_TRUE(val.is_raw());
    EXPECT_EQ(val.type_id, iris::type_id_of<Point>());

    const auto& p2 = iris::unwrap<Point>(val);
    EXPECT_EQ(p2.x, 3);
    EXPECT_EQ(p2.y, 7);
}
