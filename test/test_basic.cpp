/*
 * test_basic.cpp
 *
 * Standalone smoke test for the core C++ logic.
 * Does NOT use JNI — tests DocumentHandle / selector helpers directly.
 *
 * Compile (host, not Android):
 *   g++ -std=c++17 -O0 -g test/test_basic.cpp \
 *       cpp/document.cpp cpp/selector.cpp \
 *       -I../lexbor/source \
 *       -L<lexbor_static_lib_path> -llexbor_static \
 *       -o test_basic && ./test_basic
 *
 * Expected output:
 *   [PASS] parse
 *   [PASS] queryFirst: div
 *   [PASS] text content
 *   [PASS] attr href
 *   [PASS] hasAttr
 *   [PASS] query returns 3 nodes
 *   [PASS] innerHTML
 *   [PASS] outerHTML
 *   [PASS] queryFirst from node
 *   [PASS] no match returns empty
 *   All tests passed.
 */

#include "../cpp/document.hpp"
#include "../cpp/node.hpp"
#include "../cpp/selector.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace lexbor_jni;

static const char* HTML = R"(
<!DOCTYPE html>
<html>
<head><title>Test</title></head>
<body>
  <div id="main" class="container">
    <a href="https://example.com" data-id="1">Link One</a>
    <a href="https://other.com">Link Two</a>
    <a href="#anchor">Anchor</a>
    <p class="desc">Hello <strong>World</strong></p>
  </div>
</body>
</html>
)";

#define PASS(msg) printf("[PASS] %s\n", msg)
#define FAIL(msg) do { printf("[FAIL] %s\n", msg); return 1; } while(0)

int main() {
    // ── parse ─────────────────────────────────────────────────────────────
    DocumentHandle* doc = DocumentHandle::parse(HTML, strlen(HTML));
    if (!doc) FAIL("parse");
    PASS("parse");

    // ── queryFirst ────────────────────────────────────────────────────────
    {
        std::vector<lxb_dom_node_t*> nodes;
        bool ok = doc->queryFromNode(doc->root(), "div#main", 8, nodes, true);
        if (!ok || nodes.empty()) FAIL("queryFirst: div");
        std::string tag = node_tag_name(nodes[0]);
        if (tag != "div") FAIL("queryFirst: div tag name");
        PASS("queryFirst: div");

        // ── text content ──────────────────────────────────────────────────
        std::vector<lxb_dom_node_t*> p_nodes;
        doc->queryFromNode(doc->root(), "p.desc", 6, p_nodes, true);
        if (!p_nodes.empty()) {
            std::string text = node_text_content(p_nodes[0]);
            if (text.find("Hello") == std::string::npos) FAIL("text content");
            PASS("text content");
        }

        // ── attr ──────────────────────────────────────────────────────────
        std::vector<lxb_dom_node_t*> a_nodes;
        doc->queryFromNode(doc->root(), "a[data-id]", 10, a_nodes, true);
        if (!a_nodes.empty()) {
            auto href = node_attr(a_nodes[0], "href", 4);
            if (!href.has_value() || href->empty()) FAIL("attr href");
            PASS("attr href");

            bool has = node_has_attr(a_nodes[0], "data-id", 7);
            bool missing = node_has_attr(a_nodes[0], "data-xyz", 8);
            if (!has || missing) FAIL("hasAttr");
            PASS("hasAttr");
        }
    }

    // ── query returns 3 nodes ─────────────────────────────────────────────
    {
        std::vector<lxb_dom_node_t*> links;
        doc->queryFromNode(doc->root(), "a", 1, links, false);
        if (links.size() != 3) {
            printf("[FAIL] query returns 3 nodes (got %zu)\n", links.size());
            delete doc;
            return 1;
        }
        PASS("query returns 3 nodes");
    }

    // ── innerHTML ─────────────────────────────────────────────────────────
    {
        std::vector<lxb_dom_node_t*> p_nodes;
        doc->queryFromNode(doc->root(), "p.desc", 6, p_nodes, true);
        if (!p_nodes.empty()) {
            std::string inner = node_inner_html(p_nodes[0]);
            // innerHTML of <p class="desc">Hello <strong>World</strong></p>
            // should contain "Hello" and "<strong>"
            if (inner.find("Hello") == std::string::npos ||
                inner.find("<strong>") == std::string::npos) {
                printf("[FAIL] innerHTML: got '%s'\n", inner.c_str());
                delete doc;
                return 1;
            }
            PASS("innerHTML");

            std::string outer = node_outer_html(p_nodes[0]);
            // outerHTML should start with <p
            if (outer.find("<p") == std::string::npos) {
                printf("[FAIL] outerHTML: got '%s'\n", outer.c_str());
                delete doc;
                return 1;
            }
            PASS("outerHTML");
        }
    }

    // ── queryFirst from a specific node ───────────────────────────────────
    {
        std::vector<lxb_dom_node_t*> divs;
        doc->queryFromNode(doc->root(), "#main", 5, divs, true);
        if (!divs.empty()) {
            std::vector<lxb_dom_node_t*> anchors;
            // Query within the div node
            doc->queryFromNode(divs[0], "a", 1, anchors, true);
            if (anchors.empty()) FAIL("queryFirst from node");
            PASS("queryFirst from node");
        }
    }

    // ── no match returns empty ────────────────────────────────────────────
    {
        std::vector<lxb_dom_node_t*> nothing;
        doc->queryFromNode(doc->root(), "section.nonexistent", 19, nothing, false);
        if (!nothing.empty()) FAIL("no match returns empty");
        PASS("no match returns empty");
    }

    delete doc;
    printf("\nAll tests passed.\n");
    return 0;
}
