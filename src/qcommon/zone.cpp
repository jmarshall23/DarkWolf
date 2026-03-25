#include "../game/q_shared.h"
#include "qcommon.h"
#include <setjmp.h>
#ifdef __linux__
#include <netinet/in.h>
#else
#if defined(MACOS_X)
#include <netinet/in.h>
#else
#include <winsock.h>
#endif
#endif

#define MIN_DEDICATED_COMHUNKMEGS 1
#define MIN_COMHUNKMEGS 554      // RF, optimizing
#define DEF_COMHUNKMEGS "572"
#define DEF_COMZONEMEGS "530"

#define HUNK_MAGIC      0x89537892
#define HUNK_FREE_MAGIC 0x89537893

typedef struct {
    int     magic;
    size_t  size;
} hunkHeader_t;

typedef struct {
    size_t  mark;
    size_t  permanent;
    size_t  temp;
    size_t  tempHighwater;
} hunkUsed_t;

typedef struct hunkblock_s {
    size_t                  size;
    byte                    printed;
    struct hunkblock_s* next;
    char* label;
    char* file;
    int                     line;
} hunkblock_t;

static hunkblock_t* hunkblocks;

static hunkUsed_t  hunk_low, hunk_high;
static hunkUsed_t* hunk_permanent, * hunk_temp;

static byte* s_hunkRaw = NULL;     /* original allocation for debugging/freeing if ever needed */
static byte* s_hunkData = NULL;    /* aligned working pointer */
static size_t      s_hunkTotal;

static size_t      s_zoneTotal;
static size_t      s_smallZoneTotal;

extern fileHandle_t logfile;

/*
==============================================================================

                        ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

The zone calls are pretty much only used for small strings and structures,
all big things are allocated on the hunk.
==============================================================================
*/

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#define ZONEID      0x1d4a11
#define MINFRAGMENT 64

typedef struct zonedebug_s {
    char* label;
    char* file;
    int     line;
    size_t  allocSize;
} zonedebug_t;

typedef struct memblock_s {
    size_t              size;           // including the header and possibly tiny fragments
    int                 tag;            // a tag of 0 is a free block
    struct memblock_s* next, * prev;
    int                 id;             // should be ZONEID
#ifdef ZONE_DEBUG
    zonedebug_t         d;
#endif
} memblock_t;

typedef struct {
    size_t      size;           // total bytes malloced, including header
    size_t      used;           // total bytes used
    memblock_t  blocklist;      // start / end cap for linked list
    memblock_t* rover;
} memzone_t;

// main zone for all "dynamic" memory allocation
memzone_t* mainzone;
// we also have a small zone for small allocations that would only
// fragment the main zone (think of cvar and cmd strings)
memzone_t* smallzone;

void Z_CheckHeap(void);

/*
========================
Z_ClearZone
========================
*/
void Z_ClearZone(memzone_t* zone, int size) {
    memblock_t* block;

    // set the entire zone to one free block
    zone->blocklist.next = zone->blocklist.prev = block =
        (memblock_t*)((byte*)zone + sizeof(memzone_t));
    zone->blocklist.tag = 1;    // in use block
    zone->blocklist.id = 0;
    zone->blocklist.size = 0;
    zone->rover = block;
    zone->size = (size_t)size;
    zone->used = 0;

    block->prev = block->next = &zone->blocklist;
    block->tag = 0;             // free block
    block->id = ZONEID;
    block->size = (size_t)size - sizeof(memzone_t);
}

/*
========================
Z_AvailableZoneMemory
========================
*/
int Z_AvailableZoneMemory(memzone_t* zone) {
    size_t avail = zone->size - zone->used;
    if (avail > (size_t)INT_MAX) {
        return INT_MAX;
    }
    return (int)avail;
}

/*
========================
Z_AvailableMemory
========================
*/
int Z_AvailableMemory(void) {
    return Z_AvailableZoneMemory(mainzone);
}

/*
========================
Z_Free
========================
*/
void Z_Free(void* ptr) {
    memblock_t* block, * other;
    memzone_t* zone;

    if (!ptr) {
        Com_Error(ERR_DROP, "Z_Free: NULL pointer");
    }

    block = (memblock_t*)((byte*)ptr - sizeof(memblock_t));
    if (block->id != ZONEID) {
        Com_Error(ERR_FATAL, "Z_Free: freed a pointer without ZONEID");
    }
    if (block->tag == 0) {
        Com_Error(ERR_FATAL, "Z_Free: freed a freed pointer");
    }
    // if static memory
    if (block->tag == TAG_STATIC) {
        return;
    }

    // check the memory trash tester
    if (*(int*)((byte*)block + block->size - sizeof(int)) != ZONEID) {
        Com_Error(ERR_FATAL, "Z_Free: memory block wrote past end");
    }

    if (block->tag == TAG_SMALL) {
        zone = smallzone;
    }
    else {
        zone = mainzone;
    }

    zone->used -= block->size;

    // set the block to something that should cause problems if it is referenced...
    Com_Memset(ptr, 0xaa, block->size - sizeof(*block));

    block->tag = 0;     // mark as free

    other = block->prev;
    if (!other->tag) {
        // merge with previous free block
        other->size += block->size;
        other->next = block->next;
        other->next->prev = other;
        if (block == zone->rover) {
            zone->rover = other;
        }
        block = other;
    }

    zone->rover = block;

    other = block->next;
    if (!other->tag) {
        // merge the next free block onto the end
        block->size += other->size;
        block->next = other->next;
        block->next->prev = block;
        if (other == zone->rover) {
            zone->rover = block;
        }
    }
}

/*
================
Z_FreeTags
================
*/
void Z_FreeTags(int tag) {
    int         count;
    memzone_t* zone;

    if (tag == TAG_SMALL) {
        zone = smallzone;
    }
    else {
        zone = mainzone;
    }

    count = 0;

    // use the rover as our pointer, because
    // Z_Free automatically adjusts it
    zone->rover = zone->blocklist.next;
    do {
        if (zone->rover->tag == tag) {
            count++;
            Z_Free((void*)(zone->rover + 1));
            continue;
        }
        zone->rover = zone->rover->next;
    } while (zone->rover != &zone->blocklist);
}

/*
================
Z_TagMalloc
================
*/
#ifdef ZONE_DEBUG
void* Z_TagMallocDebug(int size, int tag, char* label, char* file, int line) {
#else
void* Z_TagMalloc(int size, int tag) {
#endif
    size_t      extra, allocSize, totalSize;
    memblock_t* start, * rover, * newb, * base;
    memzone_t* zone;

    if (!tag) {
        Com_Error(ERR_FATAL, "Z_TagMalloc: tried to use a 0 tag");
    }

    if (tag == TAG_SMALL) {
        zone = smallzone;
    }
    else {
        zone = mainzone;
    }

    allocSize = (size_t)size;

    //
    // scan through the block list looking for the first free block
    // of sufficient size
    //
    totalSize = (size_t)size;
    totalSize += sizeof(memblock_t);        // account for size of block header
    totalSize += sizeof(int);               // space for memory trash tester
    totalSize = (totalSize + 7u) & ~((size_t)7u);   // align to 64-bit boundary

    base = rover = zone->rover;
    start = base->prev;

    do {
        if (rover == start) {
#ifdef ZONE_DEBUG
         //   Z_LogHeap();
#endif
            Com_Error(ERR_FATAL,
                "Z_Malloc: failed on allocation of %llu bytes from the %s zone",
                (unsigned long long)totalSize,
                zone == smallzone ? "small" : "main");
            return NULL;
        }

        if (rover->tag) {
            base = rover = rover->next;
        }
        else {
            rover = rover->next;
        }
    } while (base->tag || base->size < totalSize);

    //
    // found a block big enough
    //
    extra = base->size - totalSize;
    if (extra > MINFRAGMENT) {
        // there will be a free fragment after the allocated block
        newb = (memblock_t*)((byte*)base + totalSize);
        newb->size = extra;
        newb->tag = 0;           // free block
        newb->prev = base;
        newb->id = ZONEID;
        newb->next = base->next;
        newb->next->prev = newb;
        base->next = newb;
        base->size = totalSize;
    }

    base->tag = tag;             // no longer a free block
    zone->rover = base->next;    // next allocation will start looking here
    zone->used += base->size;
    base->id = ZONEID;

#ifdef ZONE_DEBUG
    base->d.label = label;
    base->d.file = file;
    base->d.line = line;
    base->d.allocSize = allocSize;
#endif

    // marker for memory trash testing
    * (int*)((byte*)base + base->size - sizeof(int)) = ZONEID;

    return (void*)((byte*)base + sizeof(memblock_t));
}

/*
========================
Z_Malloc
========================
*/
#ifdef ZONE_DEBUG
void* Z_MallocDebug(int size, char* label, char* file, int line) {
#else
void* Z_Malloc(int size) {
#endif
    void* buf;

    // Z_CheckHeap (); // DEBUG

#ifdef ZONE_DEBUG
    buf = Z_TagMallocDebug(size, TAG_GENERAL, label, file, line);
#else
    buf = Z_TagMalloc(size, TAG_GENERAL);
#endif
    Com_Memset(buf, 0, (size_t)size);

    return buf;
}

#ifdef ZONE_DEBUG
void* S_MallocDebug(int size, char* label, char* file, int line) {
    return Z_TagMallocDebug(size, TAG_SMALL, label, file, line);
}
#else
void* S_Malloc(int size) {
    return Z_TagMalloc(size, TAG_SMALL);
}
#endif

/*
========================
Z_CheckHeap
========================
*/
void Z_CheckHeap(void) {
    memblock_t* block;

    for (block = mainzone->blocklist.next; ; block = block->next) {
        if (block->next == &mainzone->blocklist) {
            break;          // all blocks have been hit
        }
        if ((byte*)block + block->size != (byte*)block->next) {
            Com_Error(ERR_FATAL, "Z_CheckHeap: block size does not touch the next block\n");
        }
        if (block->next->prev != block) {
            Com_Error(ERR_FATAL, "Z_CheckHeap: next block doesn't have proper back link\n");
        }
        if (!block->tag && !block->next->tag) {
            Com_Error(ERR_FATAL, "Z_CheckHeap: two consecutive free blocks\n");
        }
    }
}

/*
========================
Z_LogZoneHeap
========================
*/
void Z_LogZoneHeap(memzone_t * zone, char* name) {
#ifdef ZONE_DEBUG
    char        dump[32], * ptr;
    int         i, j;
#endif
    memblock_t* block;
    char        buf[4096];
    size_t      size, allocSize, numBlocks;

    if (!logfile || !FS_Initialized()) {
        return;
    }

    size = 0;
    allocSize = 0;
    numBlocks = 0;

    Com_sprintf(buf, sizeof(buf), "\r\n================\r\n%s log\r\n================\r\n", name);
    FS_Write(buf, strlen(buf), logfile);

    for (block = zone->blocklist.next; block->next != &zone->blocklist; block = block->next) {
        if (block->tag) {
#ifdef ZONE_DEBUG
            ptr = ((char*)block) + sizeof(memblock_t);
            j = 0;
            for (i = 0; i < 20 && (size_t)i < block->d.allocSize; i++) {
                if (ptr[i] >= 32 && ptr[i] < 127) {
                    dump[j++] = ptr[i];
                }
                else {
                    dump[j++] = '_';
                }
            }
            dump[j] = '\0';

            Com_sprintf(buf, sizeof(buf),
                "size = %8llu: %s, line: %d (%s) [%s]\r\n",
                (unsigned long long)block->d.allocSize,
                block->d.file, block->d.line, block->d.label, dump);
            FS_Write(buf, strlen(buf), logfile);
            allocSize += block->d.allocSize;
#endif
            size += block->size;
            numBlocks++;
        }
    }

#ifdef ZONE_DEBUG
    // subtract debug memory
    size -= numBlocks * sizeof(zonedebug_t);
#else
    allocSize = numBlocks * sizeof(memblock_t);
#endif

    Com_sprintf(buf, sizeof(buf), "%llu %s memory in %llu blocks\r\n",
        (unsigned long long)size, name, (unsigned long long)numBlocks);
    FS_Write(buf, strlen(buf), logfile);

    Com_sprintf(buf, sizeof(buf), "%llu %s memory overhead\r\n",
        (unsigned long long)(size - allocSize), name);
    FS_Write(buf, strlen(buf), logfile);
}

/*
========================
Z_LogHeap
========================
*/

// static mem blocks to reduce a lot of small zone overhead
typedef struct memstatic_s {
    memblock_t  b;
    byte        mem[2];
} memstatic_t;

// x64-safe constant initializers
#define MEMSTATIC_BLOCK_SIZE(bytes_)   (((sizeof(memblock_t) + (bytes_)) + 7) & ~7)
#define MEMSTATIC_T_SIZE               ((sizeof(memstatic_t) + 7) & ~7)

// bk001204 - initializer brackets
memstatic_t emptystring =
{ { MEMSTATIC_BLOCK_SIZE(2), TAG_STATIC, NULL, NULL, ZONEID }, { '\0', '\0' } };

memstatic_t numberstring[] = {
    { { MEMSTATIC_T_SIZE, TAG_STATIC, NULL, NULL, ZONEID }, { '0', '\0' } },
    { { MEMSTATIC_T_SIZE, TAG_STATIC, NULL, NULL, ZONEID }, { '1', '\0' } },
    { { MEMSTATIC_T_SIZE, TAG_STATIC, NULL, NULL, ZONEID }, { '2', '\0' } },
    { { MEMSTATIC_T_SIZE, TAG_STATIC, NULL, NULL, ZONEID }, { '3', '\0' } },
    { { MEMSTATIC_T_SIZE, TAG_STATIC, NULL, NULL, ZONEID }, { '4', '\0' } },
    { { MEMSTATIC_T_SIZE, TAG_STATIC, NULL, NULL, ZONEID }, { '5', '\0' } },
    { { MEMSTATIC_T_SIZE, TAG_STATIC, NULL, NULL, ZONEID }, { '6', '\0' } },
    { { MEMSTATIC_T_SIZE, TAG_STATIC, NULL, NULL, ZONEID }, { '7', '\0' } },
    { { MEMSTATIC_T_SIZE, TAG_STATIC, NULL, NULL, ZONEID }, { '8', '\0' } },
    { { MEMSTATIC_T_SIZE, TAG_STATIC, NULL, NULL, ZONEID }, { '9', '\0' } }
};

/*
========================
CopyString

 NOTE: never write over the memory CopyString returns because
       memory from a memstatic_t might be returned
========================
*/
char* CopyString(const char* in) {
    char* out;

    if (!in[0]) {
        return ((char*)&emptystring) + sizeof(memblock_t);
    }
    else if (!in[1]) {
        if (in[0] >= '0' && in[0] <= '9') {
            return ((char*)&numberstring[in[0] - '0']) + sizeof(memblock_t);
        }
    }

    out = (char *)S_Malloc((int)strlen(in) + 1);
    strcpy(out, in);
    return out;
}

/*
=================
Com_Meminfo_f
=================
*/
void Com_Meminfo_f(void) {
    memblock_t* block;
    size_t      zoneBytes, zoneBlocks;
    size_t      smallZoneBytes, smallZoneBlocks;
    size_t      botlibBytes, rendererBytes;
    size_t      unused;

    zoneBytes = 0;
    botlibBytes = 0;
    rendererBytes = 0;
    zoneBlocks = 0;

    for (block = mainzone->blocklist.next; ; block = block->next) {
        if (Cmd_Argc() != 1) {
            Com_Printf("block:%p    size:%7llu    tag:%3i\n",
                (void*)block, (unsigned long long)block->size, block->tag);
        }
        if (block->tag) {
            zoneBytes += (size_t)block->size;
            zoneBlocks++;
            if (block->tag == TAG_BOTLIB) {
                botlibBytes += (size_t)block->size;
            }
            else if (block->tag == TAG_RENDERER) {
                rendererBytes += (size_t)block->size;
            }
        }

        if (block->next == &mainzone->blocklist) {
            break;
        }
        if ((byte*)block + block->size != (byte*)block->next) {
            Com_Printf("ERROR: block size does not touch the next block\n");
        }
        if (block->next->prev != block) {
            Com_Printf("ERROR: next block doesn't have proper back link\n");
        }
        if (!block->tag && !block->next->tag) {
            Com_Printf("ERROR: two consecutive free blocks\n");
        }
    }

    smallZoneBytes = 0;
    smallZoneBlocks = 0;
    for (block = smallzone->blocklist.next; ; block = block->next) {
        if (block->tag) {
            smallZoneBytes += (size_t)block->size;
            smallZoneBlocks++;
        }

        if (block->next == &smallzone->blocklist) {
            break;
        }
    }

    Com_Printf("%8llu bytes total hunk\n", (unsigned long long)s_hunkTotal);
    Com_Printf("%8llu bytes total zone\n", (unsigned long long)s_zoneTotal);
    Com_Printf("\n");
    Com_Printf("%8llu low mark\n", (unsigned long long)hunk_low.mark);
    Com_Printf("%8llu low permanent\n", (unsigned long long)hunk_low.permanent);
    if (hunk_low.temp != hunk_low.permanent) {
        Com_Printf("%8llu low temp\n", (unsigned long long)hunk_low.temp);
    }
    Com_Printf("%8llu low tempHighwater\n", (unsigned long long)hunk_low.tempHighwater);
    Com_Printf("\n");
    Com_Printf("%8llu high mark\n", (unsigned long long)hunk_high.mark);
    Com_Printf("%8llu high permanent\n", (unsigned long long)hunk_high.permanent);
    if (hunk_high.temp != hunk_high.permanent) {
        Com_Printf("%8llu high temp\n", (unsigned long long)hunk_high.temp);
    }
    Com_Printf("%8llu high tempHighwater\n", (unsigned long long)hunk_high.tempHighwater);
    Com_Printf("\n");
    Com_Printf("%8llu total hunk in use\n",
        (unsigned long long)(hunk_low.permanent + hunk_high.permanent));

    unused = 0;
    if (hunk_low.tempHighwater > hunk_low.permanent) {
        unused += hunk_low.tempHighwater - hunk_low.permanent;
    }
    if (hunk_high.tempHighwater > hunk_high.permanent) {
        unused += hunk_high.tempHighwater - hunk_high.permanent;
    }
    Com_Printf("%8llu unused highwater\n", (unsigned long long)unused);
    Com_Printf("\n");
    Com_Printf("%8llu bytes in %llu zone blocks\n",
        (unsigned long long)zoneBytes, (unsigned long long)zoneBlocks);
    Com_Printf("        %8llu bytes in dynamic botlib\n", (unsigned long long)botlibBytes);
    Com_Printf("        %8llu bytes in dynamic renderer\n", (unsigned long long)rendererBytes);
    Com_Printf("        %8llu bytes in dynamic other\n",
        (unsigned long long)(zoneBytes - (botlibBytes + rendererBytes)));
    Com_Printf("        %8llu bytes in small Zone memory\n", (unsigned long long)smallZoneBytes);
}

/*
===============
Com_TouchMemory

Touch all known used data to make sure it is paged in
===============
*/
void Com_TouchMemory(void) {
    int         start, end;
    size_t      i, j;
    volatile int sum;
    memblock_t* block;

    Z_CheckHeap();

    start = Sys_Milliseconds();
    sum = 0;

    j = hunk_low.permanent >> 2;
    for (i = 0; i < j; i += 64) {
        sum += ((int*)s_hunkData)[i];
    }

    i = (s_hunkTotal - hunk_high.permanent) >> 2;
    j = s_hunkTotal >> 2;
    for (; i < j; i += 64) {
        sum += ((int*)s_hunkData)[i];
    }

    for (block = mainzone->blocklist.next; ; block = block->next) {
        if (block->tag) {
            j = ((size_t)block->size) >> 2;
            for (i = 0; i < j; i += 64) {
                sum += ((int*)block)[i];
            }
        }
        if (block->next == &mainzone->blocklist) {
            break;
        }
    }

    end = Sys_Milliseconds();
    Com_Printf("Com_TouchMemory: %i msec\n", end - start);
}

/*
=================
Com_InitSmallZoneMemory
=================
*/
void Com_InitSmallZoneMemory(void) {
    s_smallZoneTotal = 512u * 1024u;

    smallzone = (memzone_t *) calloc(s_smallZoneTotal, 1);
    if (!smallzone) {
        Com_Error(ERR_FATAL, "Small zone data failed to allocate %1.1f megs",
            (float)s_smallZoneTotal / (1024.0f * 1024.0f));
    }
    Z_ClearZone(smallzone, (int)s_smallZoneTotal);
}

/*
=================
Com_InitZoneMemory
=================
*/
void Com_InitZoneMemory(void) {
    cvar_t* cv;

    cv = Cvar_Get("com_zoneMegs", DEF_COMZONEMEGS, CVAR_LATCH | CVAR_ARCHIVE);

    if (cv->integer < 20) {
        s_zoneTotal = 1024u * 1024u * 16u;
    }
    else {
        s_zoneTotal = (size_t)cv->integer * 1024u * 1024u;
    }

    mainzone = (memzone_t*)calloc(s_zoneTotal, 1);
    if (!mainzone) {
        Com_Error(ERR_FATAL, "Zone data failed to allocate %llu megs",
            (unsigned long long)(s_zoneTotal / (1024u * 1024u)));
    }
    Z_ClearZone(mainzone, (int)s_zoneTotal);
}

/*
=================
Hunk_Log
=================
*/
void Hunk_Log(void) {
    hunkblock_t* block;
    char        buf[4096];
    size_t      size, numBlocks;

    if (!logfile || !FS_Initialized())
        return;

    size = 0;
    numBlocks = 0;

    Com_sprintf(buf, sizeof(buf), "\r\n================\r\nHunk log\r\n================\r\n");
    FS_Write(buf, strlen(buf), logfile);

    for (block = hunkblocks; block; block = block->next) {
#ifdef HUNK_DEBUG
        Com_sprintf(buf, sizeof(buf), "size = %8llu: %s, line: %d (%s)\r\n",
            (unsigned long long)block->size, block->file, block->line, block->label);
        FS_Write(buf, strlen(buf), logfile);
#endif
        size += block->size;
        numBlocks++;
    }

    Com_sprintf(buf, sizeof(buf), "%llu Hunk memory\r\n", (unsigned long long)size);
    FS_Write(buf, strlen(buf), logfile);

    Com_sprintf(buf, sizeof(buf), "%llu hunk blocks\r\n", (unsigned long long)numBlocks);
    FS_Write(buf, strlen(buf), logfile);
}

/*
=================
Hunk_SmallLog
=================
*/
void Hunk_SmallLog(void) {
    hunkblock_t* block, * block2;
    char        buf[4096];
    size_t      size, locsize, numBlocks;

    if (!logfile || !FS_Initialized())
        return;

    for (block = hunkblocks; block; block = block->next) {
        block->printed = qfalse;
    }

    size = 0;
    numBlocks = 0;

    Com_sprintf(buf, sizeof(buf), "\r\n================\r\nHunk Small log\r\n================\r\n");
    FS_Write(buf, strlen(buf), logfile);

    for (block = hunkblocks; block; block = block->next) {
        if (block->printed) {
            continue;
        }

        locsize = block->size;
        for (block2 = block->next; block2; block2 = block2->next) {
            if (block->line != block2->line) {
                continue;
            }
            if (Q_stricmp(block->file, block2->file)) {
                continue;
            }
            size += block2->size;
            locsize += block2->size;
            block2->printed = qtrue;
        }

#ifdef HUNK_DEBUG
        Com_sprintf(buf, sizeof(buf), "size = %8llu: %s, line: %d (%s)\r\n",
            (unsigned long long)locsize, block->file, block->line, block->label);
        FS_Write(buf, strlen(buf), logfile);
#endif
        size += block->size;
        numBlocks++;
    }

    Com_sprintf(buf, sizeof(buf), "%llu Hunk memory\r\n", (unsigned long long)size);
    FS_Write(buf, strlen(buf), logfile);

    Com_sprintf(buf, sizeof(buf), "%llu hunk blocks\r\n", (unsigned long long)numBlocks);
    FS_Write(buf, strlen(buf), logfile);
}

/*
=================
Com_InitHunkMemory
=================
*/
void Com_InitHunkMemory(void) {
    cvar_t* cv;
    int         nMinAlloc;
    char* pMsg = NULL;
    uintptr_t   aligned;

    if (FS_LoadStack() != 0) {
        Com_Error(ERR_FATAL, "Hunk initialization failed. File system load stack not zero");
    }

    cv = Cvar_Get("com_hunkMegs", DEF_COMHUNKMEGS, CVAR_LATCH | CVAR_ARCHIVE);

    if (com_dedicated && com_dedicated->integer) {
        nMinAlloc = MIN_DEDICATED_COMHUNKMEGS;
        pMsg = "Minimum com_hunkMegs for a dedicated server is %i, allocating %llu megs.\n";
    }
    else {
        nMinAlloc = MIN_COMHUNKMEGS;
        pMsg = "Minimum com_hunkMegs is %i, allocating %llu megs.\n";
    }

    if (cv->integer < nMinAlloc) {
        s_hunkTotal = (size_t)nMinAlloc * 1024u * 1024u;
        Com_Printf(pMsg, nMinAlloc, (unsigned long long)(s_hunkTotal / (1024u * 1024u)));
    }
    else {
        s_hunkTotal = (size_t)cv->integer * 1024u * 1024u;
    }

    s_hunkRaw = (byte*)calloc(s_hunkTotal + 31u, 1);
    if (!s_hunkRaw) {
        Com_Error(ERR_FATAL, "Hunk data failed to allocate %llu megs",
            (unsigned long long)(s_hunkTotal / (1024u * 1024u)));
    }

    aligned = ((uintptr_t)s_hunkRaw + 31u) & ~((uintptr_t)31u);
    s_hunkData = (byte*)aligned;

    Hunk_Clear();

    Cmd_AddCommand("meminfo", Com_Meminfo_f);
#ifdef ZONE_DEBUG
   // Cmd_AddCommand("zonelog", Z_LogHeap);
#endif
#ifdef HUNK_DEBUG
    Cmd_AddCommand("hunklog", Hunk_Log);
    Cmd_AddCommand("hunksmalllog", Hunk_SmallLog);
#endif
}

/*
====================
Hunk_MemoryRemaining
====================
*/
int Hunk_MemoryRemaining(void) {
    size_t  low, high, remaining;

    low = (hunk_low.permanent > hunk_low.temp) ? hunk_low.permanent : hunk_low.temp;
    high = (hunk_high.permanent > hunk_high.temp) ? hunk_high.permanent : hunk_high.temp;

    remaining = s_hunkTotal - (low + high);

    if (remaining > (size_t)INT_MAX) {
        return INT_MAX;
    }
    return (int)remaining;
}

/*
===================
Hunk_SetMark
===================
*/
void Hunk_SetMark(void) {
    hunk_low.mark = hunk_low.permanent;
    hunk_high.mark = hunk_high.permanent;
}

/*
=================
Hunk_ClearToMark
=================
*/
void Hunk_ClearToMark(void) {
    hunk_low.permanent = hunk_low.temp = hunk_low.mark;
    hunk_high.permanent = hunk_high.temp = hunk_high.mark;
}

/*
=================
Hunk_CheckMark
=================
*/
qboolean Hunk_CheckMark(void) {
    if (hunk_low.mark || hunk_high.mark) {
        return qtrue;
    }
    return qfalse;
}

void CL_ShutdownCGame(void);
void CL_ShutdownUI(void);
void SV_ShutdownGameProgs(void);

/*
=================
Hunk_Clear
=================
*/
void Hunk_Clear(void) {

#ifndef DEDICATED
    CL_ShutdownCGame();
    CL_ShutdownUI();
#endif
    SV_ShutdownGameProgs();
#ifndef DEDICATED
    void CIN_CloseAllVideos();
    CIN_CloseAllVideos();
#endif

    hunk_low.mark = 0;
    hunk_low.permanent = 0;
    hunk_low.temp = 0;
    hunk_low.tempHighwater = 0;

    hunk_high.mark = 0;
    hunk_high.permanent = 0;
    hunk_high.temp = 0;
    hunk_high.tempHighwater = 0;

    hunk_permanent = &hunk_low;
    hunk_temp = &hunk_high;

    Com_Printf("Hunk_Clear: reset the hunk ok\n");
#ifdef HUNK_DEBUG
    hunkblocks = NULL;
#endif
}

static void Hunk_SwapBanks(void) {
    hunkUsed_t* swap;

    if (hunk_temp->temp != hunk_temp->permanent) {
        return;
    }

    if (hunk_temp->tempHighwater - hunk_temp->permanent >
        hunk_permanent->tempHighwater - hunk_permanent->permanent) {
        swap = hunk_temp;
        hunk_temp = hunk_permanent;
        hunk_permanent = swap;
    }
}

/*
=================
Hunk_Alloc

Allocate permanent (until the hunk is cleared) memory
=================
*/
#ifdef HUNK_DEBUG
void* Hunk_AllocDebug(int size, ha_pref preference, char* label, char* file, int line) {
#else
void* Hunk_Alloc(int size, ha_pref preference) {
#endif
    void* buf;
    size_t  allocSize;

    if (s_hunkData == NULL) {
        Com_Error(ERR_FATAL, "Hunk_Alloc: Hunk memory system not initialized");
    }

    if (preference == h_dontcare || hunk_temp->temp != hunk_temp->permanent) {
        Hunk_SwapBanks();
    }
    else {
        if (preference == h_low && hunk_permanent != &hunk_low) {
            Hunk_SwapBanks();
        }
        else if (preference == h_high && hunk_permanent != &hunk_high) {
            Hunk_SwapBanks();
        }
    }

    allocSize = (size_t)size;

#ifdef HUNK_DEBUG
    allocSize += sizeof(hunkblock_t);
#endif

    allocSize = (allocSize + 31u) & ~((size_t)31u);

    if (hunk_low.temp + hunk_high.temp + allocSize > s_hunkTotal) {
#ifdef HUNK_DEBUG
        Hunk_Log();
        Hunk_SmallLog();
#endif
        Com_Error(ERR_DROP, "Hunk_Alloc failed on %llu", (unsigned long long)allocSize);
    }

    if (hunk_permanent == &hunk_low) {
        buf = (void*)(s_hunkData + hunk_permanent->permanent);
        hunk_permanent->permanent += allocSize;
    }
    else {
        hunk_permanent->permanent += allocSize;
        buf = (void*)(s_hunkData + s_hunkTotal - hunk_permanent->permanent);
    }

    hunk_permanent->temp = hunk_permanent->permanent;

    Com_Memset(buf, 0, allocSize);

#ifdef HUNK_DEBUG
    {
        hunkblock_t* block;

        block = (hunkblock_t*)buf;
        block->size = allocSize - sizeof(hunkblock_t);
        block->file = file;
        block->label = label;
        block->line = line;
        block->next = hunkblocks;
        hunkblocks = block;
        buf = ((byte*)buf) + sizeof(hunkblock_t);
    }
#endif

    return buf;
}

/*
=================
Hunk_AllocateTempMemory
=================
*/
void* Hunk_AllocateTempMemory(int size) {
    void* buf;
    hunkHeader_t* hdr;
    size_t          allocSize;

    if (s_hunkData == NULL) {
        return Z_Malloc(size);
    }

    Hunk_SwapBanks();

    allocSize = (((size_t)size + 3u) & ~((size_t)3u)) + sizeof(hunkHeader_t);

    if (hunk_temp->temp + hunk_permanent->permanent + allocSize > s_hunkTotal) {
        Com_Error(ERR_DROP, "Hunk_AllocateTempMemory: failed on %llu",
            (unsigned long long)allocSize);
    }

    if (hunk_temp == &hunk_low) {
        buf = (void*)(s_hunkData + hunk_temp->temp);
        hunk_temp->temp += allocSize;
    }
    else {
        hunk_temp->temp += allocSize;
        buf = (void*)(s_hunkData + s_hunkTotal - hunk_temp->temp);
    }

    if (hunk_temp->temp > hunk_temp->tempHighwater) {
        hunk_temp->tempHighwater = hunk_temp->temp;
    }

    hdr = (hunkHeader_t*)buf;
    buf = (void*)(hdr + 1);

    hdr->magic = HUNK_MAGIC;
    hdr->size = allocSize;

    return buf;
}

/*
==================
Hunk_FreeTempMemory
==================
*/
void Hunk_FreeTempMemory(void* buf) {
    hunkHeader_t* hdr;

    if (s_hunkData == NULL) {
        Z_Free(buf);
        return;
    }

    hdr = ((hunkHeader_t*)buf) - 1;
    if (hdr->magic != HUNK_MAGIC) {
        Com_Error(ERR_FATAL, "Hunk_FreeTempMemory: bad magic");
    }

    hdr->magic = HUNK_FREE_MAGIC;

    if (hunk_temp == &hunk_low) {
        if ((byte*)hdr == (s_hunkData + hunk_temp->temp - hdr->size)) {
            hunk_temp->temp -= hdr->size;
        }
        else {
            Com_Printf("Hunk_FreeTempMemory: not the final block\n");
        }
    }
    else {
        if ((byte*)hdr == (s_hunkData + s_hunkTotal - hunk_temp->temp)) {
            hunk_temp->temp -= hdr->size;
        }
        else {
            Com_Printf("Hunk_FreeTempMemory: not the final block\n");
        }
    }
}

/*
=================
Hunk_ClearTempMemory
=================
*/
void Hunk_ClearTempMemory(void) {
    if (s_hunkData != NULL) {
        hunk_temp->temp = hunk_temp->permanent;
    }
}

/*
=================
Hunk_Trash
=================
*/
void Hunk_Trash(void) {

}