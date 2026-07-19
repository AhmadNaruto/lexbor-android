/*
 * native_handle.hpp
 *
 * Utilities for packing/unpacking typed native pointers into jlong handles.
 * Kotlin never sees the raw pointer — it only holds an opaque jlong.
 *
 * Design:
 *   - toHandle<T>()  : T* → jlong  (reinterpret_cast via uintptr_t)
 *   - fromHandle<T>(): jlong → T*  (safe null-check helper)
 *
 * No malloc/free overhead; zero-cost abstraction.
 */

#pragma once

#include <jni.h>
#include <cstdint>

namespace lexbor_jni {

// Pack a pointer into a jlong handle.
template<typename T>
inline jlong toHandle(T* ptr) {
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(ptr));
}

// Unpack a jlong handle back to a typed pointer.
// Returns nullptr if handle is 0.
template<typename T>
inline T* fromHandle(jlong handle) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(handle));
}

} // namespace lexbor_jni
