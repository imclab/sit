#include "sit.h"

vstring *
vstring_new() {
  return calloc(1, sizeof(vstring));
}

void
vstring_append(vstring *vstr, pstring *pstr) {
  VStringNode *node = malloc(sizeof(*node));
  node->strings = NULL;
  node->off = 0;
  if(vstr->node) {
    node->off = vstr->node->off + vstr->node->pstr.len;
  }
  node->prev = vstr->node;
  vstr->node = node;
  pstring_copulate(&node->pstr, pstr);
}

void
_v(pstring *out, VStringNode *node, long off, long *flags) {
  if(!node) return;
  
  int len = out->len;
  long reloff = off - node->off;
  long relend = reloff + len;
  
  assert(reloff <= off + node->pstr.len);
  
  if (reloff >= 0 && relend <= node->pstr.len) {
    // the requested string exists entirely within one buffer, therefore
    // we do a pointer-only allocation and GTFO.
    DEBUG("vstring_get handled simply");
    out->val = node->pstr.val + reloff;
    *flags = 1;
    return;
  } else if (relend <= 0) {
    _v(out, node->prev, off, flags);
    DEBUG("nothing more to do")
    // the entire string has been filled already
    return;
  }
  
  assert(!*flags);
   
  if(reloff < 0) {
    // grab previous chunk
    _v(out, node->prev, off, flags);
  } else {
    DEBUG("vstring_get must malloc");
    out->val = malloc(len);
    *flags = out->val;
  }
  
  long start = reloff > 0 ? reloff : 0; 
  if(reloff >0) reloff = 0;
  long node_remaining = node->pstr.len - start;
  long string_remaining = len + (reloff < 0 ? reloff : 0); 
  long cplen = node_remaining > string_remaining ? string_remaining : node_remaining;
  
  DEBUG("copying %ld bytes into offset %d", cplen, -reloff);
  memcpy((void *)(out->val - reloff), node->pstr.val + start, cplen);
  
  // just need to fill in part of the string
  if (relend <= node->pstr.len) {
    // we own the end of this string, so register it
    DEBUG("registering vstring_get-created string: %.*s (%ld)", out->len, out->val, out->val);
    assert(*flags == out->val);
    ll_add(&node->strings, out->val);
  }
  
}

void
vstring_get(pstring *target, vstring *vstr, long off) {
  long flags = 0;
  if (off + target->len > vstring_size(vstr)) {
    target->len = -1;
    WARN("vstring_get called past end of vstring");
  } else {
    DEBUG("vstring_get called for (%ld, %ld) (+%ld, %ld avail)", off, target->len, vstr->off, vstring_size(vstr));
    _v(target, vstr->node, off + vstr->off, &flags);
  }
}

void 
vstring_node_free(VStringNode *node) {
  if(node->prev) {
    vstring_node_free(node->prev);
  }
  DEBUG("vstring_node_free (%ld, %ld)", node->off, node->pstr.len);
  ll_free(node->strings);
  free(node->pstr.val);
  free(node);
}

long
vstring_size(vstring *vstr) {
  if(vstr->node) {
    return vstr->node->off + vstr->node->pstr.len - vstr->off;
  } else {
    return 0;
  }
}

void
vstring_shift(vstring *vstr, long off) {
  vstr->off += off;
  VStringNode *node = vstr->node;
  VStringNode *next = NULL;
  while(node) {
    if(node->off + node->pstr.len < vstr->off) {
      if(next) next->prev = NULL;
      vstring_node_free(node);
      return;
    }
    next = node;
    node = node->prev;
  }
}

void
vstring_free(vstring *vstr) {
  if(vstr->node) vstring_node_free(vstr->node);
  free(vstr);
}