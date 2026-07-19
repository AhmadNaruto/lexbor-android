/*
 * NodeList.kt
 *
 * An ordered, immutable collection of [Node] results from a CSS selector query.
 *
 * Memory model:
 *   - NodeList holds a native handle to a C++ NodeListHandle.
 *   - NodeListHandle owns a std::vector<lxb_dom_node_t*> (non-owning ptrs).
 *   - Each call to [get] allocates a small NodeHandle on the C++ heap.
 *   - [close] frees the NodeListHandle and its vector. The DOM nodes
 *     themselves are owned by the document and are NOT freed here.
 */

package io.github.lexbor_jni

/**
 * An ordered collection of [Node] elements from a CSS selector query.
 * Implements [AutoCloseable] and [Iterable].
 *
 * **Memory**: backed by a native allocation. Call [close] when you no longer
 * need the list to release native memory, or wrap in a [use] block.
 *
 * Example:
 * ```kotlin
 * doc.query("li.item").use { list ->
 *     for (item in list) {
 *         item.use {
 *             println(it.text)
 *         }
 *     }
 * }
 * ```
 */
class NodeList internal constructor(private var nativeHandle: Long) : Iterable<Node>, AutoCloseable {

    @Volatile private var closed = false

    private fun checkOpen() {
        check(!closed) { "NodeList is already closed/destroyed" }
    }

    /** Number of nodes in this list. */
    val size: Int
        get() {
            checkOpen()
            return if (nativeHandle != 0L) nativeSize(nativeHandle) else 0
        }

    /** Returns true if this list contains no elements. */
    fun isEmpty(): Boolean = size == 0

    /** Returns true if this list contains at least one element. */
    fun isNotEmpty(): Boolean = !isEmpty()

    /**
     * Returns the [Node] at [index].
     *
     * Each call allocates a small native NodeHandle — remember to call
     * [Node.close] on the returned node when done.
     *
     * @throws IndexOutOfBoundsException if index is out of range.
     */
    operator fun get(index: Int): Node {
        checkOpen()
        val sz = size
        if (index < 0 || index >= sz) {
            throw IndexOutOfBoundsException(
                "Index $index is out of bounds for NodeList of size $sz"
            )
        }
        val nodeHandle = nativeGet(nativeHandle, index)
        check(nodeHandle != 0L) { "lexbor-jni: null node at index $index" }
        return Node(nodeHandle)
    }

    /**
     * Returns an iterator over all [Node] elements.
     *
     * Each [Node] from the iterator owns a native handle; call [Node.close]
     * when done with each node.
     */
    override fun iterator(): Iterator<Node> {
        checkOpen()
        return object : Iterator<Node> {
            private var index = 0
            override fun hasNext(): Boolean {
                if (closed) return false
                return index < size
            }
            override fun next(): Node {
                checkOpen()
                if (!hasNext()) throw NoSuchElementException()
                return get(index++)
            }
        }
    }

    /**
     * Frees the native NodeList memory (the result vector).
     *
     * Does NOT free the underlying DOM nodes — those are owned by the document.
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

    private external fun nativeSize(handle: Long): Int
    private external fun nativeGet(handle: Long, index: Int): Long
    private external fun nativeDestroy(handle: Long)
}
