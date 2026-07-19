/*
 * document.cpp
 *
 * Owns an lxb_html_document_t + a reusable lxb_css_parser_t +
 * lxb_selectors_t so that:
 *   - HTML is parsed once (RAII destroy on DocumentHandle dtor)
 *   - CSS selectors can be parsed & matched without re-creating machinery
 *
 * Lifecycle:
 *   DocumentHandle created  → Kotlin HtmlDocument is valid
 *   DocumentHandle destroyed → every NodeHandle that pointed into it
 *                               becomes invalid (Kotlin must not use them)
 *
 * Memory ownership:
 *   DocumentHandle owns: lxb_html_document_t, lxb_css_parser_t,
 *                        lxb_css_memory_t, lxb_selectors_t, lxb_css_selectors_t
 *   NodeHandle borrows:  lxb_dom_node_t* (no ownership, no free)
 *   NodeListHandle owns: std::vector<lxb_dom_node_t*> (plain pointers)
 */

#include "document.hpp"

#include <lexbor/html/interfaces/document.h>
#include <lexbor/html/parser.h>
#include <lexbor/css/parser.h>
#include <lexbor/css/selectors/selectors.h>
#include <lexbor/selectors/selectors.h>
#include <lexbor/css/css.h>

namespace lexbor_jni {

// ── DocumentHandle ─────────────────────────────────────────────────────────

DocumentHandle::DocumentHandle(lxb_html_document_t* doc,
                               lxb_css_parser_t*    parser,
                               lxb_css_memory_t*    css_memory,
                               lxb_css_selectors_t* css_sels,
                               lxb_selectors_t*     sels)
    : doc_(doc), parser_(parser), css_memory_(css_memory), css_sels_(css_sels), sels_(sels)
{}

DocumentHandle::~DocumentHandle() {
    // Destroy in reverse dependency order.
    if (sels_)       lxb_selectors_destroy(sels_, true);
    if (css_sels_)   lxb_css_selectors_destroy(css_sels_, true);
    if (parser_)     lxb_css_parser_destroy(parser_, true);
    if (css_memory_) lxb_css_memory_destroy(css_memory_, true);
    if (doc_)        lxb_html_document_destroy(doc_);
}

// Factory: parse HTML and initialise all reusable objects.
DocumentHandle* DocumentHandle::parse(const char* html, size_t len) {
    // 1. Create & parse HTML document
    lxb_html_document_t* doc = lxb_html_document_create();
    if (!doc) return nullptr;

    lxb_status_t status =
        lxb_html_document_parse(doc,
                                reinterpret_cast<const lxb_char_t*>(html),
                                len);
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        return nullptr;
    }

    // 2. CSS memory pool
    lxb_css_memory_t* css_memory = lxb_css_memory_create();
    if (!css_memory) {
        lxb_html_document_destroy(doc);
        return nullptr;
    }
    status = lxb_css_memory_init(css_memory, 128);
    if (status != LXB_STATUS_OK) {
        lxb_css_memory_destroy(css_memory, true);
        lxb_html_document_destroy(doc);
        return nullptr;
    }

    // 3. CSS parser (reused for every selector parse)
    lxb_css_parser_t* parser = lxb_css_parser_create();
    if (!parser) {
        lxb_css_memory_destroy(css_memory, true);
        lxb_html_document_destroy(doc);
        return nullptr;
    }
    status = lxb_css_parser_init(parser, nullptr);
    if (status != LXB_STATUS_OK) {
        lxb_css_parser_destroy(parser, true);
        lxb_css_memory_destroy(css_memory, true);
        lxb_html_document_destroy(doc);
        return nullptr;
    }
    lxb_css_parser_memory_set(parser, css_memory);

    // 4. CSS selectors pool (mraw allocator for parsed selector ASTs)
    lxb_css_selectors_t* css_sels = lxb_css_selectors_create();
    if (!css_sels) {
        lxb_css_parser_destroy(parser, true);
        lxb_css_memory_destroy(css_memory, true);
        lxb_html_document_destroy(doc);
        return nullptr;
    }
    status = lxb_css_selectors_init(css_sels);
    if (status != LXB_STATUS_OK) {
        lxb_css_selectors_destroy(css_sels, true);
        lxb_css_parser_destroy(parser, true);
        lxb_css_memory_destroy(css_memory, true);
        lxb_html_document_destroy(doc);
        return nullptr;
    }
    lxb_css_parser_selectors_set(parser, css_sels);

    // 5. Selectors engine (walks the DOM tree matching nodes)
    lxb_selectors_t* sels = lxb_selectors_create();
    if (!sels) {
        lxb_css_selectors_destroy(css_sels, true);
        lxb_css_parser_destroy(parser, true);
        lxb_css_memory_destroy(css_memory, true);
        lxb_html_document_destroy(doc);
        return nullptr;
    }
    status = lxb_selectors_init(sels);
    if (status != LXB_STATUS_OK) {
        lxb_selectors_destroy(sels, true);
        lxb_css_selectors_destroy(css_sels, true);
        lxb_css_parser_destroy(parser, true);
        lxb_css_memory_destroy(css_memory, true);
        lxb_html_document_destroy(doc);
        return nullptr;
    }
    lxb_selectors_opt_set(sels, LXB_SELECTORS_OPT_MATCH_FIRST);

    return new DocumentHandle(doc, parser, css_memory, css_sels, sels);
}

// ── Selector query helpers ──────────────────────────────────────────────────

struct FindCtx {
    std::vector<lxb_dom_node_t*>* nodes;
    bool                          first_only;
};

static lxb_status_t find_cb(lxb_dom_node_t*                node,
                             lxb_css_selector_specificity_t /*spec*/,
                             void*                          ctx_raw)
{
    auto* ctx = static_cast<FindCtx*>(ctx_raw);
    ctx->nodes->push_back(node);
    return ctx->first_only ? LXB_STATUS_STOP : LXB_STATUS_OK;
}

// Run a CSS selector query starting from `root`.
// Appends matching nodes into `out`.
// Returns false only on memory allocation failure or parse error.
bool DocumentHandle::queryFromNode(lxb_dom_node_t*               root,
                                   const char*                    css,
                                   size_t                         css_len,
                                   std::vector<lxb_dom_node_t*>&  out,
                                   bool                           first_only)
{
    // Parse selector — allocates inside css_memory_.
    lxb_css_selector_list_t* list =
        lxb_css_selectors_parse(parser_,
                                reinterpret_cast<const lxb_char_t*>(css),
                                css_len);
    if (!list) return false;

    FindCtx ctx{&out, first_only};
    lxb_selectors_find(sels_, root, list, find_cb, &ctx);

    // Instead of destroying memory, we clean/reset the reusable memory pool
    // so it can be used for the next query.
    lxb_css_memory_clean(css_memory_);

    return true;
}

// Returns the document's root node (the #document node).
lxb_dom_node_t* DocumentHandle::root() const {
    return lxb_dom_interface_node(
        lxb_dom_interface_document(doc_)
    );
}

} // namespace lexbor_jni
