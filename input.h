#ifndef INPUT_H_INCLUDED
#define INPUT_H_INCLUDED

#include "engine.h"
#include "query_parser.h"

typedef struct Output {
	void *data;
  pstring *delimiter;
  void (*write)(struct Output *output, pstring *message);
  void (*close)(struct Output *output);
} Output;

typedef enum { REGISTERING, QUERYING } QueryParserMode;

typedef struct Input {
  struct Engine      *engine;
	Parser      *parser;
  QueryParser *qparser;
  
  QueryParserMode qparser_mode;
  
  Callback *doc_acker;
  Callback *found_callback;
  
  Output         *output;

	void *data;

} Input;

#define STRLEN(s) (sizeof(s)/sizeof(s[0]) - 1)

/**
 * A non-allocating printf-ish buffer write.  It assumes a constant prefix,
 * a printf formatstring, and varargs.  The printf component is limited to
 * 128 bytes, because fo the constant pre-allocated buffer size.
 */
#define WRITE_OUT(_s, _v, ...)  do {                        \
    static char cbuf[] = _s                                 \
    "                                "                      \
    "                                "                      \
    "                                "                      \
    "                                ";                     \
    static pstring buf = { cbuf, STRLEN(_s) };              \
    static char *insert = cbuf + STRLEN(_s);                \
    int written = snprintf(insert, 128, _v, ##__VA_ARGS__); \
    buf.len = STRLEN(_s) + written;                         \
    output->write(output, &buf);                            \
  } while(0)

Input *
input_new(struct Engine *engine, long buffer_size);

void
input_free(Input *input);

void
input_consume(struct Input *input, pstring *pstr);

void 
input_end_stream(struct Input *input);

#endif