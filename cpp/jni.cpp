/*
 * jni.cpp
 *
 * All JNI entry points for the lexbor-jni library.
 *
 * Performance & Stability Optimization Checklist:
 * 1. Exception Safety: Wrap all C++ calls in try-catch blocks and propagate
 *    exceptions (like std::bad_alloc) back to JVM as RuntimeExceptions.
 * 2. Defensively release String resources on early exit to avoid leaks.
 * 3. Cache Class/Method IDs safely on JNI_OnLoad.
 * 4. Optimizations specifically designed to run on resource-constrained Android ARM64 devices.
 */

#include "document.hpp"
#include "node.hpp"
#include "selector.hpp"
#include "native_handle.hpp"

#include <jni.h>
#include <android/log.h>
#include <stdexcept>
#include <new>

// ── Package / class configuration ────────────────────────────────────────────
#define JNI_CLASS_DOCUMENT  "com/example/lexbor/HtmlDocument"
#define JNI_CLASS_NODELIST  "com/example/lexbor/NodeList"
#define JNI_CLASS_NODE      "com/example/lexbor/Node"

#define LOG_TAG "lexbor-jni"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace lexbor_jni;

// ── Cached JNI IDs ────────────────────────────────────────────────────────────
static jclass    g_cls_node      = nullptr;
static jclass    g_cls_nodelist  = nullptr;
static jmethodID g_ctor_node     = nullptr;  // Node(Long)
static jmethodID g_ctor_nodelist = nullptr;  // NodeList(Long)

// ── Exception Helpers ─────────────────────────────────────────────────────────
static void throwJavaException(JNIEnv* env, const char* message) {
    jclass exClass = env->FindClass("java/lang/RuntimeException");
    if (exClass != nullptr) {
        env->ThrowNew(exClass, message);
    }
}

// ── JNI_OnLoad ────────────────────────────────────────────────────────────────
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    // Cache Node class + constructor (Long handle)
    {
        jclass local = env->FindClass(JNI_CLASS_NODE);
        if (!local) { LOGE("Cannot find class: %s", JNI_CLASS_NODE); return JNI_ERR; }
        g_cls_node  = reinterpret_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        g_ctor_node = env->GetMethodID(g_cls_node, "<init>", "(J)V");
        if (!g_ctor_node) { LOGE("Cannot find Node(J) constructor"); return JNI_ERR; }
    }

    // Cache NodeList class + constructor (Long handle)
    {
        jclass local = env->FindClass(JNI_CLASS_NODELIST);
        if (!local) { LOGE("Cannot find class: %s", JNI_CLASS_NODELIST); return JNI_ERR; }
        g_cls_nodelist  = reinterpret_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        g_ctor_nodelist = env->GetMethodID(g_cls_nodelist, "<init>", "(J)V");
        if (!g_ctor_nodelist) { LOGE("Cannot find NodeList(J) constructor"); return JNI_ERR; }
    }

    return JNI_VERSION_1_6;
}

// ── JNI helper: build a Kotlin NodeList from a result vector ─────────────────
static inline jlong make_node_list_handle(JNIEnv* env,
                                          std::vector<lxb_dom_node_t*> nodes,
                                          DocumentHandle*               doc)
{
    try {
        return toHandle(new NodeListHandle(std::move(nodes), doc));
    } catch (const std::bad_alloc&) {
        throwJavaException(env, "Out of memory allocating NodeListHandle");
        return 0L;
    }
}

// ── HtmlDocument JNI methods ──────────────────────────────────────────────────

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_lexbor_HtmlDocument_nativeParse(JNIEnv*  env,
                                                  jobject  /*thiz*/,
                                                  jstring  html_str)
{
    if (!html_str) return 0L;

    const char* html = env->GetStringUTFChars(html_str, nullptr);
    if (!html) return 0L;
    jsize len = env->GetStringUTFLength(html_str);

    DocumentHandle* doc = nullptr;
    try {
        doc = DocumentHandle::parse(html, static_cast<size_t>(len));
    } catch (const std::bad_alloc&) {
        env->ReleaseStringUTFChars(html_str, html);
        throwJavaException(env, "Out of memory during HTML parsing");
        return 0L;
    } catch (const std::exception& e) {
        env->ReleaseStringUTFChars(html_str, html);
        throwJavaException(env, e.what());
        return 0L;
    }

    env->ReleaseStringUTFChars(html_str, html);
    return toHandle(doc);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_lexbor_HtmlDocument_nativeClose(JNIEnv* /*env*/,
                                                   jobject /*thiz*/,
                                                   jlong   handle)
{
    if (handle != 0L) {
        delete fromHandle<DocumentHandle>(handle);
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_lexbor_HtmlDocument_nativeQuery(JNIEnv*  env,
                                                   jobject  /*thiz*/,
                                                   jlong    handle,
                                                   jstring  css_str)
{
    auto* doc = fromHandle<DocumentHandle>(handle);
    if (!doc || !css_str) return make_node_list_handle(env, {}, nullptr);

    const char* css = env->GetStringUTFChars(css_str, nullptr);
    if (!css) return make_node_list_handle(env, {}, doc);
    jsize len = env->GetStringUTFLength(css_str);

    std::vector<lxb_dom_node_t*> nodes;
    try {
        doc->queryFromNode(doc->root(), css, static_cast<size_t>(len), nodes, false);
    } catch (const std::bad_alloc&) {
        env->ReleaseStringUTFChars(css_str, css);
        throwJavaException(env, "Out of memory during CSS selection query");
        return make_node_list_handle(env, {}, doc);
    } catch (const std::exception& e) {
        env->ReleaseStringUTFChars(css_str, css);
        throwJavaException(env, e.what());
        return make_node_list_handle(env, {}, doc);
    }

    env->ReleaseStringUTFChars(css_str, css);
    return make_node_list_handle(env, std::move(nodes), doc);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_lexbor_HtmlDocument_nativeQueryFirst(JNIEnv*  env,
                                                        jobject  /*thiz*/,
                                                        jlong    handle,
                                                        jstring  css_str)
{
    auto* doc = fromHandle<DocumentHandle>(handle);
    if (!doc || !css_str) return 0L;

    const char* css = env->GetStringUTFChars(css_str, nullptr);
    if (!css) return 0L;
    jsize len = env->GetStringUTFLength(css_str);

    std::vector<lxb_dom_node_t*> nodes;
    try {
        doc->queryFromNode(doc->root(), css, static_cast<size_t>(len), nodes, true);
    } catch (const std::bad_alloc&) {
        env->ReleaseStringUTFChars(css_str, css);
        throwJavaException(env, "Out of memory during queryFirst selection");
        return 0L;
    } catch (const std::exception& e) {
        env->ReleaseStringUTFChars(css_str, css);
        throwJavaException(env, e.what());
        return 0L;
    }

    env->ReleaseStringUTFChars(css_str, css);

    if (nodes.empty()) return 0L;
    
    try {
        return toHandle(new NodeHandle(nodes[0], doc));
    } catch (const std::bad_alloc&) {
        throwJavaException(env, "Out of memory creating NodeHandle wrapper");
        return 0L;
    }
}

// ── NodeList JNI methods ──────────────────────────────────────────────────────

extern "C" JNIEXPORT jint JNICALL
Java_com_example_lexbor_NodeList_nativeSize(JNIEnv* /*env*/,
                                             jobject /*thiz*/,
                                             jlong   handle)
{
    auto* list = fromHandle<NodeListHandle>(handle);
    return list ? static_cast<jint>(list->size()) : 0;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_lexbor_NodeList_nativeGet(JNIEnv* env,
                                            jobject /*thiz*/,
                                            jlong   handle,
                                            jint    index)
{
    auto* list = fromHandle<NodeListHandle>(handle);
    if (!list) return 0L;

    lxb_dom_node_t* dom_node = list->get(static_cast<size_t>(index));
    if (!dom_node) return 0L;

    try {
        return toHandle(new NodeHandle(dom_node, list->doc()));
    } catch (const std::bad_alloc&) {
        throwJavaException(env, "Out of memory creating NodeHandle wrapper");
        return 0L;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_lexbor_NodeList_nativeDestroy(JNIEnv* /*env*/,
                                                jobject /*thiz*/,
                                                jlong   handle)
{
    if (handle != 0L) {
        delete fromHandle<NodeListHandle>(handle);
    }
}

// ── Node JNI methods ──────────────────────────────────────────────────────────

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_lexbor_Node_nativeTagName(JNIEnv* env,
                                            jobject /*thiz*/,
                                            jlong   handle)
{
    auto* h = fromHandle<NodeHandle>(handle);
    if (!h) return env->NewStringUTF("");

    try {
        std::string name = node_tag_name(h->node());
        return env->NewStringUTF(name.c_str());
    } catch (const std::exception& e) {
        throwJavaException(env, e.what());
        return nullptr;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_lexbor_Node_nativeText(JNIEnv* env,
                                         jobject /*thiz*/,
                                         jlong   handle)
{
    auto* h = fromHandle<NodeHandle>(handle);
    if (!h) return env->NewStringUTF("");

    try {
        std::string text = node_text_content(h->node());
        return env->NewStringUTF(text.c_str());
    } catch (const std::exception& e) {
        throwJavaException(env, e.what());
        return nullptr;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_lexbor_Node_nativeInnerHtml(JNIEnv* env,
                                              jobject /*thiz*/,
                                              jlong   handle)
{
    auto* h = fromHandle<NodeHandle>(handle);
    if (!h) return env->NewStringUTF("");

    try {
        std::string html = node_inner_html(h->node());
        return env->NewStringUTF(html.c_str());
    } catch (const std::exception& e) {
        throwJavaException(env, e.what());
        return nullptr;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_lexbor_Node_nativeOuterHtml(JNIEnv* env,
                                              jobject /*thiz*/,
                                              jlong   handle)
{
    auto* h = fromHandle<NodeHandle>(handle);
    if (!h) return env->NewStringUTF("");

    try {
        std::string html = node_outer_html(h->node());
        return env->NewStringUTF(html.c_str());
    } catch (const std::exception& e) {
        throwJavaException(env, e.what());
        return nullptr;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_lexbor_Node_nativeAttr(JNIEnv*  env,
                                         jobject  /*thiz*/,
                                         jlong    handle,
                                         jstring  name_str)
{
    auto* h = fromHandle<NodeHandle>(handle);
    if (!h || !name_str) return nullptr;

    const char* name = env->GetStringUTFChars(name_str, nullptr);
    if (!name) return nullptr;
    jsize len = env->GetStringUTFLength(name_str);

    jstring result = nullptr;
    try {
        auto val = node_attr(h->node(), name, static_cast<size_t>(len));
        if (val.has_value()) {
            result = env->NewStringUTF(val->c_str());
        }
    } catch (const std::exception& e) {
        env->ReleaseStringUTFChars(name_str, name);
        throwJavaException(env, e.what());
        return nullptr;
    }

    env->ReleaseStringUTFChars(name_str, name);
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_lexbor_Node_nativeHasAttr(JNIEnv*  env,
                                            jobject  /*thiz*/,
                                            jlong    handle,
                                            jstring  name_str)
{
    auto* h = fromHandle<NodeHandle>(handle);
    if (!h || !name_str) return JNI_FALSE;

    const char* name = env->GetStringUTFChars(name_str, nullptr);
    if (!name) return JNI_FALSE;
    jsize len = env->GetStringUTFLength(name_str);

    bool has = false;
    try {
        has = node_has_attr(h->node(), name, static_cast<size_t>(len));
    } catch (const std::exception& e) {
        env->ReleaseStringUTFChars(name_str, name);
        throwJavaException(env, e.what());
        return JNI_FALSE;
    }

    env->ReleaseStringUTFChars(name_str, name);
    return has ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_lexbor_Node_nativeQuery(JNIEnv*  env,
                                          jobject  /*thiz*/,
                                          jlong    handle,
                                          jstring  css_str)
{
    auto* h = fromHandle<NodeHandle>(handle);
    if (!h || !css_str) return make_node_list_handle(env, {}, nullptr);

    const char* css = env->GetStringUTFChars(css_str, nullptr);
    if (!css) return make_node_list_handle(env, {}, h->doc());
    jsize len = env->GetStringUTFLength(css_str);

    std::vector<lxb_dom_node_t*> nodes;
    try {
        h->doc()->queryFromNode(h->node(), css,
                                static_cast<size_t>(len), nodes, false);
    } catch (const std::bad_alloc&) {
        env->ReleaseStringUTFChars(css_str, css);
        throwJavaException(env, "Out of memory during node query CSS selection");
        return make_node_list_handle(env, {}, h->doc());
    } catch (const std::exception& e) {
        env->ReleaseStringUTFChars(css_str, css);
        throwJavaException(env, e.what());
        return make_node_list_handle(env, {}, h->doc());
    }

    env->ReleaseStringUTFChars(css_str, css);
    return make_node_list_handle(env, std::move(nodes), h->doc());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_lexbor_Node_nativeQueryFirst(JNIEnv*  env,
                                               jobject  /*thiz*/,
                                               jlong    handle,
                                               jstring  css_str)
{
    auto* h = fromHandle<NodeHandle>(handle);
    if (!h || !css_str) return 0L;

    const char* css = env->GetStringUTFChars(css_str, nullptr);
    if (!css) return 0L;
    jsize len = env->GetStringUTFLength(css_str);

    std::vector<lxb_dom_node_t*> nodes;
    try {
        h->doc()->queryFromNode(h->node(), css,
                                static_cast<size_t>(len), nodes, true);
    } catch (const std::bad_alloc&) {
        env->ReleaseStringUTFChars(css_str, css);
        throwJavaException(env, "Out of memory during node queryFirst selection");
        return 0L;
    } catch (const std::exception& e) {
        env->ReleaseStringUTFChars(css_str, css);
        throwJavaException(env, e.what());
        return 0L;
    }

    env->ReleaseStringUTFChars(css_str, css);

    if (nodes.empty()) return 0L;
    
    try {
        return toHandle(new NodeHandle(nodes[0], h->doc()));
    } catch (const std::bad_alloc&) {
        throwJavaException(env, "Out of memory creating NodeHandle wrapper");
        return 0L;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_lexbor_Node_nativeDestroy(JNIEnv* /*env*/,
                                            jobject /*thiz*/,
                                            jlong   handle)
{
    if (handle != 0L) {
        delete fromHandle<NodeHandle>(handle);
    }
}
