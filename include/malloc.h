#ifndef GUARD_ALLOC_H
#define GUARD_ALLOC_H

#define FREE_AND_SET_NULL(ptr)          \
{                                       \
    Free(ptr);                          \
    ptr = NULL;                         \
}

#define TRY_FREE_AND_SET_NULL(ptr) if (ptr != NULL) FREE_AND_SET_NULL(ptr)

#define MALLOC_SYSTEM_ID 0xA3A3

enum TestAllocFlag {
    TEST_ALLOC_FLAG_NONE = 0,
    TEST_ALLOC_PERSIST_ENTIRE_TEST = 1,
};

struct MemBlock
{
    struct MemBlock *prev;

    struct MemBlock *next;

    bool32 allocated: 1;
    enum TestAllocFlag testFlags: 3;
    uintptr_t location: 28; // Top 4 bits of pointers are always 0

    u16 magic; // Magic number used for error checking. Should equal MALLOC_SYSTEM_ID.
    u16 size; // size of the block excluding this header, in multiples of 4 bytes

    uint8_t data[0];
};

#define HEAP_SIZE 0x1C300
extern u8 gHeap[HEAP_SIZE];

#if TESTING || !defined(NDEBUG)

#define Alloc(size) Alloc_(size, __FILE__ ":" STR(__LINE__), TEST_ALLOC_FLAG_NONE)
#define AllocZeroed(size) AllocZeroed_(size, __FILE__ ":" STR(__LINE__), TEST_ALLOC_FLAG_NONE)

#define AllocWithFlags(size, flags) Alloc_(size, __FILE__ ":" STR(__LINE__), flags)
#define AllocZeroedWithFlags(size, flags) AllocZeroed_(size, __FILE__ ":" STR(__LINE__), flags)

#else

#define Alloc(size) Alloc_(size, NULL, TEST_ALLOC_FLAG_NONE)
#define AllocZeroed(size) AllocZeroed_(size, NULL, TEST_ALLOC_FLAG_NONE)

#define AllocWithFlags(size, flags) Alloc_(size, NULL, TEST_ALLOC_FLAG_NONE)
#define AllocZeroedWithFlags(size, flags) AllocZeroed_(size, NULL, TEST_ALLOC_FLAG_NONE)

#endif

void *Alloc_(u32 size, const char *location, enum TestAllocFlag flags);
void *AllocZeroed_(u32 size, const char *location, enum TestAllocFlag flags);
void Free(void *pointer);
void InitHeap(void *heapStart, u32 heapSize);

const struct MemBlock *HeapHead(void);
const char *MemBlockLocation(const struct MemBlock *block);

#endif // GUARD_ALLOC_H
