/// @file   tests/test_java_backend.cpp

#include <gtest/gtest.h>
#include <sdk.hpp>
#include "backend/java/java_backend.hpp"

// ── Shared JVM fixture ────────────────────────────────────────────────────────

class JavaBackendTest : public ::testing::Test {
protected:
    static iris::JavaBackend backend;

    static void SetUpTestSuite() {
        auto r = backend.connect();
        ASSERT_TRUE(r.has_value()) << "Failed to create JVM";
    }

    static void TearDownTestSuite() {
        backend.disconnect();
    }
};

iris::JavaBackend JavaBackendTest::backend{};

// ── Test structs ──────────────────────────────────────────────────────────────

struct CPoint {
    int32_t x;
    int32_t y;
};

IRIS_TYPE(CPoint,
    IRIS_FIELD_NAMED(CPoint, x, "x"),
    IRIS_FIELD_NAMED(CPoint, y, "y")
)

// ── Tests ─────────────────────────────────────────────────────────────────────

/// Discover java.awt.Point fields via JVM reflection — expects x and y.
TEST_F(JavaBackendTest, FromJava_AwtPoint) {
    JNIEnv* env = backend.env();
    ASSERT_NE(env, nullptr);

    iris::TypeId id = backend.register_class(env, "java/awt/Point");
    ASSERT_NE(id, 0u) << "register_class returned 0 — class not found?";

    auto* desc = backend.registry().find(id);
    ASSERT_NE(desc, nullptr);

    /// getDeclaredFields() returns only x, y declared in Point itself,
    /// not the inherited fields from Point2D.
    bool has_x = false, has_y = false;
    for (auto& f : desc->fields) {
        if (f.name == "x") has_x = true;
        if (f.name == "y") has_y = true;
    }
    EXPECT_TRUE(has_x) << "Field 'x' not found in java.awt.Point";
    EXPECT_TRUE(has_y) << "Field 'y' not found in java.awt.Point";
}

/// C struct{42,99} → Java Point → verify via GetIntField.
TEST_F(JavaBackendTest, CToJava_RoundTrip) {
    JNIEnv* env = backend.env();

    iris::TypeId java_id = backend.register_class(env, "java/awt/Point");
    ASSERT_NE(java_id, 0u);

    CPoint cp{42, 99};
    iris::IrisValue c_val = iris::wrap(cp);
    c_val.type_id = java_id;

    auto result = backend.c_to_java(c_val);
    ASSERT_TRUE(result.has_value()) << "c_to_java failed";
    ASSERT_TRUE(result->is_opaque());

    auto* desc  = backend.registry().find(java_id);
    ASSERT_NE(desc, nullptr);

    jobject  obj   = static_cast<jobject>(result->opaque().ptr);
    auto     cls   = reinterpret_cast<jclass>(desc->java_class);
    jfieldID fid_x = env->GetFieldID(cls, "x", "I");
    jfieldID fid_y = env->GetFieldID(cls, "y", "I");

    EXPECT_EQ(env->GetIntField(obj, fid_x), 42);
    EXPECT_EQ(env->GetIntField(obj, fid_y), 99);
}

/// Java Point(7,13) → C bytes → verify field offsets.
TEST_F(JavaBackendTest, JavaToC_RoundTrip) {
    JNIEnv* env = backend.env();

    iris::TypeId id = backend.register_class(env, "java/awt/Point");
    ASSERT_NE(id, 0u);

    auto* desc = backend.registry().find(id);
    ASSERT_NE(desc, nullptr);

    auto      cls     = reinterpret_cast<jclass>(desc->java_class);
    jmethodID ctor_xy = env->GetMethodID(cls, "<init>", "(II)V");
    ASSERT_NE(ctor_xy, nullptr);

    jobject obj = env->NewObject(cls, ctor_xy, 7, 13);
    ASSERT_NE(obj, nullptr);

    iris::IrisValue java_val;
    java_val.type_id = id;
    java_val.payload = iris::OpaqueHandle(
        env->NewGlobalRef(obj),
        backend.jvm(),
        [](void* ptr, void* ctx) {
            auto* jvm = static_cast<JavaVM*>(ctx);
            JNIEnv* e = nullptr;
            jvm->AttachCurrentThread(reinterpret_cast<void**>(&e), nullptr);
            if (e) e->DeleteGlobalRef(static_cast<jobject>(ptr));
        }
    );
    env->DeleteLocalRef(obj);

    auto result = backend.java_to_c(java_val, id);
    ASSERT_TRUE(result.has_value()) << "java_to_c failed";
    ASSERT_TRUE(result->is_raw());

    for (auto& f : desc->fields) {
        if (f.kind != iris::PrimitiveKind::I32) continue;
        int32_t v = 0;
        std::memcpy(&v, result->raw().data() + f.offset, 4);
        if (f.name == "x") { EXPECT_EQ(v, 7); }
        if (f.name == "y") { EXPECT_EQ(v, 13); }
    }
}
