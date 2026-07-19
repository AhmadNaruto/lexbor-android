/*
 * selector.cpp
 *
 * Implementations of node data-extraction helpers declared in selector.hpp.
 *
 * Serialize API semantics (from lexbor/html/serialize.h):
 *
 *   lxb_html_serialize_cb(node, ...)
 *       Serializes only the OPENING TAG of `node` itself.
 *       e.g. "<div class=\"x\">"
 *
 *   lxb_html_serialize_deep_cb(node, ...)
 *       Serializes node->first_child and all its siblings recursively.
 *       This is exactly innerHTML semantics — the content *inside* `node`.
 *       Implementation: node = node->first_child; while (node) serialize_node_cb(node); node = node->next;
 *
 *   lxb_html_serialize_tree_cb(node, ...)
 *       Serializes `node` itself + all its descendants.
 *       This is exactly outerHTML semantics.
 *       For document nodes, serializes all children (skips the #document wrapper).
 *
 * String allocation strategy:
 *   - tagName   : lxb_tag_name_by_id() → static table ptr → 1 copy to std::string
 *   - text      : lxb_dom_node_text_content() → mraw alloc → copy → destroy_text()
 *   - innerHtml : lxb_html_serialize_deep_cb() streaming → std::string directly
 *   - outerHtml : lxb_html_serialize_tree_cb() streaming → std::string directly
 *   - attr      : lxb_dom_attr_value() → mraw ptr → 1 copy to std::string
 */

#include "selector.hpp"

#include <lexbor/html/serialize.h>
#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/attr.h>
#include <lexbor/dom/interface.h>
#include <lexbor/tag/tag.h>

namespace lexbor_jni {

// ── Tag name ────────────────────────────────────────────────────────────────

std::string node_tag_name(lxb_dom_node_t* node) {
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return {};

    size_t len = 0;
    // lxb_tag_name_by_id returns a pointer into a static read-only table
    // (no allocation). We copy once into std::string.
    const lxb_char_t* name = lxb_tag_name_by_id(node->local_name, &len);
    if (!name || len == 0) return {};
    return std::string(reinterpret_cast<const char*>(name), len);
}

// ── Text content ────────────────────────────────────────────────────────────

std::string node_text_content(lxb_dom_node_t* node) {
    if (!node) return {};

    size_t len = 0;
    // lxb_dom_node_text_content allocates a null-terminated string from
    // the document's text mraw pool and returns it.
    // We copy to std::string, then free via destroy_text.
    lxb_char_t* text = lxb_dom_node_text_content(node, &len);
    if (!text) return {};

    std::string result(reinterpret_cast<const char*>(text), len);

    // lxb_dom_document_destroy_text takes lxb_dom_document_t* — we get it
    // from node->owner_document (already the correct type).
    lxb_dom_document_destroy_text(node->owner_document, text);
    return result;
}

// ── Serialization callback ───────────────────────────────────────────────────

// Appends serialized chunks directly into a std::string.
// IMPORTANT: must not throw — this is a C callback called from Lexbor's C
// serialize functions. An uncaught C++ exception through C frames is UB.
// On std::bad_alloc (OOM) we return LXB_STATUS_ERROR_MEMORY_ALLOCATION so
// Lexbor propagates the error back through its own status chain.
static lxb_status_t str_append_cb(const lxb_char_t* data, size_t len, void* ctx) {
    try {
        static_cast<std::string*>(ctx)->append(
            reinterpret_cast<const char*>(data), len);
        return LXB_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
    } catch (...) {
        return LXB_STATUS_ERROR;
    }
}

// ── Inner HTML ───────────────────────────────────────────────────────────────

std::string node_inner_html(lxb_dom_node_t* node) {
    if (!node) return {};

    std::string result;
    // lxb_html_serialize_deep_cb serializes node->first_child and all its
    // siblings — exactly the innerHTML content of `node`.
    lxb_html_serialize_deep_cb(node, str_append_cb, &result);
    return result;
}

// ── Outer HTML ───────────────────────────────────────────────────────────────

std::string node_outer_html(lxb_dom_node_t* node) {
    if (!node) return {};

    std::string result;
    // lxb_html_serialize_tree_cb serializes `node` itself + all descendants —
    // exactly outerHTML semantics.
    lxb_html_serialize_tree_cb(node, str_append_cb, &result);
    return result;
}

// ── Attribute helpers ─────────────────────────────────────────────────────────

std::optional<std::string> node_attr(lxb_dom_node_t* node,
                                     const char*     name,
                                     size_t          name_len)
{
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return std::nullopt;

    lxb_dom_attr_t* attr =
        lxb_dom_element_attr_by_name(
            lxb_dom_interface_element(node),
            reinterpret_cast<const lxb_char_t*>(name),
            name_len);
    if (!attr) return std::nullopt;

    size_t val_len = 0;
    const lxb_char_t* val = lxb_dom_attr_value(attr, &val_len);

    // Attribute exists but has no value (e.g. <input disabled>):
    // Return empty string, not nullopt. nullopt means "attribute absent".
    if (!val) return std::string{};

    return std::string(reinterpret_cast<const char*>(val), val_len);
}

bool node_has_attr(lxb_dom_node_t* node, const char* name, size_t name_len) {
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return false;
    return lxb_dom_element_attr_by_name(
               lxb_dom_interface_element(node),
               reinterpret_cast<const lxb_char_t*>(name),
               name_len) != nullptr;
}

// ── Native Text Extractor ───────────────────────────────────────────────────

#include <algorithm>
#include <cctype>

static inline std::string trim_str(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) {
        start++;
    }
    if (start == s.end()) return {};
    auto end = s.end();
    do {
        end--;
    } while (end > start && std::isspace(static_cast<unsigned char>(*end)));
    return std::string(start, end + 1);
}

static std::string declare_img_entry(lxb_dom_node_t* node) {
    std::string src;
    size_t val_len = 0;
    lxb_dom_attr_t* attr = lxb_dom_element_attr_by_name(
        lxb_dom_interface_element(node),
        reinterpret_cast<const lxb_char_t*>("src"), 3);
    if (attr) {
        const lxb_char_t* val = lxb_dom_attr_value(attr, &val_len);
        if (val) {
            src = std::string(reinterpret_cast<const char*>(val), val_len);
        }
    }
    return "\n\n<img src=\"" + src + "\" yrel=\"1.45\" />\n\n";
}

static void p_traverse(lxb_dom_node_t* node, std::string& out) {
    lxb_dom_node_t* child = node->first_child;
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            if (child->local_name == LXB_TAG_BR) {
                out += "\n";
            } else if (child->local_name == LXB_TAG_IMG) {
                out += declare_img_entry(child);
            } else {
                p_traverse(child, out);
            }
        } else if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            out += node_text_content(child);
        }
        child = child->next;
    }
}

static std::string get_p_traverse(lxb_dom_node_t* node) {
    std::string out;
    p_traverse(node, out);
    std::string trimmed = trim_str(out);
    if (trimmed.empty()) return "";
    return trimmed + "\n\n";
}

static void node_text_traverse(lxb_dom_node_t* node, std::string& out) {
    lxb_dom_node_t* child = node->first_child;
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            if (child->local_name == LXB_TAG_P) {
                out += get_p_traverse(child);
            } else if (child->local_name == LXB_TAG_BR) {
                out += "\n";
            } else if (child->local_name == LXB_TAG_HR) {
                out += "\n\n";
            } else if (child->local_name == LXB_TAG_IMG) {
                out += declare_img_entry(child);
            } else {
                node_text_traverse(child, out);
            }
        } else if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            std::string txt = trim_str(node_text_content(child));
            if (!txt.empty()) {
                out += txt + "\n\n";
            }
        }
        child = child->next;
    }
}

std::string node_extract_clean_text(lxb_dom_node_t* node) {
    if (!node) return "";
    std::string out;
    lxb_dom_node_t* child = node->first_child;
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            if (child->local_name == LXB_TAG_P) {
                out += get_p_traverse(child);
            } else if (child->local_name == LXB_TAG_BR) {
                out += "\n";
            } else if (child->local_name == LXB_TAG_HR) {
                out += "\n\n";
            } else if (child->local_name == LXB_TAG_IMG) {
                out += declare_img_entry(child);
            } else {
                node_text_traverse(child, out);
            }
        } else if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            out += trim_str(node_text_content(child));
        }
        child = child->next;
    }
    return out;
}

} // namespace lexbor_jni
