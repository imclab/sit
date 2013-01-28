#include "plist.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>


/**
 * TODO: 
 * - allocate next block if block full
 * - resize blocks?  Or just maintain count-ish of elements and add bigger blocks
 * - increment version numbers across regions
 */

plist_pool *
plist_pool_new(long size) {
	plist_pool *pool = malloc(sizeof(plist_pool));
	pool->capacity = size;
	pool->buffer = malloc(size);
	pool->lowest_plist = (plist *)((char *) pool->buffer + size);
	pool->next_block = (plist_block *)pool->buffer;
	pool->default_block_size = 512; // bytes
	pool->region_size = size / 16;
	pool->current_version = 0;
	pool->min_version = 0;
	pool->free_list = NULL;
	return pool;
}

void
check_region_overlap(plist_pool *pool) {
	(void) pool;
	// TODO: impl
}

plist *
plist_new(plist_pool *pool) {
	assert(pool);
	plist *pl;
	if(pool->free_list == NULL) {
	 	pl = --(pool->lowest_plist);
	} else {
		pl = (plist *) pool->free_list;
		pool->free_list = pool->free_list->next;
	}
	check_region_overlap(pool);
	pl->pool = pool;
	pl->last_block = NULL;
	pl->last_version = -1;
	return pl;
}

void 
plist_free(plist *pl) {
	plist_pool *pool = pl->pool;
	if(pool) {
		free_list *node = (free_list *) pl;
		node->next = pool->free_list;
		pool->free_list = node;
	}
}

plist_block *
plist_append_block(plist *pl) {
	plist_block *block = pl->pool->next_block;
	block->prev = pl->last_block;
	block->prev_version = pl->last_version;
	block->entries_count = 0;
	block->next = NULL;
	if(block->prev && block->prev_version >= pl->pool->min_version) {
		block->size = block->prev->size;
		if(block->prev_version == pl->pool->current_version) {
			block->size *= 2;
		}
		block->prev->next = block;
	} else {
		block->size = pl->pool->default_block_size;
	}
	pl->pool->next_block = ((char *) block) + block->size;
	pl->last_block = block;
	pl->last_version = pl->pool->current_version;
	return block;
}

bool
plist_block_is_full(plist_block *block) {
	long size = block->size;
	char *base = (char *) block;
	char *next = (char *) &block->entries[block->entries_count + 1];
	return next - base > size;
}

void
plist_append_entry(plist *pl, plist_entry *entry) {
	plist_block *block;
	if(pl->last_block == NULL || 
	   pl->last_version < pl->pool->current_version ||
	   plist_block_is_full(pl->last_block)) {
		block = plist_append_block(pl);
	} else {
		block = pl->last_block;
	}
	memcpy(&block->entries[block->entries_count++], entry, sizeof(plist_entry));
}

void
_count(void *data, plist_entry *entry) {
	(*(int *)data)++;
	(void) entry;
}

long
plist_size(plist *plist) {
	plist_iterator counter;
	int count = 0;
	counter.handle = _count;
	counter.data = &count;
	plist_reach(plist, &counter);
	return count;
}

void
plist_each(plist *pl, plist_iterator *iterator) {
	//TODO: impl
	(void) pl;
	(void) iterator;
}

void
plist_reach(plist *pl, plist_iterator *iterator) {
	int min = pl->pool->min_version;
	if(pl->last_version >= min) {
		plist_block *block = pl->last_block;
		while(block) {
			for (int i = block->entries_count - 1; i >= 0; i--) {
				iterator->handle(iterator->data, &block->entries[i]);
			}
			if(block->prev_version >= min) {
				block = block->prev;
			} else {
				break;
			}
		}
	}
}
