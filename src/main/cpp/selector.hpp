/*
 * selector.hpp / selector.cpp
 *
 * Selector helper: everything related to extracting text/HTML from a node.
 *
 * All string-returning functions allocate a new std::string (copy from
 * Lexbor's internal buffers) because:
 *   1. Lexbor buffers are not null-terminated.
 *   2. JNI NewStringUTF() requires a null-terminated C string.
 *   3. The mraw pool lives as long as the document, so we *could* avoid
 *      copies for simple attribute reads — but the JNI call itself
 *      copies anyway, so we accept the single copy here.
 *
 * innerHtml / outerHtml use lxb_html_serialize_* which writes to a
 * lexbor_str_t (dynamically grown). We convert to std::string once and
 * free the intermediate buffer.
 */

#pragma once

#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>

#include <string>
#include <optional>

namespace lexbor_jni {

// Returns the local tag name in lowercase (e.g. "div", "span").
// For non-element nodes returns an empty string.
std::string node_tag_name(lxb_dom_node_t* node);

// Returns the concatenated text content of the node and all its descendants.
// Maps to DOM textContent.
std::string node_text_content(lxb_dom_node_t* node);

// Returns the serialized HTML of the node's *children* (innerHTML).
std::string node_inner_html(lxb_dom_node_t* node);

// Returns the serialized HTML of the node itself + children (outerHTML).
std::string node_outer_html(lxb_dom_node_t* node);

// Returns the value of a named attribute, or nullopt if absent.
std::optional<std::string> node_attr(lxb_dom_node_t* node, const char* name, size_t name_len);

// Returns true if the attribute exists (even with empty value).
bool node_has_attr(lxb_dom_node_t* node, const char* name, size_t name_len);

} // namespace lexbor_jni
