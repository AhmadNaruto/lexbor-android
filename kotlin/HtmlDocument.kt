/*
 * HtmlDocument.kt
 *
 * Kotlin wrapper for a parsed HTML document.
 *
 * Memory contract:
 *   - HtmlDocument owns the native DocumentHandle (C++ heap).
 *   - All Node and NodeList objects returned from this document BORROW
 *     from the document's DOM arena. They become invalid after close().
 *   - close() is idempotent (safe to call multiple times).
 *
 * Usage with use{}:
 *     HtmlDocument.parse(html).use { doc ->
 *         val price = doc.queryFirst("span.price")?.text
 *     }
 *
 * System.loadLibrary is called once in HtmlDocument's companion object.
 * Node and NodeList use the same library — it is loaded only once by the JVM.
 */

package com.example.lexbor

/**
 * A parsed HTML document. Implements [AutoCloseable] for use with `use {}`.
 *
 * All [Node] and [NodeList] objects obtained from this document are
 * **invalidated** after [close] is called. Do not use them afterwards.
 *
 * Example:
 * ```kotlin
 * HtmlDocument.parse(rawHtml).use { doc ->
 *     val title = doc.queryFirst("title")?.text ?: ""
 *     val links = doc.query("a[href]")
 *     // use links...
 *     links.destroy()
 * }
 * ```
 */
class HtmlDocument private constructor(private val nativeHandle: Long) : AutoCloseable {

    companion object {
        init {
            // Load the shared library once. Node.kt and NodeList.kt do not
            // repeat this — the JVM loads each library exactly once.
            System.loadLibrary("lexbor_jni")
        }

        /**
         * Parses [html] and returns an [HtmlDocument].
         *
         * The parser is HTML5-compliant (forgiving, never throws on malformed HTML).
         *
         * @param html HTML content encoded as UTF-8.
         * @throws IllegalStateException if native memory allocation fails.
         */
        @JvmStatic
        fun parse(html: String): HtmlDocument {
            val handle = nativeParse(html)
            check(handle != 0L) { "lexbor-jni: failed to allocate HTML document" }
            return HtmlDocument(handle)
        }

        @JvmStatic private external fun nativeParse(html: String): Long
    }

    @Volatile private var closed = false

    private fun checkOpen() =
        check(!closed) { "HtmlDocument is already closed" }

    /**
     * Returns a [NodeList] of all elements matching [css].
     * Returns an empty [NodeList] if nothing matches.
     *
     * The returned [NodeList] owns a native allocation — call [NodeList.destroy]
     * when done, or iterate and discard.
     */
    fun query(css: String): NodeList {
        checkOpen()
        return NodeList(nativeQuery(nativeHandle, css))
    }

    /**
     * Returns the first element matching [css], or `null` if none found.
     *
     * More efficient than `query(css)[0]` — stops searching after the first match.
     *
     * The returned [Node] owns a small native allocation — call [Node.destroy] when done.
     */
    fun queryFirst(css: String): Node? {
        checkOpen()
        val nodeHandle = nativeQueryFirst(nativeHandle, css)
        return if (nodeHandle != 0L) Node(nodeHandle) else null
    }

    /**
     * Releases all native memory for this document.
     *
     * After [close], all [Node] and [NodeList] objects from this document
     * become **invalid**. Accessing them is undefined behavior.
     *
     * Idempotent — safe to call multiple times.
     */
    override fun close() {
        if (!closed) {
            closed = true
            nativeClose(nativeHandle)
        }
    }

    private external fun nativeClose(handle: Long)
    private external fun nativeQuery(handle: Long, css: String): Long
    private external fun nativeQueryFirst(handle: Long, css: String): Long
}
