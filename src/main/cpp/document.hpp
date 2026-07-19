/*
 * document.hpp
 *
 * C++ RAII wrapper around a single parsed HTML document.
 *
 * Object lifetime:
 *   DocumentHandle is created by DocumentHandle::parse() and destroyed
 *   explicitly when Kotlin calls HtmlDocument.close().
 *
 * Memory strategy for CSS selector parsing (fixed after real-world testing):
 *
 *   Lexbor CSS selector parsing allocates from a lxb_css_memory_t pool.
 *   If parser->memory is NULL, lxb_css_selectors_parse() creates a new
 *   lxb_css_memory_t per call and stores it as list->memory.
 *   Calling lxb_css_selector_list_destroy_memory() frees that pool,
 *   but it also NULLs parser->memory — making subsequent calls allocate
 *   again (actually this crashes because the freed pool's state is invalid).
 *
 *   The correct pattern is:
 *     1. Create a lxb_css_memory_t explicitly and bind it to the parser
 *        with lxb_css_parser_memory_set().
 *     2. After each query, call lxb_css_memory_clean() to reset the pool
 *        (reuses the backing buffer without freeing it).
 *     3. On DocumentHandle destruction, call lxb_css_memory_destroy(true).
 *
 *   This is the "fast way" documented in Lexbor's examples and avoids
 *   repeated malloc/free for every selector parse.
 */

#pragma once

#include <lexbor/html/interfaces/document.h>
#include <lexbor/html/parser.h>
#include <lexbor/css/parser.h>
#include <lexbor/css/selectors/selectors.h>
#include <lexbor/selectors/selectors.h>

#include <vector>

namespace lexbor_jni {

class DocumentHandle {
public:
    // Factory — parse UTF-8 html of `len` bytes.
    // Returns nullptr on alloc failure or parse error.
    static DocumentHandle* parse(const char* html, size_t len);

    ~DocumentHandle();

    // Run a CSS selector query starting from `root`.
    // Appends matching nodes into `out`. `first_only` enables early-stop.
    // Returns false on parse error (selector syntax is invalid).
    bool queryFromNode(lxb_dom_node_t*              root,
                       const char*                   css,
                       size_t                        css_len,
                       std::vector<lxb_dom_node_t*>& out,
                       bool                          first_only);

    // Returns the #document root node (for document-level queries).
    lxb_dom_node_t* root() const;

private:
    DocumentHandle(lxb_html_document_t* doc,
                   lxb_css_parser_t*    parser,
                   lxb_css_memory_t*    css_memory,
                   lxb_css_selectors_t* css_sels,
                   lxb_selectors_t*     sels);

    lxb_html_document_t* doc_;
    lxb_css_parser_t*    parser_;
    lxb_css_memory_t*    css_memory_;  // Reusable memory pool for selector ASTs
    lxb_css_selectors_t* css_sels_;
    lxb_selectors_t*     sels_;
};

} // namespace lexbor_jni
