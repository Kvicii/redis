/* Rax -- A radix tree implementation.
 *
 * Copyright (c) 2017-2018, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef RAX_H
#define RAX_H

#include <stdint.h>

/* Representation of a radix tree as implemented in this file, that contains
 * the strings "foo", "foobar" and "footer" after the insertion of each
 * word. When the node represents a key inside the radix tree, we write it
 * between [], otherwise it is written between ().
 *
 * This is the vanilla representation:
 *
 *              (f) ""
 *                \
 *                (o) "f"
 *                  \
 *                  (o) "fo"
 *                    \
 *                  [t   b] "foo"
 *                  /     \
 *         "foot" (e)     (a) "foob"
 *                /         \
 *      "foote" (r)         (r) "fooba"
 *              /             \
 *    "footer" []             [] "foobar"
 *
 * However, this implementation implements a very common optimization where
 * successive nodes having a single child are "compressed" into the node
 * itself as a string of characters, each representing a next-level child,
 * and only the link to the node representing the last character node is
 * provided inside the representation. So the above representation is turned
 * into:
 *
 *                  ["foo"] ""
 *                     |
 *                  [t   b] "foo"
 *                  /     \
 *        "foot" ("er")    ("ar") "foob"
 *                 /          \
 *       "footer" []          [] "foobar"
 *
 * However this optimization makes the implementation a bit more complex.
 * For instance if a key "first" is added in the above radix tree, a
 * "node splitting" operation is needed, since the "foo" prefix is no longer
 * composed of nodes having a single child one after the other. This is the
 * above tree and the resulting node splitting after this event happens:
 *
 *
 *                    (f) ""
 *                    /
 *                 (i o) "f"
 *                 /   \
 *    "firs"  ("rst")  (o) "fo"
 *              /        \
 *    "first" []       [t   b] "foo"
 *                     /     \
 *           "foot" ("er")    ("ar") "foob"
 *                    /          \
 *          "footer" []          [] "foobar"
 *
 * Similarly after deletion, if a new chain of nodes having a single child
 * is created (the chain must also not include nodes that represent keys),
 * it must be compressed back into a single node.
 *
 */

// Radix Tree 优势:
    // 本质上是前缀树, 所以存储有公共前缀的数据时, 比 B+ 树, 跳表节省内存
    // 没有公共前缀的数据项, 压缩存储, value 用 listpack 存储,也可以节省内存
    // 查询复杂度是 O(K), 只与目标长度有关, 与总数据量无关
    // 这种数据结构也经常用在搜索引擎提示, 文字自动补全等场景

// Stream 在存消息时, 推荐使用默认自动生成的 时间戳+序号 作为消息 ID, 不建议自己指定消息 ID, 这样才能发挥 Radix Tree 公共前缀的优势

// Radix Tree 不足
    // 如果数据集公共前缀较少, 会导致内存占用多
    // 增删节点需要处理其它节点的分裂, 合并,跳表只需调整前后指针即可
    // B+ 树, 跳表范围查询友好, 直接遍历链表即可, Radix Tree 需遍历树结构
    // 实现难度高比 B+ 树, 跳表复杂
#define RAX_NODE_MAX_SIZE ((1<<29)-1)
// 在 raxNode 的实现中, 无论是非压缩节点还是压缩节点, 其实具有两个特点:
    // 它们所代表的 key, 是从根节点到当前节点路径上的字符串, 但并不包含当前节点
    // 它们本身就已经包含了子节点代表的字符或合并字符串, 而对于它们的子节点来说, 也都属于非压缩或压缩节点; 所以子节点本身又会保存子节点的子节点所代表的字符或合并字符串
// Radix Tree 非叶子节点, 要不然是压缩节点, 只指向单个子节点; 要不然是非压缩节点, 指向多个子节点, 但每个子节点只表示一个字符
// 所以非叶子节点无法同时指向表示单个字符的子节点和表示合并字符串的子节点
// 根节点是空的

// 非压缩节点和压缩节点的相同之处:
    // 1. 都有保存元数据的节点头 HDR
    // 2. 都会包含指向子节点的指针, 以及子节点所代表的字符串
    // 3. 从根节点到当前节点路径上的字符串如果是 Radix Tree 的一个 key, 它们都会包含指向 key 对应 value 的指针
// 非压缩节点和压缩节点的不同之处:
    // 1. 非压缩节点指向的子节点, 每个子节点代表一个字符, 非压缩节点可以指向多个子节点
    // 2. 压缩节点指向的子节点, 代表的是一个合并字符串, 压缩节点只能指向一个子节点
typedef struct raxNode {
    // 从 Radix Tree 的根节点到当前节点路径上的字符组成的字符串, 是否表示了一个完整的 key
    uint32_t iskey:1;     /* Does this node contain a key? */
    // 节点的值是否为 NULL
    // 如果当前节点是空节点, 那么该节点就不需要为指向 value 的指针分配内存空间了
    uint32_t isnull:1;    /* Associated value is NULL (don't store it). */
    // 节点是否被压缩
    uint32_t iscompr:1;   /* Node is compressed. */
    // 节点大小
    // 具体值会根据节点是压缩节点还是非压缩节点而不同:
        // 如果当前节点是压缩节点, 该值表示压缩数据的长度
        // 如果是非压缩节点, 该值表示该节点指向的子节点个数
    uint32_t size:29;     /* Number of children, or compressed string len. */
    // 这 4 个元数据就对应压缩节点和非压缩节点头部的 HDR, 其中 iskey, isnull 和 iscompr 分别用 1 bit 表示, 而 size 占用 29 bit
    /* Data layout is as follows:
     *
     * If node is not compressed we have 'size' bytes, one for each children
     * character, and 'size' raxNode pointers, point to each child node.
     * Note how the character is not stored in the children but in the
     * edge of the parents:
     *
     * [header iscompr=0][abc][a-ptr][b-ptr][c-ptr](value-ptr?)
     *
     * if node is compressed (iscompr bit is 1) the node has 1 children.
     * In that case the 'size' bytes of the string stored immediately at
     * the start of the data section, represent a sequence of successive
     * nodes linked one after the other, for which only the last one in
     * the sequence is actually represented as a node, and pointed to by
     * the current compressed node.
     *
     * [header iscompr=1][xyz][z-ptr](value-ptr?)
     *
     * Both compressed and not compressed nodes can represent a key
     * with associated data in the radix tree at any level (not just terminal
     * nodes).
     *
     * If the node has an associated key (iskey=1) and is not NULL
     * (isnull=0), then after the raxNode pointers pointing to the
     * children, an additional value pointer is present (as you can see
     * in the representation above as "value-ptr" field).
     */
    // 节点的实际存储数据
    // 对于非压缩节点来说, data 数组包括子节点对应的字符, 指向子节点的指针, 以及节点表示 key 时对应的 value 指针
    // 对于压缩节点来说, data 数组包括子节点对应的合并字符串, 指向子节点的指针, 以及节点为 key 时的 value 指针
    // 为了满足内存对齐的需要, raxNode 会根据保存的字符串长度, 在字符串后面填充一些字节
    unsigned char data[];
} raxNode;

typedef struct rax {
    // 指向头节点的指针
    raxNode *head;
    // Radix Tree 中的 key 个数
    uint64_t numele;
    // Radix Tree 中 raxNode 的个数
    uint64_t numnodes;
} rax;

/* Stack data structure used by raxLowWalk() in order to, optionally, return
 * a list of parent nodes to the caller. The nodes do not have a "parent"
 * field for space concerns, so we use the auxiliary stack when needed. */
#define RAX_STACK_STATIC_ITEMS 32
typedef struct raxStack {
    void **stack; /* Points to static_items or an heap allocated array. */
    size_t items, maxitems; /* Number of items contained and total space. */
    /* Up to RAXSTACK_STACK_ITEMS items we avoid to allocate on the heap
     * and use this static array of pointers instead. */
    void *static_items[RAX_STACK_STATIC_ITEMS];
    int oom; /* True if pushing into this stack failed for OOM at some point. */
} raxStack;

/* Optional callback used for iterators and be notified on each rax node,
 * including nodes not representing keys. If the callback returns true
 * the callback changed the node pointer in the iterator structure, and the
 * iterator implementation will have to replace the pointer in the radix tree
 * internals. This allows the callback to reallocate the node to perform
 * very special operations, normally not needed by normal applications.
 *
 * This callback is used to perform very low level analysis of the radix tree
 * structure, scanning each possible node (but the root node), or in order to
 * reallocate the nodes to reduce the allocation fragmentation (this is the
 * Redis application for this callback).
 *
 * This is currently only supported in forward iterations (raxNext) */
typedef int (*raxNodeCallback)(raxNode **noderef);

/* Radix tree iterator state is encapsulated into this data structure. */
#define RAX_ITER_STATIC_LEN 128
#define RAX_ITER_JUST_SEEKED (1<<0) /* Iterator was just seeked. Return current
                                       element for the first iteration and
                                       clear the flag. */
#define RAX_ITER_EOF (1<<1)    /* End of iteration reached. */
#define RAX_ITER_SAFE (1<<2)   /* Safe iterator, allows operations while
                                  iterating. But it is slower. */
typedef struct raxIterator {
    int flags;
    rax *rt;                /* Radix tree we are iterating. */
    unsigned char *key;     /* The current string. */
    void *data;             /* Data associated to this key. */
    size_t key_len;         /* Current key length. */
    size_t key_max;         /* Max key len the current key buffer can hold. */
    unsigned char key_static_string[RAX_ITER_STATIC_LEN];
    raxNode *node;          /* Current node. Only for unsafe iteration. */
    raxStack stack;         /* Stack used for unsafe iteration. */
    raxNodeCallback node_cb; /* Optional node callback. Normally set to NULL. */
} raxIterator;

/* A special pointer returned for not found items. */
extern void *raxNotFound;

/* Exported API. */
rax *raxNew(void);
int raxInsert(rax *rax, unsigned char *s, size_t len, void *data, void **old);
int raxTryInsert(rax *rax, unsigned char *s, size_t len, void *data, void **old);
int raxRemove(rax *rax, unsigned char *s, size_t len, void **old);
void *raxFind(rax *rax, unsigned char *s, size_t len);
void raxFree(rax *rax);
void raxFreeWithCallback(rax *rax, void (*free_callback)(void*));
void raxStart(raxIterator *it, rax *rt);
int raxSeek(raxIterator *it, const char *op, unsigned char *ele, size_t len);
int raxNext(raxIterator *it);
int raxPrev(raxIterator *it);
int raxRandomWalk(raxIterator *it, size_t steps);
int raxCompare(raxIterator *iter, const char *op, unsigned char *key, size_t key_len);
void raxStop(raxIterator *it);
int raxEOF(raxIterator *it);
void raxShow(rax *rax);
uint64_t raxSize(rax *rax);
unsigned long raxTouch(raxNode *n);
void raxSetDebugMsg(int onoff);

/* Internal API. May be used by the node callback in order to access rax nodes
 * in a low level way, so this function is exported as well. */
void raxSetData(raxNode *n, void *data);

#endif
