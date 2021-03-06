#include "sit.h"

PlistPool *
plist_pool_new(long size) {
	PlistPool *pool = malloc(sizeof(PlistPool));
	pool->capacity = size;
	pool->buffer = malloc(size);
	pool->next_block = (PlistBlock *)pool->buffer;
	pool->default_block_size = 512; // bytes
	pool->region_size = size / 16;
	pool->region_count = 16;
	pool->current_version = 0;
	pool->min_version = 0;
	return pool;
}

void
plist_pool_free(PlistPool *pool) {
  free(pool->buffer);
  free(pool);
}

long
plist_cursor_document_id(Cursor *scursor) {
  PlistCursor *cursor = (PlistCursor *)scursor;
  if(cursor->as_cursor.data != NULL) {
    return ((PlistEntry *)cursor->as_cursor.data)->doc;
  } else {
    return -1;
  }
}

bool
plist_cursor_prev(Cursor *scursor) {
  PlistCursor *cursor = (PlistCursor *)scursor;
  if(cursor->exhausted) {
    return false;
  }
  Plist *pl = cursor->plist;
  PlistPool *pool = pl->pool;
  PlistEntry * entry = cursor->as_cursor.data;
  
  if(cursor->block == NULL) {
    if(pl->last_block && pl->last_version >= pool->min_version) {
      cursor->block = pl->last_block;
    } else {
      cursor->exhausted = true;
      cursor->as_cursor.data = NULL;
      return false;
    }
  }
  
  if(entry == NULL) {
    int size = cursor->block->entries_count;
    if (size == 0) {
      cursor->exhausted = true;
      cursor->as_cursor.data = NULL;
      return false;
    } else {
      cursor->as_cursor.data = &cursor->block->entries[size - 1];
      return true;
    }
  } else if (entry == &cursor->block->entries[0]) {
    if(cursor->block->prev && cursor->block->prev_version >= pool->min_version) {
      cursor->block = cursor->block->prev;
      int size = cursor->block->entries_count;
      if (size == 0) {
        cursor->exhausted = true;
        cursor->as_cursor.data = NULL;
        return false;
      } else {
        cursor->as_cursor.data = &cursor->block->entries[size - 1];
        return true;
      }      
    } else {
      cursor->exhausted = true;
      cursor->as_cursor.data = NULL;
      return false;
    }
  } else {
    cursor->as_cursor.data = ((PlistEntry*) cursor->as_cursor.data) - 1;
    return true;
  }
}

bool
plist_cursor_next(Cursor *scursor) {
  PlistCursor *cursor = (PlistCursor *)scursor;
  (void) cursor;
  return false;
}

PlistEntry *
plist_cursor_entry(Cursor *scursor) {
  PlistCursor *cursor = (PlistCursor *)scursor;
  return cursor->exhausted ? NULL : cursor->as_cursor.data;
}


long 
plist_cursor_seek_lte(Cursor *scursor, long value) {
  PlistCursor *cursor = (PlistCursor *)scursor;
	long doc;
	while((doc = plist_cursor_document_id(&cursor->as_cursor)) > value) {
		if(!plist_cursor_prev(&cursor->as_cursor)) {
			doc = -1;
			break;
		}
	}	
	return doc;
}


Plist *
plist_new(PlistPool *pool) {
	assert(pool);
	Plist *pl = malloc(sizeof(Plist));
	pl->pool = pool;
	pl->last_block = NULL;
	pl->last_version = INT_MIN;
	return pl;
}

void 
plist_free(Plist *pl) {
  (void) pl;
  free(pl);
}

PlistCursor *
plist_cursor_new(Plist *pl) {
  assert(pl);
  PlistCursor *cursor = malloc(sizeof(PlistCursor));
  cursor->as_cursor.prev = plist_cursor_prev;
  cursor->as_cursor.next = plist_cursor_next;
  cursor->as_cursor.id = plist_cursor_document_id;
  cursor->as_cursor.seek_lte = plist_cursor_seek_lte;
  cursor->as_cursor.data = NULL;
  cursor->plist = pl;
  cursor->block = NULL;
  cursor->exhausted = false;
  return cursor;
}

void
plist_cursor_free(PlistCursor *cursor) {
  free(cursor);
}


PlistBlock *
plist_append_block(Plist *pl) {
	PlistPool *pool = pl->pool;
	char *next_block = pool->next_block;
	int current_region = pool->current_version % pool->region_count;
	char *region_cutoff = ((char*)pool->buffer) + current_region * pool->region_size;

	int size = pool->default_block_size;
	if(pl->last_block && pl->last_version >= pool->min_version) {
		size = pl->last_block->size;
		if(pl->last_version == pool->current_version && size * 2 < pool->region_size) {
			size *= 2;
		}
	}
	
	if(next_block + size > region_cutoff) {
		pool->current_version++;
		pool->min_version = pool->current_version - pool->region_count + 1;
		next_block = ((char*)pool->buffer) + current_region * pool->region_size;
	}
	
	PlistBlock *block = (void *) next_block;
	block->prev = pl->last_block;
	block->prev_version = pl->last_version;
	block->entries_count = 0;
	block->next = NULL;
	if(block->prev && block->prev_version >= pl->pool->min_version) {
		block->prev->next = block;
	}
	block->size = size;
	pl->pool->next_block = ((char *) block) + block->size;
	pl->last_block = block;
	pl->last_version = pl->pool->current_version;
	return block;
}

bool
plist_block_is_full(PlistBlock *block) {
	long size = block->size;
	char *base = (char *) block;
	char *next = (char *) &block->entries[block->entries_count + 1];
	return next - base > size;
}

void
plist_append_entry(Plist *pl, PlistEntry *entry) {
	PlistBlock *block;
	if(pl->last_block == NULL || 
	   pl->last_version < pl->pool->current_version ||
	   plist_block_is_full(pl->last_block)) {
		block = plist_append_block(pl);
	} else {
		block = pl->last_block;
	}
	memcpy(&block->entries[block->entries_count++], entry, sizeof(PlistEntry));
}

void
_count(Callback *cb, void *entry) {
	(*(int *)cb->user_data)++;
	(void) entry;
}

long
plist_size(Plist *plist) {
	Callback counter;
	int count = 0;
	counter.handler = _count;
	counter.user_data = &count;
	plist_reach(plist, &counter);
	return count;
}



void
plist_each(Plist *pl, Callback *iterator) {
	//TODO: impl
	(void) pl;
	(void) iterator;
}

void
plist_reach(Plist *pl, Callback *iterator) {
	int min = pl->pool->min_version;
	if(pl->last_version >= min) {
		PlistBlock *block = pl->last_block;
		while(block) {
			for (int i = block->entries_count - 1; i >= 0; i--) {
				iterator->handler(iterator, &block->entries[i]);
			}
			if(block->prev_version >= min) {
				block = block->prev;
			} else {
				break;
			}
		}
	}
}
