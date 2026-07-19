# lexbor-jni — Architecture & Implementation Guide

## Project Structure

```
lexbor-jni/
    kotlin/
        HtmlDocument.kt    <- AutoCloseable wrapper, parse() factory
        NodeList.kt        <- AutoCloseable, Iterable<Node>, backed by C++ NodeListHandle
        Node.kt            <- AutoCloseable, single DOM element, properties delegate to native
    cpp/
        native_handle.hpp  <- toHandle<T> / fromHandle<T> (jlong <-> T*)
        document.hpp/.cpp  <- DocumentHandle: owns all Lexbor state (with reusable css memory)
        node.hpp           <- NodeHandle (non-owning) + NodeListHandle (owning)
        selector.hpp/.cpp  <- Pure helpers: tag_name, text, inner/outerHTML, attr
        jni.cpp            <- Exception-safe Java_* entry points + JNI_OnLoad caching
    CMakeLists.txt         <- Minimal Lexbor build targeting Android ARM64 with LTO & size optimization (-Oz)
    ARCHITECTURE.md        <- This file
```

---

## 1. Wrapper Architecture

```
Kotlin: HtmlDocument (AutoCloseable) / NodeList (AutoCloseable) / Node (AutoCloseable)
        Each holds private nativeHandle: Long with use-after-close safety guards.
               |
               | JNI boundary (jlong = uintptr_t pointer with try/catch exception propagation)
               v
C++: DocumentHandle* / NodeListHandle* / NodeHandle*
     Stored as jlong via toHandle<T>() / fromHandle<T>()
               |
               | Static link
               v
Lexbor: lxb_html_document_t / lxb_dom_node_t / lxb_css_memory_t
```

---

## 2. Mapping Object Kotlin -> Lexbor

| Kotlin       | C++ Handle          | Lexbor Type                    | Ownership                  |
|--------------|---------------------|--------------------------------|----------------------------|
| HtmlDocument | DocumentHandle*     | lxb_html_document_t*           | OWNS                       |
| NodeList     | NodeListHandle*     | std::vector<lxb_dom_node_t*>   | OWNS vector, BORROWS nodes |
| Node         | NodeHandle*         | lxb_dom_node_t*                | BORROWS (no ownership)     |
| (internal)   | (in DocumentHandle) | lxb_css_memory_t*              | OWNED by DocumentHandle    |
| (internal)   | (in DocumentHandle) | lxb_css_parser_t*              | OWNED by DocumentHandle    |
| (internal)   | (in DocumentHandle) | lxb_css_selectors_t*           | OWNED by DocumentHandle    |
| (internal)   | (in DocumentHandle) | lxb_selectors_t*               | OWNED by DocumentHandle    |

---

## 3. Lifecycle & Memory Workflow (Robust Reusable Memory Strategy)

Untuk mencegah kebocoran atau kerusakan alokasi memori internal Lexbor CSS parser, library ini mengimplementasikan skema manajemen memori **reusable** yang efisien (*fast-path*):

```
HtmlDocument.parse(html)
    -> C: DocumentHandle::parse()
          lxb_html_document_create()       alloc document
          lxb_html_document_parse()        build DOM tree into mraw arena
          lxb_css_memory_create/init()     alloc long-lived CSS memory pool
          lxb_css_parser_create/init()     alloc reusable CSS parser
          lxb_css_parser_memory_set()      bind long-lived memory pool to parser
          lxb_css_selectors_create/init()  alloc selector AST pool
          lxb_selectors_create/init()      alloc matching engine
    -> Returns: HtmlDocument(handle)  [VALID]

doc.query("div.price")
    -> C: lxb_css_selectors_parse()  parse selector -> AST (allocates in css_memory_)
          lxb_selectors_find()       walk DOM, collect matches
          lxb_css_memory_clean()     reset/clean CSS memory pool (backing buffer remains allocated for reuse)
          new NodeListHandle(vector<lxb_dom_node_t*>)
    -> Returns: NodeList(listHandle)

nodeList[0]
    -> C: new NodeHandle(dom_node*, doc*)  BORROWS the node
    -> Returns: Node(nodeHandle)

node.text  ->  lxb_dom_node_text_content() + destroy_text()
node.innerHtml  ->  lxb_html_serialize_deep_cb() streaming -> std::string directly
node.outerHtml  ->  lxb_html_serialize_tree_cb() streaming -> std::string directly

node.close()          -> delete NodeHandle   (DOM node lives in document arena)
nodeList.close()      -> delete NodeListHandle + ~vector()  (nodes untouched)

doc.close()
    -> C: ~DocumentHandle()
          lxb_selectors_destroy()
          lxb_css_selectors_destroy()
          lxb_css_parser_destroy()
          lxb_css_memory_destroy()          frees CSS parser memory pool
          lxb_html_document_destroy()       FREES ENTIRE mraw ARENA AT ONCE
    ALL Node / NodeList objects from this doc become INVALID
```

---

## 4. JNI Exception Safety & Local Reference Management

Keamanan platform Android diperketat dengan implementasi penanganan error mutakhir di layer JNI:
*   **Exception Safety**: Semua pemanggilan fungsi native dibungkus oleh blok `try-catch`. Alokasi memori C++ yang gagal (`std::bad_alloc`) atau kesalahan parsing internal diubah secara otomatis menjadi `java.lang.RuntimeException` di sisi Java/Kotlin untuk mencegah *application crashes*.
*   **Callback Exception Safety**: Callback C++ (seperti `str_append_cb` saat serialisasi) dibungkus dengan blok `try-catch` sehingga exception (seperti OOM/`std::bad_alloc`) tidak melintasi batas frame stack C Lexbor yang dapat memicu *Undefined Behavior*. Exception diubah menjadi status error Lexbor (`LXB_STATUS_ERROR_MEMORY_ALLOCATION`).
*   **Pencegahan Memory Leak**: Pelepasan string JNI (`ReleaseStringUTFChars`) dijamin berjalan secara defensif meskipun terjadi error di tengah-tengah pemrosesan query CSS.

---

## 5. Lexbor Modules Compiled (186 source files)

Modul-modul Lexbor yang dikompilasi secara statis untuk scraping minimal:

| Module            | Files | Reason Included                                     |
|-------------------|-------|-----------------------------------------------------|
| core              | 20    | Arena allocator, strings, arrays, hash — base of everything |
| tag               | 1     | tag_id <-> tag name (e.g. LXB_TAG_DIV -> "div")    |
| ns                | 1     | Namespace IDs (HTML/SVG) used by selector engine    |
| dom               | 14    | DOM tree: node, element, attr, text, document       |
| html              | 101   | HTML5 tokenizer + tree builder + serializer + all element interfaces + attribute/element mutation steps |
| css (partial)     | 28    | CSS selector parser + syntax tokenizer + property/declaration/at-rule dependency modules |
| selectors         | 1     | CSS-selector-to-DOM matching engine                 |

---

## 6. Binary Size & Build Optimizations (Android ARM64 Spec)

Penerapan optimasi di `CMakeLists.txt` untuk release target `arm64-v8a`:
*   **`-Oz` (Size Optimization)**: Compiler mengoptimalkan ukuran kode mesin secara agresif. Ini sangat krusial bagi mobile CPU ARM64 karena biner yang lebih kecil meningkatkan efisiensi instruksi cache (I-cache) L1/L2.
*   **`-flto` (Link-Time Optimization)**: Kompilasi antar modul digabungkan saat linking. Ini memotong kode mati (dead code) dan melakukan optimasi fungsi inline di sepanjang pustaka static dan shared JNI secara penuh.
*   **`-march=armv8-a -mtune=generic`**: Mengaktifkan set instruksi ARM64 native yang efisien pada platform Android modern.
*   **`-Wl,--gc-sections` & `-Wl,--strip-all`**: Menghapus seluruh simbol debugger dan section ELF yang tidak dipanggil dari biner akhir `.so`.

---

## 7. Android Kotlin AutoCloseable Usage Examples

Set LEXBOR_ROOT in CMakeLists.txt:
```cmake
set(LEXBOR_ROOT "/path/to/lexbor" CACHE PATH "Lexbor source")
```

Change package name in jni.cpp:
```c
#define JNI_CLASS_DOCUMENT  "io/github/lexbor_jni/HtmlDocument"
#define JNI_CLASS_NODELIST  "io/github/lexbor_jni/NodeList"
#define JNI_CLASS_NODE      "io/github/lexbor_jni/Node"
```

Gunakan blok `.use {}` di Kotlin untuk menjamin pembebasan memori native yang andal dan aman:

```kotlin
import io.github.lexbor_jni.HtmlDocument

// Contoh 1: Scraping satu nilai (single value) dengan penanganan memori instan
fun scrapePrice(html: String): String? {
    return HtmlDocument.parse(html).use { doc ->
        doc.queryFirst("span.price")?.use { node ->
            node.text.trim()
        }
    }
}

// Contoh 2: Scraping daftar data dengan forEachNode (rekomendasi, otomatis menutup node)
fun scrapeProductCards(html: String): List<String> {
    val results = mutableListOf<String>()
    HtmlDocument.parse(html).use { doc ->
        doc.query("div.product-card").use { cards ->
            cards.forEachNode { card ->
                val name = card.queryFirst("h2.title")?.use { it.text }
                val price = card.queryFirst("span.price")?.use { it.text }
                if (name != null && price != null) {
                    results.add("$name: $price")
                }
            }
        }
    }
    return results
}
```
