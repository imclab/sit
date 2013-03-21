#include "sit.h"

#define COMMAND_LIMIT 16

void
_input_error_found(struct ProtocolHandler *handler, pstring *message) {
  Input *input = handler->data;
  Output *output = input->output;
  pstring escaped;
  json_escape(&escaped, message);
  SMALL_OUT("{\"status\": \"error\", \"message\": \"", "%.*s\"}", escaped.len, escaped.val);
  free(escaped.val);
}

void
_close_on_error(struct ProtocolHandler *handler, pstring *message) {
  (void) message;
  Input *input = handler->data;
  Output *output = input->output;
  output->close(output);
}

void
_close_on_error2(Callback *cb, void *message) {
  ProtocolHandler *handler = cb->user_data;
  _close_on_error(handler, message);
}

int
extract_string(pstring *target, pstring *haystack, int off) {
  for (int i = off; i < haystack->len; i++) {
    if (haystack->val[i] == ' ' || haystack->val[i] == '\r') {
      target->val = haystack->val + off;
      target->len = i - off;
      return i + 1;
    }
  }
  target->val = haystack->val + off;
  target->len = haystack->len - off;
  return haystack->len;
}

void 
_parse_command(ProtocolParser *parser, pstring *pstr) {
  pstring cmd;
  pstring more;
  int off = 0;
  off = extract_string(&cmd, pstr, off);
  more.len = pstr->len - off;
  more.val = pstr->val + off;
  if(!cmd.len) return; // empty line
  if(*(more.val + more.len - 1) == '\r') more.len--;
  parser->handler->command_found(parser->handler, &cmd, &more);
}

void 
_line_consume(ProtocolParser *parser, pstring *pstr) {  
  ProtocolHandler *handler = parser->handler;
  if(parser->state == FORCE_DATA) {
    handler->data_found(handler, pstr);
    return;
  }
  
  char *buf;
  pstring tmp = {
    pstr->val,
    pstr->len
  };
  while ((buf = memchr(tmp.val, '\n', tmp.len))) {
    tmp.len = buf - tmp.val;
    if(tmp.val[0] == '{' || parser->state != COMPLETE) {
      handler->data_found(handler, &tmp);
      if(parser->state != FORCE_DATA) {
        handler->data_complete(handler);
        parser->state = COMPLETE;
      }
    } else {
      _parse_command(parser, &tmp);
    }
    tmp.val += tmp.len + 1;
    tmp.len = pstr->len - (tmp.val - pstr->val);
  }
  if(tmp.len){
    if(parser->state == COMPLETE && tmp.val[0] != '{') {
      handler->error_found(handler, c2pstring("Command was too long"));
    } else {
      handler->data_found(handler, &tmp);
      parser->state = PARTIAL;
    }
  }
}

void
_line_end_stream(ProtocolParser *parser) {
  (void) parser;
}

void
_dump_handler(struct Callback *self, void *data) {
  Query *query = data;
  Output *output = self->user_data;
  output->write(output, query_to_s(query));
}

void
_raw_found_handler(Callback *callback, void *data) {
  long doc_id = *(long*)data;
  Input *input = callback->user_data;
  Engine *engine = input->engine;
  Output *output = input->output;
  pstring *doc = engine_get_document(engine, doc_id);
  if(doc) output->write(output, doc);
}

void
_raw_query_handler(Callback *callback, void *data) {
  Query *query = data;
  Input *input = callback->user_data;
  Output *output = input->output;
  Engine * engine = input->engine;
  if(input->qparser_mode == REGISTERING) {
    query->callback = callback_new(_raw_found_handler, input);
    engine_register(engine, query);
    query->callback = NULL; // disassociate from gc.
  } else {
    query->callback = callback_new(_raw_found_handler, input);
    ResultIterator *iter = engine_search(engine, query);
    while(result_iterator_prev(iter) && (query->limit-- != 0)) {
      result_iterator_do_callback(iter);
    }
    result_iterator_free(iter);
  }   
}

void 
_raw_noop(Callback *cb, void *data) {}

pstring _empty = {"", 0};

void
_input_command_found(struct ProtocolHandler *handler, pstring *command, pstring *more) {
  DEBUG("found cmd:  %.*s", command->len, command->val);
  Input *input = handler->data;
  Output *output = input->output;
  Engine *engine = input->engine;
  
  if(!cpstrcmp("register", command)) {
    input->qparser_mode = REGISTERING;
    INFO("registering: %.*s", more->len, more->val);
    query_parser_consume(input->qparser, more);
    query_parser_reset(input->qparser);
  } else if(!cpstrcmp("query", command)) {
    input->qparser_mode = QUERYING;
    query_parser_consume(input->qparser, more);
    query_parser_reset(input->qparser);
  } else if(!cpstrcmp("raw", command)) {
    INFO("entering raw mode");
    handler->error_found = _close_on_error;
    query_parser_free(input->qparser);
    input->qparser = query_parser_new(callback_new(_raw_query_handler, input));
    callback_free(input->parser->on_document);
    callback_free(input->parser->on_error);
    callback_free(input->doc_acker);
  	input->parser->on_document = callback_new(_raw_noop, input);
  	input->parser->on_error = callback_new(_close_on_error2, handler);
    input->doc_acker = callback_new(_raw_noop, input);
    output->delimiter = &_empty;
  } else if(!cpstrcmp("release_task", command)) {
    long task_id = strtol(more->val, NULL, 10);
    bool success = engine_release_task(engine, task_id);
    if(success) {
      SMALL_OUT("{\"status\": \"ok\", \"message\": \"unregistered\", \"task_id\": ", "%ld}", task_id);
    } else {
      SMALL_OUT("{\"status\": \"error\", \"message\": \"not found\", \"task_id\": ", "%ld}", task_id);
    }
  } else if(!cpstrcmp("connect", command)) {
    Task *task = client_task_new(engine, more);
    if(task) {
      pstring *json = task_to_json(task);
      SMALL_OUT("{\"status\": \"ok\", \"message\": \"added\", \"details: ", "%.*s}", json->len, json->val);
      pstring_free(json);    
    } else {
      SMALL_OUT("{\"status\": \"error\", \"message\": \"", "%s\"}", strerror(errno));
    }
  } else if(!cpstrcmp("tail", command)) {
    Task *task = tail_task_new(engine, more, 1.);
    pstring *json = task_to_json(task);
    SMALL_OUT("{\"status\": \"ok\", \"message\": \"added\", \"details: ", "%.*s}", json->len, json->val);
    pstring_free(json);
  } else if(!cpstrcmp("tasks", command)) {
    SMALL_OUT("{\"status\": \"ok\", \"message\": \"begin\"}", "");
    dictIterator * iterator = dictGetIterator(engine->tasks);
  	dictEntry *next;
  	while((next = dictNext(iterator))) {
      Task *task = dictGetKey(next);
      pstring *json = task_to_json(task);
      SMALL_OUT("{\"status\": \"ok\", details: ", "%.*s}", json->len, json->val);
      pstring_free(json);
  	}
    dictReleaseIterator(iterator);
    SMALL_OUT("{\"status\": \"ok\", \"message\": \"complete\"}", "");
  } else if(!cpstrcmp("tell", command)) {  
    long task_id = strtol(more->val, NULL, 10);
    if(more->val[0] == '$' && more->val[1] == '!') {
      task_id = task_last_id();
    }
    Task *task = engine_get_task(engine, task_id);
    if(task) {
      if(task->tell) {
        char *space = memchr(more->val, ' ', more->len);
        if(space) {
          pstring message = { space + 1, more->len - (space - more->val) };
          char *n = message.val + message.len - 1;
          *(n) = '\n';
          task->tell(task, &message);
          SMALL_OUT("{\"status\": \"ok\", \"message\": \"success\"}", "");
        } else {
          SMALL_OUT("{\"status\": \"error\", \"message\": \"invalid tell message\"", ""); 
        }
      } else {
        SMALL_OUT("{\"status\": \"error\", \"message\": \"task doesn't accept input\", \"task_id\": ", "%ld}", task_id); 
      }
    } else {
      SMALL_OUT("{\"status\": \"error\", \"message\": \"not found\", \"task_id\": ", "%ld}", task_id); 
    }
  } else if(!cpstrcmp("stream", command)) {
    Parser *parser = engine_new_stream_parser(engine, more);
    if(parser) {
      parser->on_document = input->parser->on_document;
      parser->on_error = input->parser->on_error;
      parser_free(input->parser);
      input->parser = parser;
      handler->parser->state = FORCE_DATA;      
      SMALL_OUT("{\"status\": \"ok\", \"message\": \"streaming\"}", "");
    } else {
      pstring json;
      json_escape(&json, more);
      OUT("{\"status\": \"error\", \"message\": \"no stream parser for %.*s\"}", json.len, json.val);
    }
  } else if(!cpstrcmp("unregister", command)) {
    long query_id = strtol(more->val, NULL, 10);
    bool success = engine_unregister(input->engine, query_id);
    if(success) {
      SMALL_OUT("{\"status\": \"ok\", \"message\": \"unregistered\", \"query_id\": ", "%ld}", query_id);
    } else {
      SMALL_OUT("{\"status\": \"error\", \"message\": \"not found\", \"query_id\": ", "%ld}", query_id);
    }
  } else if(!cpstrcmp("get", command)) {
    long doc_id = strtol(more->val, NULL, 10);
    pstring *doc = engine_get_document(input->engine, doc_id);
    if(doc) {
      pstring json;
      json_escape(&json, doc);
      OUT("{\"status\": \"ok\", \"message\": \"get success\", \"doc\": \"%.*s\"}", json.len, json.val);
    } else {
      SMALL_OUT("{\"status\": \"error\", \"message\": \"not found\", \"doc_id\": ", "%ld}", doc_id);
    }
  } else if(!cpstrcmp("close", command)) {
    input->output->close(input->output);
  } else if(TEST_MODE && !cpstrcmp("dump", command)) {
    Callback *cb = callback_new(_dump_handler, output);
    engine_each_query(input->engine,  cb);
    callback_free(cb);
#ifdef HAVE_EV_H
  } else if(TEST_MODE && !cpstrcmp("stop", command)) {
    INFO("stopping now!\n");
    input->output->close(input->output);
    ev_unloop(ev_default_loop(0), EVUNLOOP_ALL);
    engine_free(input->engine);
    INFO("stopped\n");
#endif
  } else {
    pstring *buf = pstringf("Unknown command: %.*s", command->len, command->val);
    handler->error_found(handler, buf);
    pstring_free(buf);
  }
}

void
_input_data_found(struct ProtocolHandler *handler, pstring *data) {
  DEBUG("found data: %.*s\n", data->len, data->val);
  Input *input = handler->data;
  input_consume(input, data);
}

void
_input_data_complete(struct ProtocolHandler *handler) {
  (void) handler;
}

ProtocolParser *
line_input_protocol_new(Input *input) {
  ProtocolParser * parser = line_protocol_new();
  ProtocolHandler *handler = parser->handler;
  parser->data = NULL;
  handler->data = input;
  handler->command_found = _input_command_found;
  handler->data_found    = _input_data_found   ;
  handler->data_complete = _input_data_complete;
  handler->error_found   = _input_error_found  ;
  return parser;
}

void
line_input_protocol_free(ProtocolParser *parser) {
  free(parser->handler);
  free(parser);
}

ProtocolParser *
line_protocol_new() {
  ProtocolParser *parser = calloc(1, sizeof(ProtocolParser));
  ProtocolHandler *handler = calloc(1, sizeof(ProtocolHandler));
  parser->handler = handler;
  handler->parser = parser;
  parser->consume = _line_consume;
  parser->end_stream = _line_end_stream;
  handler->command_found = NULL;
  handler->data_found = NULL;
  handler->data_complete = NULL;
  handler->error_found = NULL;
  parser->data = NULL;
  handler->data = NULL;
  return parser;
}
