/*
 * node.hpp / node.cpp
 *
 * NodeHandle: a thin, non-owning wrapper around a single lxb_dom_node_t*.
 *
 * Ownership rules:
 *   - NodeHandle does NOT own the lxb_dom_node_t it points to.
 *   - The lxb_dom_node_t is owned by the DocumentHandle (the HTML document).
 *   - If the DocumentHandle is destroyed, this NodeHandle becomes a dangling
 *     reference. Kotlin is responsible for not calling methods after close().
 *
 * Why not ref-count nodes?
 *   Lexbor nodes live inside a mraw pool owned by the document; individual
 *   nodes cannot be freed independently. Ref-counting would add overhead with
 *   no benefit — the Kotlin API contract already forbids using a Node after
 *   close().
 *
 * Why a separate heap object per Node?
 *   Kotlin's jlong handle scheme requires a stable address. Wrapping the raw
 *   pointer in a tiny heap struct lets us type-check handles and add fields
 *   later if needed (e.g., a back-pointer to DocumentHandle for safety checks).
 */

#pragma once

#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>

#include <vector>

namespace lexbor_jni {

class DocumentHandle; // forward

// Non-owning wrapper for a single DOM node.
class NodeHandle {
public:
    explicit NodeHandle(lxb_dom_node_t* node, DocumentHandle* doc)
        : node_(node), doc_(doc) {}

    lxb_dom_node_t* node() const { return node_; }
    DocumentHandle* doc()  const { return doc_;  }

    // Non-copyable (cheap to construct; Kotlin owns one per Node object).
    NodeHandle(const NodeHandle&) = delete;
    NodeHandle& operator=(const NodeHandle&) = delete;

private:
    lxb_dom_node_t* node_; // borrowed — NOT owned
    DocumentHandle* doc_;  // borrowed — NOT owned
};

// Owning container of query results (plain non-owning pointers to DOM nodes).
class NodeListHandle {
public:
    explicit NodeListHandle(std::vector<lxb_dom_node_t*> nodes,
                            DocumentHandle* doc)
        : nodes_(std::move(nodes)), doc_(doc) {}

    size_t size() const { return nodes_.size(); }
    lxb_dom_node_t* get(size_t idx) const {
        return (idx < nodes_.size()) ? nodes_[idx] : nullptr;
    }
    DocumentHandle* doc() const { return doc_; }

    NodeListHandle(const NodeListHandle&) = delete;
    NodeListHandle& operator=(const NodeListHandle&) = delete;

private:
    std::vector<lxb_dom_node_t*> nodes_; // non-owning pointers
    DocumentHandle*              doc_;   // borrowed — NOT owned
};

} // namespace lexbor_jni
