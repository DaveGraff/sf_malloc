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

sf_header *get_header(sf_block *blockPtr){
    return &blockPtr->header;
}

size_t get_size(sf_header *header){
    return ((*header) / 4) * 4;
}

int is_alloc(sf_block *blockPtr){
    return (*get_header(blockPtr)) & 2;
}

sf_block *get_next(sf_block *blockPtr){
    size_t size = get_size(get_header(blockPtr));
    return (void *)blockPtr + size;
}

sf_block *get_prev(sf_block *blockPtr){
    sf_footer footer = blockPtr->prev_footer;
    size_t size = footer ^ sf_magic();
    size = (size / 4) * 4;
    return (void *)blockPtr - size;
}

sf_footer *get_footer(sf_block *blockPtr){
    return &get_next(blockPtr)->prev_footer;
}

void set_prev_alloc(sf_block *blockPtr, int state){
    sf_block *nextBlock = get_next(blockPtr);
    nextBlock->header = get_size(get_header(nextBlock)) + is_alloc(nextBlock) + state;

    //Not epilogue
    if((void *)get_next(nextBlock) < sf_mem_end()){
        // sf_block *nextNextBlock = get_next(nextBlock);
        // nextNextBlock->prev_footer = (nextBlock->header) ^ sf_magic();
        size_t *footer = get_footer(nextBlock);
        *footer = (nextBlock->header) ^ sf_magic();
    }
}

int is_valid(void *pp){
    //We want to know where the block starts
    pp += 8;

    if(pp == NULL)
        return 0;
    if(pp < sf_mem_start() + 40){
        return 0;
    }
    if(pp > sf_mem_end() - 8)
        return 0;

    pp -= 8;
    sf_block *ptr = pp;

    if(!is_alloc(ptr))
        return 0;
    if(get_size(get_header(ptr)) < 32)
        return 0;

    if(*get_header(ptr) != (*get_footer(ptr) ^ sf_magic()))
        return 0;

    if(!(*get_header(ptr) & 1)){
        if(is_alloc(get_prev(ptr)))
            return 0;
    }

    return 1;
}

//Handles initial setup by calling mem_grow, creating the
//prologue and epilogue, and creating the inital free lists
//Returns -1 in error, 0 otherwise
int initial_setup() {
    int *rval = sf_mem_grow();
    if(rval == NULL)
        return -1;


    //Set prologue
    size_t *heapPtr = sf_mem_start();
    heapPtr++;

    *heapPtr = 0x23;

    heapPtr += 3;
    *heapPtr = (0x23) ^ sf_magic();

    //Set epilogue
    sf_epilogue *epi = sf_mem_end();
    epi--;
    epi->header = 0x2;

    //Initialize free lists
    for (int i = 0; i < NUM_FREE_LISTS; ++i)
    {
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
    }

    size_t blockSize = sf_mem_end() - sf_mem_start() - 48;

    //Footer is implicitly prol footer
    sf_block *first = (sf_block *)heapPtr;
    first->header = blockSize + 1;

    //Set first block footer
    epi--;
    epi->header = (blockSize + 1) ^ sf_magic();

    //Add to big boi free list
    int last = NUM_FREE_LISTS -1;

    sf_free_list_heads[last].body.links.next = first;
    sf_free_list_heads[last].body.links.prev = first;

    first->body.links.next = &sf_free_list_heads[last];
    first->body.links.prev = &sf_free_list_heads[last];

    return 0;
}

void place_block(sf_block *blockPtr, size_t size){
    int num[8] = {1,2,4,8,16,32,64,128};

    int chosen = 8;

    int M = 32;

    for (int i = 0; i < 8; ++i){
        if (size <= num[i]*M){
            chosen = i;
            break;
        }
    }

    //Set references
    sf_block *last = sf_free_list_heads[chosen].body.links.prev;

    last->body.links.next = blockPtr;
    blockPtr->body.links.prev = last;

    blockPtr->body.links.next = &sf_free_list_heads[chosen];
    sf_free_list_heads[chosen].body.links.prev = blockPtr;
}

//Increases the size of sf_mem, moves the epilogue, and coalesces
//with any space before it
int increase_size() {
    //Get ptr to old epilogue
    sf_epilogue *old_epi = sf_mem_end();
    old_epi--;

    //Don't bother to do anything else until we know it can succeed
    int *rval = sf_mem_grow();
    if(rval == NULL)
        return -1;

    //Set epilogue
    sf_epilogue *epi = sf_mem_end();
    epi--;
    epi->header = 0x2;

    size_t blockSize;
    sf_block *blockPtr = NULL;
    //Previous block free
    if ((size_t)old_epi->header & 1){
        //Get to footer before epilogue
        old_epi--;
        //Get footer size
        size_t prev_block_size = (old_epi->header) ^ sf_magic();
        prev_block_size = (prev_block_size / 4) * 4;

        blockPtr = (void *)old_epi - prev_block_size;
        blockSize = (void *)epi - (void *)blockPtr - 8;

    } else {
        blockSize = (void *)epi - (void *)old_epi - 8;
        old_epi--;
        blockPtr = (void *)old_epi;

    }

    blockPtr->header = blockSize + 1;


    size_t *footer = get_footer(get_next(blockPtr));
    *footer = *get_header(blockPtr) ^ sf_magic();


    place_block(blockPtr, blockSize);

    return 0;
}

void remove_from_free_lists(sf_block *blockPtr){
    for (int i = 0; i < NUM_FREE_LISTS; ++i){
        sf_block *freePtr = sf_free_list_heads[i].body.links.next;

        while (freePtr != &sf_free_list_heads[i]) {
            if(freePtr == blockPtr){
                sf_block *prev = freePtr->body.links.prev;
                sf_block *next = freePtr->body.links.next;

                prev->body.links.next = next;
                next->body.links.prev = prev;
                return;
            }

            freePtr = freePtr->body.links.next;
        }
    }
}

void coalesce_and_place(sf_block *blockPtr){
    //Is left free?
    // size_t footer = (size_t)blockPtr->prev_footer;
    // footer = footer ^ sf_magic();
    if(!(is_alloc(get_prev(blockPtr)))){
        sf_block *blockl = get_prev(blockPtr);

        size_t sizel = get_size(get_header(blockl));
        size_t sizem = get_size(get_header(blockPtr));

        remove_from_free_lists(blockl);

        size_t *header = (size_t *)get_header(blockl);
        *header = sizel + sizem + 1;
        size_t *footer = (size_t *)get_footer(blockPtr);
        *footer = *header ^ sf_magic();

        blockPtr = blockl;
    }



    //Is right free?
    sf_block *blockr = get_next(blockPtr);

    if( !((blockr->header)&2)  ){
        size_t sizem = get_size(get_header(blockPtr));

        size_t sizer = get_size(get_header(blockr));

        remove_from_free_lists(blockr);

        blockPtr->header = sizem + sizer + 1;
        size_t *footer = get_footer(blockr);
        *footer = (blockPtr->header) ^ sf_magic();
    }

    place_block(blockPtr, get_size(get_header(blockPtr)));
}

//Returns a valid block pointer, or NULL if not possible
sf_block *find_block(size_t size){
    //Find a block
    sf_block *unsplit = NULL;

    while (unsplit == NULL){
        for(int i = 0; i < NUM_FREE_LISTS; i++){
            sf_block *blockPtr = sf_free_list_heads[i].body.links.next;

            //Something in the list
            while (blockPtr != &sf_free_list_heads[i]) {

                //We got one!
                if(get_size(get_header(blockPtr)) >= size){
                    //Remove from list
                    (blockPtr->body.links.prev)->body.links.next = blockPtr->body.links.next;
                    (blockPtr->body.links.next)->body.links.prev = blockPtr->body.links.prev;

                    return blockPtr;
                }

                blockPtr = blockPtr->body.links.next;
            }
        }

        //Increase size & go again
        if(increase_size() == -1)
            return NULL;
    }

    //This should never happen
    return NULL;
}

void handle_splinter(sf_block *unsplit, size_t size){
    //Split block if possible & replace
    size_t totalSize = get_size(get_header(unsplit));

    size_t leftovers = totalSize - size;

    //Block must be split
    if(leftovers >= 32){
        totalSize -= leftovers;

        //Set new size
        int prev_was_alloc = (unsplit->header) & 1;
        unsplit->header = totalSize + 2 + prev_was_alloc;

        //Get new block
        sf_block *splitBlock = (void *)unsplit + totalSize;

        //Set prev footers & header
        splitBlock->prev_footer = (unsplit->header) ^ sf_magic();
        splitBlock->header = leftovers + 1;

        //Set footer of splitbock
        size_t *footer = get_footer(splitBlock);
        *footer = (splitBlock->header) ^ sf_magic();

        // set_prev_alloc(splitBlock, 0);

        coalesce_and_place(splitBlock);
    }
}

void *sf_malloc(size_t size) {
    if(size == 0){
        return NULL;
    }

    //Include size of header & footer
    size += 16;

    //Size must be a multiple of 16 and at least 32
    if (size % 16 != 0)
        size += 16 - (size % 16);

    if (size < 32)
        size = 32;

    //First call
    if(sf_mem_start() == sf_mem_end())
        if(initial_setup() == -1)
            return NULL;

    //Get a valid block
    sf_block *unsplit = find_block(size);
    if (unsplit == NULL)
        return NULL;

    //Set allocated
    unsplit->header += 2;
    size_t *footer = get_footer(unsplit);
    *footer = (unsplit->header) ^ sf_magic();

    handle_splinter(unsplit, size);

    return &(unsplit->body.payload);
}

void sf_free(void *pp) {
    //Assume pointer was given from payload
    pp -= 16;

    //Check if valid
    if(!is_valid(pp)){
        abort();
    }


    //Change alloc state
    sf_block *blockPtr = pp;
    // set_prev_alloc(blockPtr, 0);
    blockPtr->header -= 2;

    size_t *footer = get_footer(blockPtr);
    *footer = (blockPtr->header) ^ sf_magic();

    //Attempt to coalesce
    coalesce_and_place(blockPtr);
    return;
}

void *sf_realloc(void *pp, size_t rsize) {
    if (pp == 0){
        sf_errno = EINVAL;
        return NULL;
    }

    //Assume pointer was given from payload
    pp -= 16;

    //Check if valid
    if(!is_valid(pp)){
        sf_errno = EINVAL;
        return NULL;
    }

    //malloc
    if(rsize < 1){
        sf_free(pp);
        return NULL;
    }

    //if new size is smaller...
    if (rsize < get_size(get_header(pp))){
       handle_splinter(pp, rsize);
       return pp;
    }

    void *newPtr = sf_malloc(rsize);
    //Move to payload
    newPtr+=16;

    //memcpy
    size_t size = get_size(get_header(pp));
    //Back to payload
    pp += 16;
    memcpy(newPtr, pp, size);

    //free old ptr
    sf_free(pp);

    return newPtr;
}
