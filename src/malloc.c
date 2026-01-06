#include "global.h"
#include <stdint.h>
#include "malloc.h"

#include "gba/isagbprint.h"

#if TESTING
#include "test/test.h"
#endif

enum RBTree_Direction {
    LEFT = 0,
    RIGHT,
};

enum RBTree_Color {
    RED = 0,
    BLACK,
};

struct FreeMemBlock
{
    struct MemBlock hdr;

    struct FreeMemBlock *parent;
    union {
        struct {
            struct FreeMemBlock *left;
            struct FreeMemBlock *right;
        };
        struct FreeMemBlock *children[2];
    };
    enum RBTree_Color color;
};

struct Heap {
    struct FreeMemBlock *root;
    void *start;
    u32 size;
};

static struct Heap sMainHeap;

ALIGNED(4) EWRAM_DATA u8 gHeap[HEAP_SIZE] = {0};

bool32 CheckHeap();

static struct FreeMemBlock *PutMemBlockHeader(void *ptr, struct MemBlock *prev, struct MemBlock *next, u32 size)
{
    struct FreeMemBlock *header = ptr;

    header->hdr.allocated = FALSE;
    header->hdr.testFlags = TEST_ALLOC_FLAG_NONE;
    header->hdr.location = 0;
    header->hdr.magic = MALLOC_SYSTEM_ID;
    header->hdr.size = size >> 2;
    header->hdr.prev = prev;
    header->hdr.next = next;

    header->parent = NULL;
    header->left = NULL;
    header->right = NULL;
    header->color = RED;

    return header;
}

static struct FreeMemBlock *PutFirstMemBlockHeader(void *block, u32 size)
{
    return PutMemBlockHeader(block, (struct MemBlock *)block, (struct MemBlock *)block, size - sizeof(struct MemBlock));
}

static inline enum RBTree_Direction Direction(const struct FreeMemBlock *node)
{
    return node == node->parent->right ? RIGHT : LEFT;
}

static struct FreeMemBlock *RotateSubtree(struct Heap *heap, struct FreeMemBlock *subRoot, enum RBTree_Direction dir)
{
    struct FreeMemBlock *parent = subRoot->parent;
    struct FreeMemBlock *newRoot = subRoot->children[1 - dir];
    struct FreeMemBlock *newChild = newRoot->children[dir];

    subRoot->children[1 - dir] = newChild;
    if (newChild != NULL)
        newChild->parent = subRoot;

    newRoot->parent = parent;
    subRoot->parent = newRoot;
    newRoot->children[dir] = subRoot;

    if (parent != NULL)
        parent->children[subRoot == parent->right ? RIGHT : LEFT] = newRoot;
    else
        heap->root = newRoot;

    return newRoot;
}

static void InsertRBTreeNode(struct Heap *heap, struct FreeMemBlock *node)
{
    node->color = RED;

    node->left = NULL;
    node->right = NULL;

    struct FreeMemBlock *child = heap->root;
    struct FreeMemBlock *parent = NULL;
    enum RBTree_Direction dir = LEFT;

    while (child != NULL)
    {
        parent = child;
        if (child->hdr.size <= node->hdr.size)
        {
            child = child->right;
            dir = RIGHT;
        }
        else
        {
            child = child->left;
            dir = LEFT;
        }
    }

    node->parent = parent;

    if (parent == NULL) // loop wasn't entered, tree is empty
    {
        heap->root = node;
        return;
    }

    parent->children[dir] = node;

    do {
        if (parent->color == BLACK)
            return;

        struct FreeMemBlock *grandparent = parent->parent;

        if (grandparent == NULL)
        {
            parent->color = BLACK;
            return;
        }

        dir = Direction(parent);

        struct FreeMemBlock *uncle = grandparent->children[1 - dir];

        if (uncle == NULL || uncle->color == BLACK)
        {
            if (parent->children[1 - dir] == node)
            {
                RotateSubtree(heap, parent, dir);
                node = parent;
                parent = grandparent->children[dir];
            }

            RotateSubtree(heap, grandparent, 1 - dir);
            parent->color = BLACK;
            grandparent->color = RED;
            return;
        }

        parent->color = BLACK;
        uncle->color = BLACK;
        grandparent->color = RED;

        node = grandparent;
    } while((parent = node->parent));

    return;
}

static void RemoveRBTreeNode(struct Heap *heap, struct FreeMemBlock *node)
{
    if (node->left != NULL && node->right != NULL)
    {
        enum RBTree_Direction nodeDir = node->parent != NULL ? Direction(node) : LEFT;

        struct FreeMemBlock *succ = node->right;
        enum RBTree_Direction succDir = RIGHT;
        while(succ->left != NULL)
        {
            succ = succ->left;
            succDir = LEFT;
        }

        if (succ->parent == node) {
            struct FreeMemBlock *succRight = succ->right;

            succ->parent = node->parent;
            succ->children[succDir] = node;
            succ->children[1 - succDir] = node->children[1 - succDir];

            node->parent = succ;
            node->right = succRight;
            node->left = NULL;


            if (succ->parent)
                succ->parent->children[nodeDir] = succ;
            else
                heap->root = succ;
            succ->left->parent = succ;

            if (node->right)
                node->right->parent = node;
        }
        else
        {
            struct FreeMemBlock *nodeParent = node->parent;
            struct FreeMemBlock *nodeChildren[2] = {node->left, node->right};

            node->parent = succ->parent;
            node->left = succ->left;
            node->right = succ->right;

            succ->parent = nodeParent;
            succ->left = nodeChildren[LEFT];
            succ->right = nodeChildren[RIGHT];

            if (succ->parent != NULL)
                succ->parent->children[nodeDir] = succ;
            else
                heap->root = succ;
            succ->left->parent = succ;
            succ->right->parent = succ;

            node->parent->children[succDir] = node;
            if (node->right != NULL)
                node->right->parent = node;
        }
    }

    if (node->left == NULL && node->right == NULL)
    {
        if (node->parent == NULL)
        {
            heap->root = NULL;
            return;
        }

        enum RBTree_Direction dir = Direction(node);
        node->parent->children[dir] = NULL;

        if (node->color == RED)
        {
            return;
        }

        struct FreeMemBlock *parent = node->parent;
        struct FreeMemBlock *sibling;
        struct FreeMemBlock *closeNephew;
        struct FreeMemBlock *distantNephew;

        do {
            dir = Direction(node);

            sibling = parent->children[1 - dir];
            closeNephew = sibling->children[dir];
            distantNephew = sibling->children[1 - dir];

            if (sibling->color == RED)
            {
                RotateSubtree(heap, parent, dir);
                parent->color = RED;
                sibling->color = BLACK;
                sibling = closeNephew;

                distantNephew = sibling->children[1 - dir];

                if (distantNephew && distantNephew->color == RED)
                    goto rotate_parent;

                if (closeNephew && closeNephew->color == RED)
                    goto rotate_sibling;

                sibling->color = RED;
                parent->color = BLACK;
            }

            if (distantNephew && distantNephew->color == RED)
                goto rotate_parent;

            if (closeNephew && closeNephew->color == RED)
                goto rotate_sibling;

            if (!parent)
                return;

            if (parent->color == RED)
            {
                sibling->color = RED;
                parent->color = BLACK;
                return;
            }

            sibling->color = RED;
            node = parent;

        } while ((parent = node->parent));
rotate_sibling:
        RotateSubtree(heap, sibling, 1 - dir);
        sibling->color = RED;
        closeNephew->color = BLACK;
        distantNephew = sibling;
        sibling = closeNephew;

rotate_parent:
        RotateSubtree(heap, parent, dir);
        sibling->color = parent->color;
        parent->color = BLACK;
        distantNephew->color = BLACK;

        return;
    }
    else
    {
        enum RBTree_Direction dir = node->left != NULL ? LEFT : RIGHT;
        struct FreeMemBlock *child = node->children[dir];
        child->parent = node->parent;
        child->color = BLACK;

        if (child->parent == NULL)
            heap->root = child;
        else
            child->parent->children[Direction(node)] = child;
    }
}

static inline void *AllocateNode(struct Heap *heap, struct FreeMemBlock *node, const char *location, enum TestAllocFlag flags)
{
    void *ret;
    node->hdr.allocated = TRUE;
    node->hdr.testFlags = flags;
    node->hdr.location = (uintptr_t)location;
    ret = node->hdr.data;
    RemoveRBTreeNode(heap, node);
    return ret;
}

static void *AllocInternal(struct Heap *heap, u32 size, const char *location, enum TestAllocFlag flags)
{
    u32 allocSize = (size + 3) >> 2;

    struct FreeMemBlock *current = heap->root;

    void *ret = NULL;

    while (ret == NULL && current != NULL)
    {
        u32 blockSize = current->hdr.size;
        if (allocSize > blockSize)
        {
            current = current->right;
        }
        else if (allocSize == blockSize)
        {
            ret = AllocateNode(heap, current, location, flags);
        }
        else if (current->left && current->left->hdr.size >= allocSize)
        {
            current = current->left;
        }
        else
        {
            ret = AllocateNode(heap, current, location, flags);

            if (blockSize - allocSize >= (sizeof(struct FreeMemBlock) >> 2)) // Remaining space is enough to create a free node, split current block
            {
                u32 newSize = blockSize - allocSize - (sizeof(struct MemBlock) >> 2);
                current->hdr.size = allocSize;
                struct FreeMemBlock *newNode = PutMemBlockHeader(current->hdr.data + (allocSize << 2), &(current->hdr), current->hdr.next, newSize << 2);

                current->hdr.next = &newNode->hdr;
                if (newNode->hdr.next != heap->start)
                    newNode->hdr.next->prev = &newNode->hdr;
                InsertRBTreeNode(heap, newNode);
            }
        }
    }

    assertf(ret != NULL, "%s: out of memory trying to allocate %d bytes", location, size) {}

    return ret;
}

static void FreeInternal(struct Heap *heap, void *pointer)
{
    if (pointer)
    {
        struct MemBlock *block = (struct MemBlock *)(pointer - sizeof(struct MemBlock));
        AGB_ASSERT(block->magic == MALLOC_SYSTEM_ID);
        AGB_ASSERT(block->allocated == TRUE);
        block->allocated = FALSE;

        if (block->next != heap->start && !block->next->allocated)
        {
            RemoveRBTreeNode(heap, (struct FreeMemBlock *)block->next);
            block->size += block->next->size + (sizeof(struct MemBlock) >> 2);
            block->next->magic = 0;
            block->next = block->next->next;
            if (block->next != heap->start)
                block->next->prev = block;
        }

        if (block != heap->start && !block->prev->allocated)
        {
            struct FreeMemBlock *freePrevBlock = (struct FreeMemBlock *)block->prev;
            RemoveRBTreeNode(heap, freePrevBlock);
            block->prev->next = block->next;
            if (block->next != heap->start)
                block->next->prev = block->prev;
            block->magic = 0;
            block->prev->size += block->size + (sizeof(struct MemBlock) >> 2);
            block = block->prev;
        }

        InsertRBTreeNode(heap, (struct FreeMemBlock *)block);
    }
}

static void *AllocZeroedInternal(struct Heap *heap, u32 size, const char *location, enum TestAllocFlag flags)
{
    void *mem = AllocInternal(heap, size, location, flags);

    if (mem != NULL)
    {
        size = (size + 3) & ~3;

        CpuFill32(0, mem, size);
    }

    return mem;
}

static bool32 CheckMemBlockInternal(const struct Heap *heap, const void *pointer)
{
    struct MemBlock *head = (struct MemBlock *)heap->start;
    struct MemBlock *block = (struct MemBlock *)((u8 *)pointer - sizeof(struct MemBlock));

    if(block->magic != MALLOC_SYSTEM_ID)
        return FALSE;

    if(block->next->magic != MALLOC_SYSTEM_ID)
        return FALSE;

    if(block->next != head && block->next->prev != block)
        return FALSE;

    if(block->prev->magic != MALLOC_SYSTEM_ID)
        return FALSE;

    if(block->prev != head && block->prev->next != block)
        return FALSE;

    if(block->next != head && block->next != (struct MemBlock *)(block->data + (block->size << 2)))
        return FALSE;

    return TRUE;
}

void InitHeap(void *heapStart, u32 heapSize)
{
    sMainHeap.start = heapStart;
    sMainHeap.size = heapSize;
    sMainHeap.root = heapStart;
    PutFirstMemBlockHeader(heapStart, heapSize);
}

void *Alloc_(u32 size, const char *location, enum TestAllocFlag flags)
{
    return AllocInternal(&sMainHeap, size, location, flags);
}

void *AllocZeroed_(u32 size, const char *location, enum TestAllocFlag flags)
{
    return AllocZeroedInternal(&sMainHeap, size, location, flags);
}

void Free(void *pointer)
{
    FreeInternal(&sMainHeap, pointer);
}

bool32 CheckMemBlock(const void *pointer)
{
    return CheckMemBlockInternal(&sMainHeap, pointer);
}

static bool32 CheckRBTreeInternal(const struct FreeMemBlock *node)
{
    if (node == NULL) return TRUE;

    if (node->parent != NULL && (node->parent == node->right || node->parent == node->left))
        return FALSE;

    if (node->parent != NULL && node->parent->children[Direction(node)] != node)
        return FALSE;

    if (node->left != NULL && node->left->parent != node)
        return FALSE;

    if (node->right != NULL && node->right->parent != node)
        return FALSE;

    if (node->hdr.allocated)
        return FALSE;


    if (node->left && !CheckRBTreeInternal(node->left))
        return FALSE;

    if (node->right && !CheckRBTreeInternal(node->right))
        return FALSE;

    return TRUE;
}

bool32 CheckHeapRBTree(const struct Heap *heap)
{
    return CheckRBTreeInternal(heap->root);
}

bool32 CheckHeapLinkedList(const struct Heap *heap)
{
    const struct MemBlock *pos = heap->start;

    do {
        if (!CheckMemBlockInternal(heap, pos->data))
            return FALSE;
        pos = pos->next;
    } while (pos != heap->start);

    return TRUE;
}

bool32 CheckHeap(void)
{
    return CheckHeapLinkedList(&sMainHeap) && CheckHeapRBTree(&sMainHeap);
}

const struct MemBlock *HeapHead(void)
{
    return (const struct MemBlock *)sMainHeap.start;
}

const char *MemBlockLocation(const struct MemBlock *block)
{
    if (!block->allocated)
        return NULL;

    return (const char *)(ROM_START | block->location);
}
