/*
 * Node.kt
 *
 * A single DOM element node obtained from a CSS selector query.
 *
 * Memory model:
 *   - Node holds a jlong handle to a C++ NodeHandle (2 raw pointers, ~16 bytes).
 *   - NodeHandle holds a NON-OWNING pointer to an lxb_dom_node_t inside the
 *     document's mraw arena. The arena is freed when HtmlDocument.close() is called.
 *   - [close] frees only the NodeHandle wrapper, NOT the DOM node itself.
 *
 * Validity contract:
 *   A Node is valid only while its parent HtmlDocument is open.
 *   After HtmlDocument.close(), all Node objects from that document are invalid.
 */

package com.example.lexbor

/**
 * A single DOM element returned from a CSS selector query.
 * Implements [AutoCloseable] to prevent memory leaks of the C++ NodeHandle wrapper.
 *
 * Properties compute their values on demand from the native DOM.
 *
 * **Memory**: [Node] holds a small native allocation (~16 bytes). Call [close] when done,
 * or use in a [use] block.
 *
 * **Validity**: Do not use this object after the parent [HtmlDocument] is closed.
 *
 * Example:
 * ```kotlin
 * doc.queryFirst("div.product")?.use { node ->
 *     println(node.tagName)          // "div"
 *     println(node.attr("data-id"))  // "42" or null
 * }
 * ```
 */
class Node internal constructor(private var nativeHandle: Long) : AutoCloseable {

    @Volatile private var closed = false

    private fun checkOpen() {
        check(!closed) { "Node is already closed/destroyed" }
        check(nativeHandle != 0L) { "Invalid native pointer" }
    }

    /**
     * The lowercase tag name of this element.
     * Examples: `"div"`, `"span"`, `"a"`, `"img"`.
     */
    val tagName: String
        get() {
            checkOpen()
            return nativeTagName(nativeHandle)
        }

    /**
     * The concatenated text content of this element and all its descendants.
     * Equivalent to DOM `textContent`. Whitespace is preserved as-is.
     */
    val text: String
        get() {
            checkOpen()
            return nativeText(nativeHandle)
        }

    /**
     * The serialized HTML of the **children** of this element.
     * Equivalent to DOM `innerHTML`.
     */
    val innerHtml: String
        get() {
            checkOpen()
            return nativeInnerHtml(nativeHandle)
        }

    /**
     * The serialized HTML of this element **including itself**.
     * Equivalent to DOM `outerHTML`.
     */
    val outerHtml: String
        get() {
            checkOpen()
            return nativeOuterHtml(nativeHandle)
        }

    /**
     * Returns the value of the named attribute, or `null` if the attribute
     * is absent on this element.
     *
     * Returns an **empty string** if the attribute exists but has no value
     * (e.g. `<input disabled>`).
     *
     * @param name Attribute name (case-insensitive in HTML).
     */
    fun attr(name: String): String? {
        checkOpen()
        return nativeAttr(nativeHandle, name)
    }

    /**
     * Returns `true` if this element has the attribute [name], regardless
     * of whether it has a value.
     */
    fun hasAttr(name: String): Boolean {
        checkOpen()
        return nativeHasAttr(nativeHandle, name)
    }

    /**
     * Returns a [NodeList] of all **descendants** matching [css], with this
     * element as the search root.
     *
     * The returned [NodeList] must be [NodeList.close]d when done.
     */
    fun query(css: String): NodeList {
        checkOpen()
        val listHandle = nativeQuery(nativeHandle, css)
        return NodeList(listHandle)
    }

    /**
     * Returns the first descendant matching [css], or `null` if none found.
     * More efficient than `query(css)[0]`.
     *
     * The returned [Node] must be [close]d when done.
     */
    fun queryFirst(css: String): Node? {
        checkOpen()
        val h = nativeQueryFirst(nativeHandle, css)
        return if (h != 0L) Node(h) else null
    }

    /**
     * Releases the native NodeHandle wrapper for this node.
     *
     * Does NOT free the underlying DOM node (owned by the document).
     * Safe to call multiple times (idempotent).
     */
    override fun close() {
        if (!closed) {
            closed = true
            val handle = nativeHandle
            nativeHandle = 0L
            if (handle != 0L) {
                nativeDestroy(handle)
            }
        }
    }

    /** Deprecated: use [close] instead. Kept for backward compatibility. */
    @Deprecated("Use close() instead for AutoCloseable support", ReplaceWith("close()"))
    fun destroy() {
        close()
    }

    private external fun nativeTagName(handle: Long): String
    private external fun nativeText(handle: Long): String
    private external fun nativeInnerHtml(handle: Long): String
    private external fun nativeOuterHtml(handle: Long): String
    private external fun nativeAttr(handle: Long, name: String): String?
    private external fun nativeHasAttr(handle: Long, name: String): Boolean
    private external fun nativeQuery(handle: Long, css: String): Long
    private external fun nativeQueryFirst(handle: Long, css: String): Long
    private external fun nativeDestroy(handle: Long)
}
