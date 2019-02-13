/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"


sf_header *findfit(size_t adjustedsize, size_t requestedSize);
sf_header *splitfreeblock(size_t adjustedSize, size_t freeBlockSize, size_t requestedSize, sf_header *theBlock);
void *getMoreMemory();
void *setPrologueAndEpilogue(void *ptr);
void *addToFreeList(sf_header *blockPtr);
void addToFreeSentinel(sf_free_list_node *sentinelNode, sf_header *freeHeader);
void *setFreeHeaderAndFooter(void *ptr, unsigned int allocated, unsigned int prevAllocated, unsigned int blockSize);
sf_header *setHeader(sf_header *theBlock, unsigned size, unsigned requestedSize, unsigned allocated);
void setPrevAllocated(unsigned allocated, void* blockPtr);
void removeFromFreeList(sf_header *theBlock);
void setEpilogue(void *newPagePtr);
void *coalesce(sf_header *theBlock);
sf_header *splitblock(size_t adjustedSize, size_t sizeOfBlock, size_t requestedSize, sf_header *theBlock, void *payload);

#define ROWSIZE 8 //in bytes
#define DOUBLEROWSIZE 16 //in bytes
#define ALLOCATEDHEADERSIZE 8 //in bytes
#define FOOTERSIZE 8 //in bytes
#define FREEHEADERFOOTERSIZE 32 //in bytes

unsigned int allocated = 1;
unsigned int notAllocated = 0;
sf_prologue *the_prologue;
sf_epilogue *the_epilogue;

void *sf_malloc(size_t size)
{

    size_t adjusted_size;

    if(size == 0)
    {
        return NULL;
    }

    if(size + ALLOCATEDHEADERSIZE < FREEHEADERFOOTERSIZE) //less than 32 bytes
    {
        adjusted_size = FREEHEADERFOOTERSIZE;
    }
    else
    {
        adjusted_size = size + ALLOCATEDHEADERSIZE;
        while(adjusted_size % DOUBLEROWSIZE != 0)
        {
            adjusted_size = adjusted_size + 1;
        }
    }

    sf_header *allocatedBlockPtr = findfit(adjusted_size, size);
    if(allocatedBlockPtr != NULL)
    {
        return &allocatedBlockPtr->payload;
    }
    sf_errno = ENOMEM;
    return NULL;
}

void sf_free(void *pp)
{
    if(pp == NULL)
    {
        return abort();
    }
    if(pp < sf_mem_start() + sizeof(sf_prologue) || pp > sf_mem_end() - sizeof(sf_epilogue))
    {
        return abort();
    }
    sf_header *theBlock = (void*)pp - ROWSIZE;
    unsigned size = theBlock->info.block_size << 4;
    if(size % 16 != 0 || size < 32)
    {
        return abort();
    }
    unsigned prevAllocated = theBlock->info.prev_allocated;
    if(theBlock->info.allocated == 0)
    {
        return abort();
    }
    if(theBlock->info.requested_size + ROWSIZE > size)
    {
        return abort();
    }
    if(prevAllocated == 0)
    {
        sf_footer *prevFooter = (void*)theBlock - ROWSIZE;
        unsigned prevSize = prevFooter->info.block_size;
        sf_header *prevHeader = (void*)theBlock - prevSize;
        if(prevHeader->info.allocated != 0 || prevFooter->info.allocated != 0)
        {
            return abort();
        }
    }
    setFreeHeaderAndFooter(theBlock, notAllocated, prevAllocated, size);
    //addToFreeList(theBlock);
    sf_header *newBlock = coalesce(theBlock);
    addToFreeList(newBlock);
    unsigned newSize = newBlock->info.block_size << 4;
    sf_header *nextBlock = (void*)newBlock + newSize;
    setPrevAllocated(notAllocated, nextBlock);
}

void *sf_realloc(void *pp, size_t rsize)
{
    if(pp == NULL)
    {
        sf_errno = EINVAL;
        return NULL;
    }
    if(pp < sf_mem_start() + sizeof(sf_prologue) || pp > sf_mem_end() - sizeof(sf_epilogue))
    {
        sf_errno = EINVAL;
        return NULL;
    }
    sf_header *theBlock = (void*)pp - ROWSIZE;
    unsigned size = theBlock->info.block_size << 4;
    if(size % 16 != 0 || size < 32)
    {
        sf_errno = EINVAL;
        return NULL;
    }
    unsigned prevAllocated = theBlock->info.prev_allocated;
    unsigned reqSize = theBlock->info.requested_size;
    if(theBlock->info.allocated == 0)
    {
        sf_errno = EINVAL;
        return NULL;
    }
    if(theBlock->info.requested_size + ROWSIZE > size)
    {
        sf_errno = EINVAL;
        return NULL;
    }
    if(prevAllocated == 0)
    {
        sf_footer *prevFooter = (void*)theBlock - ROWSIZE;
        unsigned prevSize = prevFooter->info.block_size;
        sf_header *prevHeader = (void*)theBlock - prevSize;
        if(prevHeader->info.allocated != 0 || prevFooter->info.allocated != 0)
        {
            sf_errno = EINVAL;
            return NULL;
        }
    }
    if(size == 0)
    {
        free(pp);
        return NULL;
    }
    //CHECKS ARE DONE
    if(reqSize == rsize)
    {
        return pp;
    }
    if(rsize > reqSize)
    {
        size_t adjustedsize = rsize + ROWSIZE;
        while(adjustedsize % 16 != 0)
        {
            adjustedsize = adjustedsize + 1;
        }
        if(adjustedsize <= size || adjustedsize <= FREEHEADERFOOTERSIZE)
        {
            memcpy(pp, pp, rsize);
            theBlock->info.requested_size = rsize;
            return pp;
        }
        void *newBlock = sf_malloc(rsize);
        if(newBlock == NULL)
        {
            sf_errno = ENOMEM;
            return NULL;
        }
        memcpy(newBlock, pp, rsize);
        sf_header *newHeader = (void*)newBlock - ROWSIZE;
        newHeader->info.requested_size = rsize;
        sf_free(pp);
        return newBlock;
    }
    //(rsize < reqSize)
    size_t adjustedsize = rsize + ROWSIZE;
    while(adjustedsize % 16 != 0)
    {
        adjustedsize = adjustedsize + 1;
    }
    if(adjustedsize < FREEHEADERFOOTERSIZE)
    {
        adjustedsize = FREEHEADERFOOTERSIZE;
    }
    if(adjustedsize + FREEHEADERFOOTERSIZE > size)
    {
        memcpy(pp, pp, rsize);
        theBlock->info.requested_size = rsize;
        return pp;
    }
    sf_header *newPP = splitblock(adjustedsize, size, rsize, theBlock, pp);
    return &newPP->payload;
}

sf_header *splitblock(size_t adjustedSize, size_t sizeOfBlock, size_t requestedSize, sf_header *theBlock, void *payload)
{
    memcpy(&theBlock->payload, payload, requestedSize);
    size_t freeSize = sizeOfBlock - adjustedSize;
    sf_header *allocatedHeader = setHeader(theBlock, adjustedSize, requestedSize, allocated);
    sf_header *freeHeader = (void*)theBlock + adjustedSize;
    setFreeHeaderAndFooter(freeHeader, notAllocated, allocated, freeSize);
    coalesce(freeHeader);
    addToFreeList(freeHeader);
    return allocatedHeader;
}


//START OF HELPER FUNCTIONS
sf_header *findfit(size_t adjustedsize, size_t requestedSize)
{
    sf_free_list_node *currentNode;
    currentNode = &sf_free_list_head;
    currentNode = currentNode->next;
    while(currentNode != &sf_free_list_head)
    {
        if(adjustedsize <= currentNode->size && currentNode->head.links.next != &currentNode->head) //enough space and there is a valid node
        {
            return splitfreeblock(adjustedsize, currentNode->size, requestedSize, currentNode->head.links.next);
        }
        currentNode = currentNode->next;
    }
    //NOT ENOUGH MEMORY
    if(getMoreMemory() == NULL)
    {
        return NULL;
    }
    return findfit(adjustedsize, requestedSize);
}

sf_header *splitfreeblock(size_t adjustedSize, size_t freeBlockSize, size_t requestedSize, sf_header *theBlock)
{
    // ALLOCATE ENTIRE BLOCK
    if(freeBlockSize - adjustedSize < FREEHEADERFOOTERSIZE)
    {
        removeFromFreeList(theBlock);
        return setHeader(theBlock, freeBlockSize, requestedSize, allocated);
    }
    removeFromFreeList(theBlock);
    sf_header *allocatedHeader = setHeader(theBlock, adjustedSize, requestedSize, allocated);
    sf_header *freeHeader = (void*)theBlock + adjustedSize;
    setFreeHeaderAndFooter(freeHeader, notAllocated, allocated, freeBlockSize - adjustedSize);
    coalesce(freeHeader);
    addToFreeList(freeHeader);
    return allocatedHeader;
}

int freeBlockExists(sf_header *theBlock)
{
    size_t theBlockSize = theBlock->info.block_size << 4;
    sf_free_list_node *currentNode;
    currentNode = &sf_free_list_head;
    currentNode = currentNode->next;
    while(currentNode != &sf_free_list_head && theBlockSize >= currentNode->size)
    {
        if(currentNode->size == theBlockSize) // A LISTNODE OF THE REQUIRED SIZE IS FOUND
        {
            sf_header *thisHeader = currentNode->head.links.next;
            while(thisHeader != &currentNode->head)
            {
                if(thisHeader == theBlock)
                {
                    return 1;
                }
                thisHeader = thisHeader->links.next;
            }
            return 0;
        }
        currentNode = currentNode->next;
    }
    return 0;
}
void removeFromFreeList(sf_header *theBlock)
{
    if(freeBlockExists(theBlock) == 1)
    {
        sf_header *prevBlock = theBlock->links.prev;
        sf_header *nextBlock = theBlock->links.next;
        prevBlock->links.next = nextBlock;
        nextBlock->links.prev = prevBlock;
    }
}

sf_header *setHeader(sf_header *theBlock, unsigned size, unsigned requestedSize, unsigned allocated_status)
{
    theBlock->info.allocated = allocated_status;
    theBlock->info.requested_size = requestedSize;
    theBlock->info.block_size = size >> 4;
    setPrevAllocated(allocated, (void*)theBlock + size); //SET NEXT BLOCK's PREV ALLOCATED AS ALLOCATED
    return theBlock;
}

void setPrevAllocated(unsigned allocated_status, void* blockPtr)
{
    if(blockPtr == (void*)sf_mem_end() - sizeof(sf_epilogue))
    {
        sf_epilogue *epilogue = blockPtr;
        epilogue->footer.info.prev_allocated = allocated_status;
    }
    else
    {
        sf_header *theHeader = blockPtr;
        theHeader->info.prev_allocated = allocated_status;
    }
}

//RETURNS NULL IF NOT ENOUGH MEMORY, OTHERWISE RETURNS POINTER TO THE NEW FREE NODE OTHERWISE
void *getMoreMemory()
{
    void *oldPageEpilogue;
    oldPageEpilogue = (void*)sf_mem_end() - sizeof(sf_epilogue);
    void *newPagePtr;
    newPagePtr = sf_mem_grow();
    if(newPagePtr == NULL)
    {
        return NULL;
    }
    if(oldPageEpilogue <= (void*)sf_mem_start())
    {
        return addToFreeList(setPrologueAndEpilogue(newPagePtr));
    }
    else
    {
        setEpilogue(sf_mem_end() - sizeof(sf_epilogue));
        sf_footer *epilogueFooter = oldPageEpilogue;
        unsigned prevAllocated = epilogueFooter->info.prev_allocated;
        setFreeHeaderAndFooter(oldPageEpilogue, notAllocated, prevAllocated, PAGE_SZ);
        addToFreeList(coalesce(oldPageEpilogue));
        return oldPageEpilogue;
    }
    return NULL;
}

void *coalesce(sf_header *theBlock)
{
    unsigned prevAllocated = theBlock->info.prev_allocated;
    sf_header *nextBlock = (void*)theBlock + (theBlock->info.block_size << 4);
    unsigned nextAllocated = nextBlock->info.allocated;

    if(prevAllocated == 1 && nextAllocated == 1)
    {
        removeFromFreeList(theBlock);
        return theBlock;
    }
    else
    {
        if(prevAllocated == 1 && nextAllocated == 0)
        {
            removeFromFreeList(theBlock);
            removeFromFreeList(nextBlock);
            unsigned size = (theBlock->info.block_size << 4) + (nextBlock->info.block_size << 4);
            //addToFreeList(setFreeHeaderAndFooter(theBlock, notAllocated, prevAllocated, size));
            setFreeHeaderAndFooter(theBlock, notAllocated, prevAllocated, size);
            return coalesce(theBlock);
        }
        else
        {
            if(prevAllocated == 0 && nextAllocated == 1)
            {
                sf_footer *prevFooter = (void*)theBlock - ROWSIZE;
                unsigned prevSize = prevFooter->info.block_size << 4;
                sf_header *prevBlock = (void*)theBlock - prevSize;
                prevAllocated = prevBlock->info.prev_allocated;
                removeFromFreeList(theBlock);
                removeFromFreeList(prevBlock);
                unsigned size = (theBlock->info.block_size << 4) + prevSize;
                //addToFreeList(setFreeHeaderAndFooter(prevBlock, notAllocated, prevAllocated, size));
                setFreeHeaderAndFooter(prevBlock, notAllocated, prevAllocated, size);
                return coalesce(prevBlock);
            }
            else
            {
                if(prevAllocated == 0 && nextAllocated == 0)
                {
                    sf_footer *prevFooter = (void*)theBlock - ROWSIZE;
                    unsigned prevSize = prevFooter->info.block_size << 4;
                    sf_header *prevBlock = (void*)theBlock - prevSize;
                    prevAllocated = prevBlock->info.prev_allocated;
                    removeFromFreeList(prevBlock);
                    removeFromFreeList(theBlock);
                    removeFromFreeList(nextBlock);
                    unsigned size = (theBlock->info.block_size << 4) + prevSize + (nextBlock->info.block_size << 4);
                    //addToFreeList(setFreeHeaderAndFooter(prevBlock, notAllocated, prevAllocated, size));
                    setFreeHeaderAndFooter(prevBlock, notAllocated, prevAllocated, size);
                    return coalesce(prevBlock);
                }
                else
                {
                    return NULL; //SHOULD NEVER GET HERE
                }
            }
        }
    }
}

void setEpilogue(void *newPagePtr)
{
    sf_epilogue *newEpilogue = newPagePtr;
    sf_footer *newEpilogueFooter = newPagePtr;
    newEpilogue->footer = *newEpilogueFooter;
    newEpilogue->footer.info.allocated = 1;
    newEpilogue->footer.info.prev_allocated = 0;
}
// RETURNS POINTER TO FREE HEADER
void *setPrologueAndEpilogue(void *ptr)
{
    sf_prologue *the_prologue = sf_mem_start();
    sf_epilogue *the_epilogue = sf_mem_end() - sizeof(sf_epilogue);
    sf_header *prologueHeader = sf_mem_start();
    sf_footer *prologueFooter = sf_mem_start() + DOUBLEROWSIZE;
    sf_footer *epilogueFooter = sf_mem_end() - sizeof(sf_epilogue);
    the_prologue->header = *prologueHeader;
    the_prologue->footer = *prologueFooter;
    the_epilogue->footer = *epilogueFooter;
    the_prologue->header.info.allocated = 1;
    the_prologue->footer.info.allocated = 1;
    the_epilogue->footer.info.allocated = 1;
    the_epilogue->footer.info.prev_allocated = 0;
    unsigned int freeBlockSize = PAGE_SZ - sizeof(sf_prologue) - sizeof(sf_epilogue);
    return setFreeHeaderAndFooter(sf_mem_start() + sizeof(sf_prologue), notAllocated, allocated, freeBlockSize);
}

// RETURN POINTER TO FREE NODE
void *addToFreeList(sf_header *blockPtr)
{
    size_t theBlockSize = blockPtr->info.block_size << 4;
    sf_free_list_node *currentNode;
    currentNode = &sf_free_list_head;
    currentNode = currentNode->next;
    while(currentNode != &sf_free_list_head && theBlockSize >= currentNode->size)
    {
        if(currentNode->size == theBlockSize) // A LISTNODE OF THE REQUIRED SIZE IS FOUND
        {
            addToFreeSentinel(currentNode, blockPtr);
            return blockPtr;
        }
        currentNode = currentNode->next;
    }
    sf_free_list_node *newListNode = sf_add_free_list(theBlockSize, currentNode);
    addToFreeSentinel(newListNode, blockPtr);
    return blockPtr;
}

void *setFreeHeaderAndFooter(void *ptr, unsigned int allocated_status, unsigned int prevAllocated, unsigned int blockSize)
{
    sf_header *freeBlockHeader = ptr;
    freeBlockHeader->info.allocated = allocated_status;
    freeBlockHeader->info.prev_allocated = prevAllocated;
    freeBlockHeader->info.block_size = blockSize >> 4;
    freeBlockHeader->info.requested_size = notAllocated;
    sf_footer *freeBlockFooter = ptr + blockSize - FOOTERSIZE;
    freeBlockFooter->info.allocated = allocated_status;
    freeBlockFooter->info.prev_allocated = prevAllocated;
    freeBlockFooter->info.block_size = blockSize >> 4;
    freeBlockFooter->info.requested_size = notAllocated;
    return freeBlockHeader;
}

void addToFreeSentinel(sf_free_list_node *sentinelNode, sf_header *freeHeader)
{
    sf_header *nextHeader = sentinelNode->head.links.next;
    sf_header *sentinelHeader = &sentinelNode->head;
    //SET PREV OF NEXT HEADER TO THE NEW HEADER
    nextHeader->links.prev = freeHeader;
    //SET NEXT OF SENTINEL TO THE NEW HEADER
    //sentinelNode->head.links.next = freeHeader;
    sentinelHeader->links.next = freeHeader;
    //SET NEXT AND PREV OF THE FREEHEADER
    freeHeader->links.prev = sentinelHeader;
    freeHeader->links.next = nextHeader;
}