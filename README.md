# lexbor-android

> **HTML5 parser + CSS selector engine for Android** — a thin Kotlin/JNI wrapper around the [Lexbor](https://github.com/lexbor/lexbor) C library. Parse any HTML and query nodes with CSS selectors. Zero external JVM dependencies.

---

## Table of Contents

1. [Overview](#overview)
2. [Features](#features)
3. [Requirements](#requirements)
4. [Project Structure](#project-structure)
5. [Setup & Integration](#setup--integration)
   - [As Git Submodule](#as-git-submodule)
   - [Build Configuration](#build-configuration)
   - [ProGuard / R8](#proguard--r8)
6. [Quick Start](#quick-start)
7. [API Reference](#api-reference)
   - [HtmlDocument](#htmldocument)
   - [Node](#node)
   - [NodeList](#nodelist)
8. [Usage Examples](#usage-examples)
   - [Basic Scraping](#basic-scraping)
   - [Extracting All Links](#extracting-all-links)
   - [Nested Queries](#nested-queries)
   - [Attribute Extraction](#attribute-extraction)
   - [HTML Serialization](#html-serialization)
   - [Safe Resource Management](#safe-resource-management)
   - [Coroutine-Friendly Usage](#coroutine-friendly-usage)
   - [CSS Selector Features](#css-selector-features)
9. [Memory Management](#memory-management)
10. [Architecture](#architecture)
11. [Build Optimizations](#build-optimizations)
12. [Running C++ Tests](#running-c-tests)
13. [Troubleshooting](#troubleshooting)

---

## Overview

**lexbor-android** is a lightweight Android library that brings the power of Lexbor — a fast, spec-compliant HTML5 parser written in C — to Kotlin/Android via JNI. It exposes a clean, idiomatic Kotlin API for:

- Parsing HTML strings (UTF-8, forgiving/error-recovering — the same as a browser)
- Querying the DOM with standard CSS selectors
- Reading element properties: tag name, text content, `innerHTML`, `outerHTML`, attributes

The entire native code path compiles to a single `.so` file (`liblexbor_jni.so`) with zero JVM dependencies beyond the Kotlin stdlib.

---

## Features

| Feature | Detail |
|---|---|
| **HTML5-compliant parsing** | Uses Lexbor's tokenizer + tree builder — same rules as a real browser |
| **CSS selector engine** | Tag, class, ID, attribute, pseudo-class (`:nth-child`, `:first-child`, etc.), combinators |
| **AutoCloseable API** | All objects implement `AutoCloseable` — use `use {}` blocks for guaranteed cleanup |
| **Zero JVM dependencies** | Only `kotlin-stdlib` required |
| **Minimal binary footprint** | Only 7 Lexbor modules compiled; `-Oz` + LTO + dead-code stripping |
| **Thread-safe parsing** | Create one `HtmlDocument` per thread; documents are independent |
| **Exception-safe JNI** | All C++ exceptions are converted to `RuntimeException` at the JNI boundary |
| **Memory-efficient** | Reusable CSS parser memory pool — no `malloc`/`free` per query |

---

## Requirements

| Component | Minimum Version |
|---|---|
| Android `minSdk` | 21 (Android 5.0) |
| `compileSdk` | 34 |
| NDK | r21 or later (C++17 support) |
| CMake | 3.22.1+ |
| Kotlin | 1.9.x |
| ABI | `arm64-v8a` (default; add others as needed) |

---

## Project Structure

```
lexbor-android/
├── kotlin/
│   ├── HtmlDocument.kt     ← AutoCloseable document wrapper; parse() factory
│   ├── Node.kt             ← AutoCloseable single DOM element
│   └── NodeList.kt         ← AutoCloseable Iterable<Node> from query results
├── cpp/
│   ├── native_handle.hpp   ← toHandle<T> / fromHandle<T> (jlong ↔ T*)
│   ├── document.hpp/.cpp   ← DocumentHandle: owns all Lexbor state
│   ├── node.hpp            ← NodeHandle (borrows) + NodeListHandle (owns vector)
│   ├── selector.hpp/.cpp   ← Helper functions: tag_name, text, inner/outerHTML, attr
│   └── jni.cpp             ← Exception-safe Java_* entry points + JNI_OnLoad caching
├── test/
│   └── test_basic.cpp      ← Standalone C++ smoke tests (no JNI, no Android)
├── lexbor/                 ← Git submodule (lexbor/lexbor)
├── CMakeLists.txt          ← Minimal Lexbor build (186 source files)
├── build.gradle.kts        ← Android Library plugin configuration
├── consumer-rules.pro      ← ProGuard/R8 rules to preserve JNI classes
├── ARCHITECTURE.md         ← Detailed internal architecture notes
└── README.md               ← This file
```

---

## Setup & Integration

### As Git Submodule

Add this library and its Lexbor dependency to your project:

```bash
# Add lexbor-android as a submodule
git submodule add https://github.com/AhmadNaruto/lexbor-android.git lexbor-android

# Initialize and fetch Lexbor source (required for native build)
git submodule update --init --recursive
```

### Build Configuration

**`settings.gradle.kts`** — include the module:

```kotlin
include(":lexbor-android")
project(":lexbor-android").projectDir = file("lexbor-android")
```

**`app/build.gradle.kts`** — add the dependency:

```kotlin
dependencies {
    implementation(project(":lexbor-android"))
}
```

**`lexbor-android/build.gradle.kts`** — configure the native build path:

```kotlin
android {
    namespace = "io.github.lexbor_jni"
    compileSdk = 34

    defaultConfig {
        minSdk = 21

        externalNativeBuild {
            cmake {
                // Path to your Lexbor source tree (default: sibling folder)
                arguments("-DLEXBOR_ROOT=${project.rootDir}/../lexbor")
                abiFilters("arm64-v8a")
                cppFlags("-std=c++17 -Wall -O2")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
```

**`CMakeLists.txt`** — Lexbor is bundled as a git submodule inside `lexbor-android/lexbor/`:

```cmake
# Default: uses the bundled submodule
set(LEXBOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/lexbor" CACHE PATH "Lexbor source root")
```

Or point to a custom location via Gradle arguments:

```kotlin
arguments("-DLEXBOR_ROOT=/path/to/your/lexbor")
```

### ProGuard / R8

The `consumer-rules.pro` file is automatically applied to consuming apps. It keeps the JNI wrapper classes from being obfuscated or removed:

```proguard
-keepclasseswithmembernames class * { native <methods>; }
-keep class io.github.lexbor_jni.HtmlDocument { *; }
-keep class io.github.lexbor_jni.NodeList { *; }
-keep class io.github.lexbor_jni.Node { *; }
```

No manual configuration needed — these rules propagate automatically when the library module is included.

---

## Quick Start

```kotlin
import io.github.lexbor_jni.HtmlDocument

val html = """
    <html>
      <body>
        <h1 class="title">Hello, Lexbor!</h1>
        <p class="desc">A fast HTML parser for Android.</p>
        <a href="https://example.com" data-id="42">Visit</a>
      </body>
    </html>
""".trimIndent()

// Parse and query — all resources freed automatically by use {}
HtmlDocument.parse(html).use { doc ->
    val title = doc.queryFirst("h1.title")?.use { it.text }
    println(title) // "Hello, Lexbor!"

    val href = doc.queryFirst("a[href]")?.use { it.attr("href") }
    println(href) // "https://example.com"
}
```

---

## API Reference

### `HtmlDocument`

**Package:** `io.github.lexbor_jni`
**Implements:** `AutoCloseable`

The root object. Created via the `parse()` factory. All `Node` and `NodeList` objects obtained from a document are **invalidated** when the document is closed.

#### Companion / Factory

```kotlin
companion object {
    fun parse(html: String): HtmlDocument
}
```

| Method | Returns | Description |
|---|---|---|
| `parse(html: String)` | `HtmlDocument` | Parses UTF-8 HTML string. HTML5-compliant; never throws on malformed HTML. Throws `IllegalStateException` if native memory allocation fails. |

#### Instance Methods

```kotlin
fun query(css: String): NodeList
fun queryFirst(css: String): Node?
override fun close()
```

| Method | Returns | Description |
|---|---|---|
| `query(css)` | `NodeList` | Returns all elements matching the CSS selector. Returns an empty `NodeList` if no matches. The `NodeList` must be closed when done. |
| `queryFirst(css)` | `Node?` | Returns the **first** matching element, or `null`. More efficient than `query()[0]` — stops at the first match. The `Node` must be closed when done. |
| `close()` | `Unit` | Frees all native memory. Invalidates all `Node` and `NodeList` objects from this document. **Idempotent** — safe to call multiple times. |

---

### `Node`

**Package:** `io.github.lexbor_jni`
**Implements:** `AutoCloseable`

A single DOM element returned by a CSS selector query. Holds a small (~16 bytes) native handle. Properties are computed on demand from the native DOM.

> **Validity**: A `Node` is only valid while its parent `HtmlDocument` is open. Using it after `HtmlDocument.close()` is undefined behavior.

#### Properties

```kotlin
val tagName: String
val text: String
val innerHtml: String
val outerHtml: String
```

| Property | Type | Description |
|---|---|---|
| `tagName` | `String` | Lowercase HTML tag name. Examples: `"div"`, `"span"`, `"a"`, `"img"`. |
| `text` | `String` | Concatenated text content of this element and all descendants. Equivalent to DOM `textContent`. Whitespace is preserved as-is. |
| `innerHtml` | `String` | Serialized HTML of the **children** of this element. Equivalent to DOM `innerHTML`. |
| `outerHtml` | `String` | Serialized HTML **including this element**. Equivalent to DOM `outerHTML`. |

#### Methods

```kotlin
fun attr(name: String): String?
fun hasAttr(name: String): Boolean
fun query(css: String): NodeList
fun queryFirst(css: String): Node?
override fun close()
```

| Method | Returns | Description |
|---|---|---|
| `attr(name)` | `String?` | Returns the attribute value, or `null` if the attribute is absent. Returns an **empty string** for boolean attributes (e.g. `<input disabled>`). |
| `hasAttr(name)` | `Boolean` | Returns `true` if the attribute exists, regardless of its value. |
| `query(css)` | `NodeList` | Queries **descendants** of this node matching the selector. Must be closed when done. |
| `queryFirst(css)` | `Node?` | Returns the first matching descendant, or `null`. Must be closed when done. |
| `close()` | `Unit` | Frees the native `NodeHandle` wrapper. Does **not** free the underlying DOM node (owned by the document). **Idempotent.** |

> `destroy()` is a deprecated alias for `close()` kept for backward compatibility.

---

### `NodeList`

**Package:** `io.github.lexbor_jni`
**Implements:** `Iterable<Node>`, `AutoCloseable`

An ordered, immutable collection of `Node` results from a CSS selector query.

> Each call to `get(index)` or the iterator allocates a small native `NodeHandle`. Remember to `close()` each `Node` when done.

#### Properties

```kotlin
val size: Int
```

| Property | Type | Description |
|---|---|---|
| `size` | `Int` | Number of nodes in this list. |

#### Methods

```kotlin
fun isEmpty(): Boolean
fun isNotEmpty(): Boolean
operator fun get(index: Int): Node
override fun iterator(): Iterator<Node>
inline fun forEachNode(action: (Node) -> Unit)
override fun close()
```

| Method | Returns | Description |
|---|---|---|
| `isEmpty()` | `Boolean` | Returns `true` if the list contains no elements. |
| `isNotEmpty()` | `Boolean` | Returns `true` if the list contains at least one element. |
| `get(index)` | `Node` | Returns the node at the given index. Throws `IndexOutOfBoundsException` for out-of-range indices. |
| `iterator()` | `Iterator<Node>` | Returns an iterator over all nodes. Each node from the iterator must be individually closed. |
| `forEachNode(action)` | `Unit` | Iterates over all nodes, calls `action(node)`, and automatically closes each node upon block completion. |
| `close()` | `Unit` | Frees the native `NodeListHandle` and its result vector. DOM nodes themselves are **not** freed. **Idempotent.** |

> `destroy()` is a deprecated alias for `close()`.

---

## Usage Examples

### Basic Scraping

```kotlin
import io.github.lexbor_jni.HtmlDocument

fun getPageTitle(html: String): String {
    return HtmlDocument.parse(html).use { doc ->
        doc.queryFirst("title")?.use { it.text.trim() } ?: ""
    }
}
```

### Extracting All Links

```kotlin
fun extractLinks(html: String): List<String> {
    return HtmlDocument.parse(html).use { doc ->
        doc.query("a[href]").use { links ->
            links.mapNotNull { link ->
                link.use { it.attr("href") }
            }
        }
    }
}
```

### Nested Queries

```kotlin
data class Product(val name: String, val price: String, val imageUrl: String?)

fun scrapeProductCards(html: String): List<Product> {
    return HtmlDocument.parse(html).use { doc ->
        doc.query("div.product-card").use { cards ->
            cards.mapNotNull { card ->
                card.use {
                    val name  = card.queryFirst("h2.title")?.use { it.text.trim() }
                    val price = card.queryFirst("span.price")?.use { it.text.trim() }
                    val img   = card.queryFirst("img")?.use { it.attr("src") }
                    if (name != null && price != null) Product(name, price, img) else null
                }
            }
        }
    }
}
```

### Attribute Extraction

```kotlin
fun extractImageSources(html: String): List<String> {
    return HtmlDocument.parse(html).use { doc ->
        doc.query("img").use { images ->
            images.mapNotNull { img ->
                img.use {
                    it.attr("src") ?: it.attr("data-src") // fallback to lazy-load attr
                }
            }
        }
    }
}

fun hasLazyImages(html: String): Boolean {
    return HtmlDocument.parse(html).use { doc ->
        doc.queryFirst("img[loading=lazy]")?.use { it.hasAttr("loading") } ?: false
    }
}
```

### HTML Serialization

```kotlin
fun getArticleHtml(html: String): Pair<String, String>? {
    return HtmlDocument.parse(html).use { doc ->
        doc.queryFirst("article")?.use { article ->
            // innerHTML = content inside <article>...</article>
            // outerHTML = full <article>...</article> tag
            Pair(article.innerHtml, article.outerHtml)
        }
    }
}
```

### Safe Resource Management

All objects implement `AutoCloseable`. Use `use {}` at every level to guarantee cleanup. For loops over lists, use the extension helper `forEachNode` which automatically handles closing each element:

```kotlin
// Recommended pattern — resources freed in correct LIFO order
HtmlDocument.parse(html).use { doc ->               // doc freed last
    doc.query("ul.menu > li").use { items ->         // list freed second
        items.forEachNode { item ->                  // each item is closed automatically!
            item.queryFirst("a")?.use { a ->
                println("${a.text} -> ${a.attr("href")}")
            }
        }
    }
}
// All native memory released ✓
```

Or using standard kotlin loop (requires manual `use` inside loop):

```kotlin
HtmlDocument.parse(html).use { doc ->
    doc.query("ul.menu > li").use { items ->
        for (item in items) {
            item.use {
                item.queryFirst("a")?.use { a ->
                    println("${a.text} -> ${a.attr("href")}")
                }
            }
        }
    }
}
```

Manual management (equivalent):

```kotlin
val doc  = HtmlDocument.parse(html)
val list = doc.query("li")
val node = list[0]

println(node.tagName) // "li"

node.close()   // free NodeHandle wrapper
list.close()   // free NodeListHandle + vector
doc.close()    // free entire DOM arena — must be last
```

### Coroutine-Friendly Usage

Parsing is CPU-bound — dispatch to `Dispatchers.Default`:

```kotlin
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

suspend fun parseOnBackground(html: String): List<String> =
    withContext(Dispatchers.Default) {
        HtmlDocument.parse(html).use { doc ->
            doc.query("span.tag").use { tags ->
                tags.map { tag -> tag.use { it.text.trim() } }
            }
        }
    }
```

### CSS Selector Features

```kotlin
HtmlDocument.parse(html).use { doc ->
    // Tag, class, ID
    doc.queryFirst("h1")
    doc.queryFirst("div.container")
    doc.queryFirst("#main-content")

    // Attribute selectors
    doc.query("input[type=text]")
    doc.query("a[href^=https]")   // starts with "https"
    doc.query("a[href$=.pdf]")    // ends with ".pdf"
    doc.query("a[href*=github]")  // contains "github"
    doc.query("img[loading]")     // has attribute (any value)

    // Pseudo-classes
    doc.query("li:first-child")
    doc.query("li:last-child")
    doc.query("tr:nth-child(2n+1)") // odd rows
    doc.query("p:not(.hidden)")
    doc.query("a:not([href])")

    // Combinators
    doc.query("div > p")    // direct child
    doc.query("h2 + p")     // adjacent sibling
    doc.query("h2 ~ p")     // general sibling
    doc.query("nav a")      // descendant

    // Multiple selectors (comma-separated)
    doc.query("h1, h2, h3")
    doc.query("input[type=text], input[type=email]")
}
```

---

## Memory Management

Understanding the memory ownership model prevents leaks and use-after-free bugs:

```
HtmlDocument  --owns-->  DocumentHandle  --owns-->  lxb_html_document_t  (DOM arena)
                                         --owns-->  lxb_css_memory_t     (selector pool, reused)
                                         --owns-->  lxb_css_parser_t
                                         --owns-->  lxb_selectors_t

NodeList      --owns-->  NodeListHandle  --owns-->  std::vector<lxb_dom_node_t*>
                                         --borrows--> nodes (owned by document arena)

Node          --owns-->  NodeHandle      --borrows--> lxb_dom_node_t* (owned by document)
```

**Key rules:**

1. **Close `Node` and `NodeList` before closing `HtmlDocument`.**
   After `HtmlDocument.close()`, all `Node`/`NodeList` from that document are invalid.

2. **`Node.close()` is cheap** — frees only a ~16-byte C++ wrapper, not the DOM node.

3. **`NodeList.close()` frees the result vector**, not the DOM nodes.

4. **`HtmlDocument.close()` frees everything** — the entire DOM arena is released at once.

5. All `close()` methods are **idempotent** — safe to call multiple times.

6. The CSS selector memory pool is **reused** across queries on the same document (no malloc per query call).

---

## Architecture

```
+------------------------------------------+
|  Kotlin Layer                            |
|  HtmlDocument / Node / NodeList          |
|  (AutoCloseable wrappers, nativeHandle)  |
+----------------+-------------------------+
                 |
                 |  JNI boundary
                 |  jlong = uintptr_t (raw C++ pointer)
                 |  try/catch -> RuntimeException propagation
                 |
+----------------v-------------------------+
|  C++ Layer  (namespace lexbor_jni)      |
|  DocumentHandle / NodeHandle            |
|  NodeListHandle                         |
|  jni.cpp  <-- Java_* entry points      |
+----------------+-------------------------+
                 |
                 |  Static link
                 |
+----------------v-------------------------+
|  Lexbor C Library (static .a)           |
|  lxb_html_document_t  (DOM tree)        |
|  lxb_css_selectors_t  (selector AST)   |
|  lxb_selectors_t      (DOM walker)     |
+------------------------------------------+
```

**Lexbor modules compiled (186 C source files):**

| Module | Files | Purpose |
|---|---|---|
| `core` | 20 | Arena allocator, strings, arrays, hash — base of everything |
| `tag` | 1 | Tag ID to name lookup (`"div"`, `"span"`, ...) |
| `ns` | 1 | Namespace IDs (HTML/SVG/MathML) |
| `dom` | 14 | DOM tree: node, element, attr, text, document |
| `html` | 101 | HTML5 tokenizer, tree builder, serializer, all element interfaces |
| `css` (partial) | 28 | CSS selector parser + syntax tokenizer |
| `selectors` | 1 | CSS-selector-to-DOM matching engine |

**Excluded modules:** `encoding`, `unicode`, `punycode`, `url`, `style`, `engine`, Shadow DOM — none are required for parse + CSS-select workloads.

---

## Build Optimizations

The following flags are applied in `CMakeLists.txt` for the Android `arm64-v8a` release target:

| Flag | Effect |
|---|---|
| `-Oz` | Aggressive size optimization — minimizes instruction footprint, improving ARM64 L1/L2 I-cache efficiency |
| `-flto` | Link-Time Optimization — eliminates dead code, enables cross-module inlining across all 186 C files |
| `-march=armv8-a -mtune=generic` | Enables full ARM64 ISA on modern Android devices |
| `-ffunction-sections -fdata-sections` | Places each function/data in its own ELF section for linker pruning |
| `-Wl,--gc-sections` | Removes all ELF sections not reachable from any entry point |
| `-Wl,--strip-all` | Strips all debug symbols from the final `.so` |
| `-fvisibility=hidden` | Prevents internal Lexbor symbols from polluting the dynamic symbol table |

---

## Running C++ Tests

`test/test_basic.cpp` tests the C++ layer directly — no JNI, no Android device required:

```bash
# Build and run on a Linux host (lexbor must be built as static lib first)
g++ -std=c++17 -O0 -g \
    test/test_basic.cpp \
    cpp/document.cpp cpp/selector.cpp \
    -I../lexbor/source \
    -L<path_to_lexbor_static_lib> -llexbor_static \
    -o test_basic && ./test_basic
```

Expected output:

```
[PASS] parse
[PASS] queryFirst: div
[PASS] text content
[PASS] attr href
[PASS] hasAttr
[PASS] query returns 3 nodes
[PASS] innerHTML
[PASS] outerHTML
[PASS] queryFirst from node
[PASS] no match returns empty

All tests passed.
```

---

## Troubleshooting

### `UnsatisfiedLinkError: liblexbor_jni.so`

**Cause:** The `.so` was not built for the device's ABI.

**Fix:** Confirm `abiFilters` in `build.gradle.kts` includes your target ABI:
```kotlin
abiFilters("arm64-v8a", "x86_64") // add x86_64 for emulators
```

---

### `IllegalStateException: lexbor-jni: failed to allocate HTML document`

**Cause:** Native memory allocation failed (OOM condition).

**Fix:** Ensure sufficient native heap is available. Parsing very large HTML documents may require several MB.

---

### `IllegalStateException: HtmlDocument is already closed`

**Cause:** Calling `query()` or `queryFirst()` after `close()` has been called.

**Fix:** Ensure the document is not used outside its `use {}` block or after a manual `close()` call.

---

### Node properties throw after document close

**Cause:** Accessing `Node.text`, `Node.tagName`, etc. after the parent `HtmlDocument` is closed.

**Fix:** Always extract data inside the document's `use {}` block:

```kotlin
// WRONG — node accessed after doc is closed
var node: Node? = null
HtmlDocument.parse(html).use { doc ->
    node = doc.queryFirst("h1")
}
println(node?.text) // UNSAFE: doc is already closed

// CORRECT — data extracted before doc closes
val title = HtmlDocument.parse(html).use { doc ->
    doc.queryFirst("h1")?.use { it.text }
}
println(title) // safe
```

---

### Build error: `LEXBOR_ROOT` not found

**Cause:** Lexbor submodule was not initialized.

**Fix:**
```bash
git submodule update --init --recursive
```

---

### ProGuard strips JNI classes in release builds

**Fix:** Ensure `consumer-rules.pro` is present in the module root. For manual builds, add to your app's ProGuard config:
```proguard
-keep class io.github.lexbor_jni.** { *; }
```

---

## License

This project is an Android JNI wrapper. Lexbor is licensed under the **Apache License 2.0**. See the [Lexbor repository](https://github.com/lexbor/lexbor) for the full license text.
