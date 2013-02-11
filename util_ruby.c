#include "util_ruby.h"
#include "dict.h"
#include "sit.h"
#include "sit_ruby.h"
#include <stdbool.h>

value_holder root = {
  0, 
  NULL
};

const void *
vwrap(VALUE value) {
	value_holder *h = malloc(sizeof(value_holder));
	h->val = value;
  h->next = root.next;
  root.next = h;
	return h;
}

VALUE
vunwrap(const void *vp) {
	value_holder *h = (value_holder *) vp;
	return h->val;
}


void 
markall() {
  value_holder *h = &root;
  while(h->val) {
    rb_gc_mark(h->val);
    h = h->next;
  }
}

VALUE
rbc_isTestMode(VALUE class) {
  return isTestMode() ? Qtrue : Qfalse;
}

VALUE
rbc_setTestMode(VALUE class, VALUE val) {
  setTestMode(val == Qtrue);
  return Qnil;
}