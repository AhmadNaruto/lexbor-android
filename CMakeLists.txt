# =============================================================================
# CMakeLists.txt — lexbor-jni minimal scraping library
# =============================================================================
#
# Build philosophy:
#   - NO file(GLOB ...) — every .c file is listed explicitly with a reason.
#   - NO add_subdirectory(lexbor) — we do not build the full Lexbor project.
#   - Only the modules required for HTML parsing + CSS selector matching are
#     compiled.
#   - Each section is annotated with why it is needed.
#
# Required modules (dependency order):
#   core      → memory allocator, strings, arrays, hash — everything depends on this
#   tag       → tag ID ↔ name lookup (HTML element names)
#   ns        → namespace IDs (HTML/SVG/MathML) — referenced by DOM nodes
#   dom       → DOM tree structure, element/attr/node interfaces
#   html      → HTML5 tokenizer + tree builder + serializer
#   css       → CSS parser, syntax tokenizer, selector AST
#   selectors → CSS-selector-to-DOM matching engine
#
# Excluded modules and reasons:
#   encoding      — we receive UTF-8 from JVM; no charset conversion needed
#   unicode       — only required by encoding/punycode; not in our path
#   punycode      — domain name encoding; irrelevant for scraping
#   url           — URL parsing; not part of HTML parse/select pipeline
#   style         — CSS computed style application to DOM; browser feature
#   engine        — Lexbor's experimental scripting engine; not needed
#   ports/windows — we target Android/POSIX only
#
# =============================================================================

cmake_minimum_required(VERSION 3.22)
project(lexbor_jni LANGUAGES C CXX)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to the Lexbor source tree.
# We use the internal git submodule folder so that the project compiles out-of-the-box.
set(LEXBOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/lexbor"
    CACHE PATH "Root of the Lexbor source tree")

set(LEXBOR_SRC "${LEXBOR_ROOT}/source/lexbor")

# =============================================================================
# 1. MODULE: core
#    Why: Foundation for all other modules. Provides:
#      - lexbor_mraw_t     (memory pool / arena allocator)
#      - lexbor_array_t    (dynamic array)
#      - lexbor_dobject_t  (typed object pool used by selectors)
#      - lexbor_str_t      (string + serialization)
#      - lexbor_hash_t     (used by tag & ns modules)
#      - lexbor_avl_t      (AVL tree used by dom element style slot)
#      - lexbor_bst_map_t  (used by CSS tokenizer)
#      - lexbor_mem_t      (raw malloc wrapper)
#      - lexbor_serialize  (streaming serialization callback framework)
#    All other modules include lexbor/core/*.h directly.
# =============================================================================
set(LEXBOR_CORE_SOURCES
    ${LEXBOR_SRC}/core/array.c         # Dynamic pointer array
    ${LEXBOR_SRC}/core/array_obj.c     # Typed object array (used by selectors dobject)
    ${LEXBOR_SRC}/core/avl.c           # AVL tree (used by dom element style field)
    ${LEXBOR_SRC}/core/bst.c           # Binary search tree (used by css syntax)
    ${LEXBOR_SRC}/core/bst_map.c       # BST map (used by css tokenizer character maps)
    ${LEXBOR_SRC}/core/conv.c          # Number/string conversions
    ${LEXBOR_SRC}/core/diyfp.c         # Double-to-string (dtoa dependency)
    ${LEXBOR_SRC}/core/dobject.c       # Object pool (used by selectors entry alloc)
    ${LEXBOR_SRC}/core/dtoa.c          # Double-to-string (used by css value)
    ${LEXBOR_SRC}/core/hash.c          # Hash table (used by tag/ns name lookup)
    ${LEXBOR_SRC}/core/in.c            # Incoming buffer (used by html tokenizer)
    ${LEXBOR_SRC}/core/mem.c           # Memory allocator wrapper
    ${LEXBOR_SRC}/core/mraw.c          # Arena allocator (used everywhere)
    ${LEXBOR_SRC}/core/plog.c          # Parse log (used by html tree for error tracking)
    ${LEXBOR_SRC}/core/print.c         # Print utilities (used by serialization)
    ${LEXBOR_SRC}/core/serialize.c     # Streaming serialization callbacks
    ${LEXBOR_SRC}/core/shs.c           # Static hash set (tag/ns lookup tables)
    ${LEXBOR_SRC}/core/str.c           # String operations
    ${LEXBOR_SRC}/core/strtod.c        # String-to-double (used by css value)
    ${LEXBOR_SRC}/core/utils.c         # Misc utilities (char classification, etc.)
    # POSIX port — Android is POSIX
    ${LEXBOR_SRC}/ports/posix/lexbor/core/memory.c  # malloc/free/realloc wrappers
    ${LEXBOR_SRC}/ports/posix/lexbor/core/perf.c    # perf timer (no-op on Android usually)
    # NOTE: ports/posix/lexbor/core/fs.c is NOT included — filesystem ops
    # are not needed for in-memory HTML parsing.
)

# =============================================================================
# 2. MODULE: tag
#    Why: Maps tag_id ↔ tag name string ("div", "span", ...).
#         Used by: dom, html (tree builder assigns tag_ids to nodes),
#                  selector.cpp (lxb_tag_name_by_id for tagName property).
#    Files: one implementation file + generated resource (compiled in tag.c).
# =============================================================================
set(LEXBOR_TAG_SOURCES
    ${LEXBOR_SRC}/tag/tag.c
)

# =============================================================================
# 3. MODULE: ns
#    Why: Namespace IDs (LXB_NS_HTML, LXB_NS_SVG, etc.).
#         DOM nodes carry a namespace field checked by the selector engine
#         (e.g. pseudo-element matching, attribute namespace matching).
# =============================================================================
set(LEXBOR_NS_SOURCES
    ${LEXBOR_SRC}/ns/ns.c
)

# =============================================================================
# 4. MODULE: dom
#    Why: Core DOM data structures and interfaces.
#         - lxb_dom_node_t       — base node type (all elements inherit from this)
#         - lxb_dom_element_t    — element (has attributes, tag name)
#         - lxb_dom_attr_t       — attribute (name + value)
#         - lxb_dom_document_t   — root document type
#         - lxb_dom_collection_t — array of nodes (used internally by selectors)
#         - lxb_dom_character_data_t / text / comment — text nodes
#         Serialization reads text nodes; selectors walk the node tree.
#
#    EXCLUDED from dom:
#         - shadow_root.c — Shadow DOM; browser feature only
#         All other interfaces are included because the tree builder
#         creates them during parsing (e.g. document_fragment for templates).
# =============================================================================
set(LEXBOR_DOM_SOURCES
    ${LEXBOR_SRC}/dom/collection.c
    ${LEXBOR_SRC}/dom/exception.c
    ${LEXBOR_SRC}/dom/interface.c
    ${LEXBOR_SRC}/dom/interfaces/attr.c
    ${LEXBOR_SRC}/dom/interfaces/cdata_section.c
    ${LEXBOR_SRC}/dom/interfaces/character_data.c
    ${LEXBOR_SRC}/dom/interfaces/comment.c
    ${LEXBOR_SRC}/dom/interfaces/document.c
    ${LEXBOR_SRC}/dom/interfaces/document_fragment.c
    ${LEXBOR_SRC}/dom/interfaces/document_type.c
    ${LEXBOR_SRC}/dom/interfaces/element.c
    ${LEXBOR_SRC}/dom/interfaces/event_target.c
    ${LEXBOR_SRC}/dom/interfaces/node.c
    ${LEXBOR_SRC}/dom/interfaces/processing_instruction.c
    ${LEXBOR_SRC}/dom/interfaces/text.c
    # shadow_root.c EXCLUDED — Shadow DOM is a browser API, not needed
)

# =============================================================================
# 5. MODULE: html
#    Why: HTML5-compliant tokenizer + tree builder (the actual parser).
#         Also includes:
#         - html/serialize.c  — lxb_html_serialize_deep_cb for inner/outer HTML
#         - html/tag.c        — HTML-specific tag initialization
#         - html/node.c       — HTML node helpers
#
#    html/interfaces/*.c: REQUIRED — the tree builder constructs these specific
#    element types during parsing (e.g., lxb_html_script_element_t for <script>,
#    lxb_html_table_element_t for <table>). Each .c file registers the element
#    type with the HTML document's interface table. Without them, the parser
#    would crash on valid HTML that contains those tags.
#
#    html/tokenizer/state*.c: REQUIRED — implements the HTML5 tokenizer state
#    machine (states for rawtext, rcdata, script, comments, doctype, etc.).
#
#    html/tree/insertion_mode/*.c: REQUIRED — implements the HTML5 tree
#    construction algorithm's insertion modes (in_body, in_table, etc.).
#
#    EXCLUDED from html:
#         - html/encoding.c    — charset sniffing from <meta charset>; we get
#                                UTF-8 strings from the JVM, no sniffing needed.
#         - html/serialize_ext.c — extended serialization (pretty-print, options)
#                                  not called by our code.
#
#    REQUIRED (previously excluded but needed by html/interfaces/document.c):
#         - html/attribute_steps.c — document.c registers these as attribute
#                                    change callbacks on the dom_document vtable.
#                                    Without them, linking fails.
#         - html/element_steps.c   — Same: document.c registers element lifecycle
#                                    callbacks (insertion, removal, ...) on the
#                                    dom_document. Required for linking.
# =============================================================================
set(LEXBOR_HTML_INTERFACE_SOURCES
    ${LEXBOR_SRC}/html/interfaces/anchor_element.c
    ${LEXBOR_SRC}/html/interfaces/area_element.c
    ${LEXBOR_SRC}/html/interfaces/audio_element.c
    ${LEXBOR_SRC}/html/interfaces/base_element.c
    ${LEXBOR_SRC}/html/interfaces/body_element.c
    ${LEXBOR_SRC}/html/interfaces/br_element.c
    ${LEXBOR_SRC}/html/interfaces/button_element.c
    ${LEXBOR_SRC}/html/interfaces/canvas_element.c
    ${LEXBOR_SRC}/html/interfaces/d_list_element.c
    ${LEXBOR_SRC}/html/interfaces/data_element.c
    ${LEXBOR_SRC}/html/interfaces/data_list_element.c
    ${LEXBOR_SRC}/html/interfaces/details_element.c
    ${LEXBOR_SRC}/html/interfaces/dialog_element.c
    ${LEXBOR_SRC}/html/interfaces/directory_element.c
    ${LEXBOR_SRC}/html/interfaces/div_element.c
    ${LEXBOR_SRC}/html/interfaces/document.c
    ${LEXBOR_SRC}/html/interfaces/element.c
    ${LEXBOR_SRC}/html/interfaces/embed_element.c
    ${LEXBOR_SRC}/html/interfaces/field_set_element.c
    ${LEXBOR_SRC}/html/interfaces/font_element.c
    ${LEXBOR_SRC}/html/interfaces/form_element.c
    ${LEXBOR_SRC}/html/interfaces/frame_element.c
    ${LEXBOR_SRC}/html/interfaces/frame_set_element.c
    ${LEXBOR_SRC}/html/interfaces/head_element.c
    ${LEXBOR_SRC}/html/interfaces/heading_element.c
    ${LEXBOR_SRC}/html/interfaces/hr_element.c
    ${LEXBOR_SRC}/html/interfaces/html_element.c
    ${LEXBOR_SRC}/html/interfaces/iframe_element.c
    ${LEXBOR_SRC}/html/interfaces/image_element.c
    ${LEXBOR_SRC}/html/interfaces/input_element.c
    ${LEXBOR_SRC}/html/interfaces/label_element.c
    ${LEXBOR_SRC}/html/interfaces/legend_element.c
    ${LEXBOR_SRC}/html/interfaces/li_element.c
    ${LEXBOR_SRC}/html/interfaces/link_element.c
    ${LEXBOR_SRC}/html/interfaces/map_element.c
    ${LEXBOR_SRC}/html/interfaces/marquee_element.c
    ${LEXBOR_SRC}/html/interfaces/media_element.c
    ${LEXBOR_SRC}/html/interfaces/menu_element.c
    ${LEXBOR_SRC}/html/interfaces/meta_element.c
    ${LEXBOR_SRC}/html/interfaces/meter_element.c
    ${LEXBOR_SRC}/html/interfaces/mod_element.c
    ${LEXBOR_SRC}/html/interfaces/o_list_element.c
    ${LEXBOR_SRC}/html/interfaces/object_element.c
    ${LEXBOR_SRC}/html/interfaces/opt_group_element.c
    ${LEXBOR_SRC}/html/interfaces/option_element.c
    ${LEXBOR_SRC}/html/interfaces/output_element.c
    ${LEXBOR_SRC}/html/interfaces/paragraph_element.c
    ${LEXBOR_SRC}/html/interfaces/param_element.c
    ${LEXBOR_SRC}/html/interfaces/picture_element.c
    ${LEXBOR_SRC}/html/interfaces/pre_element.c
    ${LEXBOR_SRC}/html/interfaces/progress_element.c
    ${LEXBOR_SRC}/html/interfaces/quote_element.c
    ${LEXBOR_SRC}/html/interfaces/script_element.c
    ${LEXBOR_SRC}/html/interfaces/search_element.c
    ${LEXBOR_SRC}/html/interfaces/select_element.c
    ${LEXBOR_SRC}/html/interfaces/selectedcontent_element.c
    ${LEXBOR_SRC}/html/interfaces/slot_element.c
    ${LEXBOR_SRC}/html/interfaces/source_element.c
    ${LEXBOR_SRC}/html/interfaces/span_element.c
    ${LEXBOR_SRC}/html/interfaces/style_element.c
    ${LEXBOR_SRC}/html/interfaces/table_caption_element.c
    ${LEXBOR_SRC}/html/interfaces/table_cell_element.c
    ${LEXBOR_SRC}/html/interfaces/table_col_element.c
    ${LEXBOR_SRC}/html/interfaces/table_element.c
    ${LEXBOR_SRC}/html/interfaces/table_row_element.c
    ${LEXBOR_SRC}/html/interfaces/table_section_element.c
    ${LEXBOR_SRC}/html/interfaces/template_element.c
    ${LEXBOR_SRC}/html/interfaces/text_area_element.c
    ${LEXBOR_SRC}/html/interfaces/time_element.c
    ${LEXBOR_SRC}/html/interfaces/title_element.c
    ${LEXBOR_SRC}/html/interfaces/track_element.c
    ${LEXBOR_SRC}/html/interfaces/u_list_element.c
    ${LEXBOR_SRC}/html/interfaces/unknown_element.c
    ${LEXBOR_SRC}/html/interfaces/video_element.c
    ${LEXBOR_SRC}/html/interfaces/window.c
)

set(LEXBOR_HTML_SOURCES
    ${LEXBOR_SRC}/html/attribute_steps.c  # REQUIRED: registered as attr callbacks on dom_document vtable
    ${LEXBOR_SRC}/html/common.c
    ${LEXBOR_SRC}/html/element_steps.c    # REQUIRED: registered as node lifecycle callbacks on dom_document vtable
    ${LEXBOR_SRC}/html/interface.c
    ${LEXBOR_SRC}/html/node.c
    ${LEXBOR_SRC}/html/parser.c
    ${LEXBOR_SRC}/html/serialize.c          # lxb_html_serialize_deep_cb / lxb_html_serialize_tree_cb
    # html/serialize_ext.c EXCLUDED         # pretty-print only, not called by our code
    # html/encoding.c EXCLUDED              # charset sniffing from <meta charset>
    ${LEXBOR_SRC}/html/tag.c
    ${LEXBOR_SRC}/html/token.c
    ${LEXBOR_SRC}/html/token_attr.c
    ${LEXBOR_SRC}/html/tokenizer.c
    ${LEXBOR_SRC}/html/tokenizer/error.c
    ${LEXBOR_SRC}/html/tokenizer/state.c
    ${LEXBOR_SRC}/html/tokenizer/state_comment.c
    ${LEXBOR_SRC}/html/tokenizer/state_doctype.c
    ${LEXBOR_SRC}/html/tokenizer/state_rawtext.c
    ${LEXBOR_SRC}/html/tokenizer/state_rcdata.c
    ${LEXBOR_SRC}/html/tokenizer/state_script.c
    ${LEXBOR_SRC}/html/tree.c
    ${LEXBOR_SRC}/html/tree/active_formatting.c
    ${LEXBOR_SRC}/html/tree/error.c
    ${LEXBOR_SRC}/html/tree/open_elements.c
    ${LEXBOR_SRC}/html/tree/template_insertion.c
    # All insertion modes required by the HTML5 tree construction algorithm:
    ${LEXBOR_SRC}/html/tree/insertion_mode/after_after_body.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/after_after_frameset.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/after_body.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/after_frameset.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/after_head.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/before_head.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/before_html.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/foreign_content.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/in_body.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/in_caption.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/in_cell.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/in_column_group.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/in_frameset.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/in_head.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/in_head_noscript.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/in_row.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/in_table.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/in_table_body.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/in_table_text.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/in_template.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/initial.c
    ${LEXBOR_SRC}/html/tree/insertion_mode/text.c
    # Combining interface sources
    ${LEXBOR_HTML_INTERFACE_SOURCES}
)

# =============================================================================
# 6. MODULE: css (selector parsing only)
#    Why: lxb_css_selectors_parse() parses CSS selector strings into an AST.
#         The AST is then consumed by the selectors engine (module 7).
#
#    The CSS module is large; we include only what selector parsing needs:
#
#    INCLUDED:
#      css/css.c            — module init
#      css/parser.c         — lxb_css_parser_t (wraps syntax tokenizer)
#      css/selectors/       — CSS selector AST (selector.c, selectors.c, state.c)
#      css/selectors/pseudo — pseudo-class handling (:hover etc.)
#      css/syntax/          — CSS tokenizer + parser (selector parser depends on it)
#      css/log.c            — parse error accumulation
#      css/rule.c           — rule base type (selector list links to rule)
#      css/stylesheet.c     — stylesheet context (parser holds a reference)
#      css/state.c          — parser state transitions
#
#    REQUIRED (previously excluded but needed by internal dependencies):
#      css/at_rule.c        — state.c calls lxb_css_at_rule_create(); rule.c calls
#                             lxb_css_at_rule_*_destroy() for cleanup.
#      css/declaration.c    — state.c calls lxb_css_declaration_create().
#      css/property.c       — state.c calls lxb_css_property__undef_create();
#                             rule.c calls lxb_css_property_destroy() and
#                             lxb_css_property_by_id().
#      css/blank.c          — used by property/declaration parsing state machine.
#      css/unit.c           — referenced from property.c value parsing.
#      css/value.c          — property.c references value type system.
# =============================================================================
set(LEXBOR_CSS_SOURCES
    ${LEXBOR_SRC}/css/css.c
    ${LEXBOR_SRC}/css/log.c
    ${LEXBOR_SRC}/css/parser.c
    ${LEXBOR_SRC}/css/rule.c
    ${LEXBOR_SRC}/css/state.c
    ${LEXBOR_SRC}/css/stylesheet.c
    # These were initially excluded as "CSS property/value only" but are
    # required by css/rule.c and css/state.c internal dependency chain:
    ${LEXBOR_SRC}/css/at_rule.c          # state.c calls lxb_css_at_rule_create(); rule.c destroy
    ${LEXBOR_SRC}/css/at_rule/state.c    # at_rule.c state machine
    ${LEXBOR_SRC}/css/blank.c            # property/declaration parsing helpers
    ${LEXBOR_SRC}/css/declaration.c      # state.c calls lxb_css_declaration_create()
    ${LEXBOR_SRC}/css/property.c         # state.c/rule.c need lxb_css_property__undef_create()
    ${LEXBOR_SRC}/css/property/state.c   # property.c state machine
    ${LEXBOR_SRC}/css/unit.c             # referenced from property.c
    ${LEXBOR_SRC}/css/value.c            # value type system referenced by property.c
    # Selector AST
    ${LEXBOR_SRC}/css/selectors/pseudo.c
    ${LEXBOR_SRC}/css/selectors/pseudo_state.c
    ${LEXBOR_SRC}/css/selectors/selector.c
    ${LEXBOR_SRC}/css/selectors/selectors.c
    ${LEXBOR_SRC}/css/selectors/state.c
    # CSS syntax tokenizer (required by selector parser)
    ${LEXBOR_SRC}/css/syntax/anb.c          # :nth-child(An+B) parser
    ${LEXBOR_SRC}/css/syntax/parser.c
    ${LEXBOR_SRC}/css/syntax/state.c
    ${LEXBOR_SRC}/css/syntax/syntax.c
    ${LEXBOR_SRC}/css/syntax/token.c
    ${LEXBOR_SRC}/css/syntax/tokenizer.c
    ${LEXBOR_SRC}/css/syntax/tokenizer/error.c
)

# =============================================================================
# 7. MODULE: selectors
#    Why: lxb_selectors_t is the DOM walking engine that applies parsed CSS
#         selector lists against the DOM tree.
#    Single file: selectors/selectors.c
# =============================================================================
set(LEXBOR_SELECTORS_SOURCES
    ${LEXBOR_SRC}/selectors/selectors.c
)

# =============================================================================
# Build: static library for Lexbor (minimal scraping subset)
# =============================================================================
add_library(lexbor_static STATIC
    ${LEXBOR_CORE_SOURCES}
    ${LEXBOR_TAG_SOURCES}
    ${LEXBOR_NS_SOURCES}
    ${LEXBOR_DOM_SOURCES}
    ${LEXBOR_HTML_SOURCES}
    ${LEXBOR_CSS_SOURCES}
    ${LEXBOR_SELECTORS_SOURCES}
)

target_include_directories(lexbor_static PUBLIC
    ${LEXBOR_ROOT}/source   # gives #include "lexbor/..." paths
)

# Compiler flags for Lexbor C code
target_compile_options(lexbor_static PRIVATE
    -Oz                     # Optimize aggressively for size (highly beneficial for ARM instruction cache)
    -fvisibility=hidden     # don't export internal symbols
    -ffunction-sections     # enable --gc-sections to strip unused functions
    -fdata-sections
    -flto                   # Enable Link-Time Optimization
    -Wall
    -Wno-unused-function    # Lexbor has many inline helpers not all used
)

# Target-specific tuning for ARM64 when compiling for Android
if(ANDROID AND ANDROID_ABI STREQUAL "arm64-v8a")
    target_compile_options(lexbor_static PRIVATE -march=armv8-a -mtune=generic)
endif()

target_compile_definitions(lexbor_static PRIVATE
    LEXBOR_STATIC           # suppress __declspec(dllexport) on Windows (no-op on Android)
)

# =============================================================================
# Build: JNI shared library
# =============================================================================
add_library(lexbor_jni SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/document.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/selector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/jni.cpp
)

target_include_directories(lexbor_jni PRIVATE
    ${LEXBOR_ROOT}/source   # for lexbor/html/html.h etc.
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp
)

target_compile_options(lexbor_jni PRIVATE
    -Oz                     # Minimize code footprint
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
    -flto                   # Match LTO with static library to enable cross-unit inline
    -std=c++17
)

if(ANDROID AND ANDROID_ABI STREQUAL "arm64-v8a")
    target_compile_options(lexbor_jni PRIVATE -march=armv8-a -mtune=generic)
endif()

# Link order: our JNI code → static lexbor → Android system libs
target_link_libraries(lexbor_jni PRIVATE
    lexbor_static
    android      # NDK android library (for __android_log_print)
    log          # Android logging
)

# Strip dead code at link time and apply LTO
target_link_options(lexbor_jni PRIVATE
    -Wl,--gc-sections        # remove unused ELF sections
    -Wl,--strip-all          # strip debug symbols in release builds
    -flto                    # Perform Link-Time Optimization at link step
)
