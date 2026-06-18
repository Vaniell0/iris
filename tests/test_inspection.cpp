/// @file   tests/test_inspection.cpp

#include <gtest/gtest.h>
#include <sdk/cpp/iris.hpp>
#include <registry.hpp>
#include <backend/java/java_backend.hpp>

// ── Test structs ──────────────────────────────────────────────────────────────

struct Packet {
    int32_t  id;
    int64_t  timestamp;
    double   value;
};

IRIS_TYPE(Packet,
    IRIS_FIELD(Packet, id),
    IRIS_FIELD(Packet, timestamp),
    IRIS_FIELD(Packet, value)
)

// ── TypeRegistry::inspect ─────────────────────────────────────────────────────

TEST(Inspection, InspectReturnsFields) {
    auto id  = iris::type_id_of<Packet>();
    auto opt = iris::TypeRegistry::global().inspect(id);
    ASSERT_TRUE(opt.has_value());

    const auto& ti = *opt;
    EXPECT_EQ(ti.id,         id);
    EXPECT_EQ(ti.name,       "Packet");
    EXPECT_EQ(ti.total_size, sizeof(Packet));
    ASSERT_EQ(ti.fields.size(), 3u);
    EXPECT_EQ(ti.fields[0].name, "id");
    EXPECT_EQ(ti.fields[1].name, "timestamp");
    EXPECT_EQ(ti.fields[2].name, "value");
}

TEST(Inspection, InspectJniSigs) {
    auto opt = iris::TypeRegistry::global().inspect(iris::type_id_of<Packet>());
    ASSERT_TRUE(opt.has_value());

    EXPECT_EQ(opt->fields[0].jni_sig, "I");   // I32
    EXPECT_EQ(opt->fields[1].jni_sig, "J");   // I64
    EXPECT_EQ(opt->fields[2].jni_sig, "D");   // F64
}

TEST(Inspection, InspectUnknownTypeReturnsNullopt) {
    auto opt = iris::TypeRegistry::global().inspect(0xdeadbeefull);
    EXPECT_FALSE(opt.has_value());
}

TEST(Inspection, InspectNoJniCalls) {
    // Before any backend touches Packet, java_handles_cached must be false.
    auto opt = iris::TypeRegistry::global().inspect(iris::type_id_of<Packet>());
    ASSERT_TRUE(opt.has_value());
    EXPECT_FALSE(opt->java_handles_cached);
}

// ── JavaBackend::dry_run ──────────────────────────────────────────────────────

TEST(Inspection, DryRunAllMappable) {
    iris::JavaBackend backend;
    ASSERT_TRUE(backend.connect().has_value());

    auto id  = iris::type_id_of<Packet>();
    auto run = backend.dry_run(id);

    EXPECT_EQ(run.type_id,       id);
    EXPECT_EQ(run.class_name,    "Packet");
    EXPECT_EQ(run.fields.size(), 3u);
    EXPECT_EQ(run.mappable_count, 3u);
    EXPECT_EQ(run.skipped_count,  0u);

    for (const auto& fm : run.fields)
        EXPECT_TRUE(fm.mappable) << "field " << fm.name << " not mappable: " << fm.reason;
}

TEST(Inspection, DryRunUnknownTypeReturnsEmpty) {
    iris::JavaBackend backend;

    auto run = backend.dry_run(0xdeadbeefull);
    EXPECT_EQ(run.type_id, 0xdeadbeefull);
    EXPECT_TRUE(run.fields.empty());
    EXPECT_EQ(run.mappable_count, 0u);
    EXPECT_EQ(run.skipped_count,  0u);
}

TEST(Inspection, DryRunWithSkippedField) {
    std::vector<iris::FieldDesc> fields = {
        { "x",    iris::PrimitiveKind::I32,   0, 4, "" },
        { "blob", iris::PrimitiveKind::Bytes,  4, 8, "" },
    };
    auto id = iris::TypeRegistry::global().from_fields("MixedType", fields, 12);
    ASSERT_NE(id, 0u);

    iris::JavaBackend backend;

    auto run = backend.dry_run(id);
    EXPECT_EQ(run.mappable_count, 1u);
    EXPECT_EQ(run.skipped_count,  1u);

    EXPECT_TRUE(run.fields[0].mappable);
    EXPECT_FALSE(run.fields[1].mappable);
    EXPECT_FALSE(run.fields[1].reason.empty());
}

// ── TypeRegistry::freeze ──────────────────────────────────────────────────────

TEST(Inspection, FreezeBlocksRegistration) {
    auto& reg = iris::TypeRegistry::global();

    EXPECT_FALSE(reg.is_frozen());

    reg.freeze();

    EXPECT_TRUE(reg.is_frozen());

    std::vector<iris::FieldDesc> fields = {
        { "n", iris::PrimitiveKind::I32, 0, 4, "" },
    };
    auto id = reg.from_fields("PostFreezeType", fields, 4);
    EXPECT_EQ(id, 0u);
}
