/*
 * async modules
 * Copyright (C) 2012 kazuya kawaguchi. See Copyright Notice in kyotocabinet.h
 */

#define BUILDING_NODE_EXTENSION

#include "async.h"
#include "debug.h"
#include <assert.h>

using namespace v8;
using namespace kyotocabinet;
namespace kc = kyotocabinet;


enum kc_async_kind_t {
  KC_ASYNC_VISIT_FULL,
  KC_ASYNC_VISIT_EMPTY,
  KC_ASYNC_FILE_PROCESS,
};

#define KC_ASYNC_REQ_FIELD    \
  Persistent<Object> cb;      \
  AtomicInt64 flag;           \
  AtomicInt64 done;           \
  kc_async_kind_t type;       \


// async base request
typedef struct kc_async_base_req_s {
  KC_ASYNC_REQ_FIELD;
} kc_async_base_req_t;

// visitor request
typedef struct kc_async_visitor_req_s {
  KC_ASYNC_REQ_FIELD;
  bool writable;
  char *key;
  char *value;
  size_t key_size;
  size_t value_size;
  char *rv;
  size_t sp;
} kc_async_visitor_req_t;

// file processor request
typedef struct kc_async_file_proc_req_s {
  KC_ASYNC_REQ_FIELD;
  int64_t count;
  int64_t size;
  char *path;
  bool rv;
} kc_async_file_proc_req_t;


static pthread_mutex_t async_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t async_cond = PTHREAD_COND_INITIALIZER;
static uv_once_t kc__async_init_once_guard = UV_ONCE_INIT;
static bool async_init = false;
static uv_async_t async_req_notifier;
static uv_idle_t async_req_done_notifier;


AsyncVisitor::AsyncVisitor(Persistent<Object> &cb, bool writable)
  : writable_(writable), cb_(cb) {
  TRACE("fields: writable_ = %d, cb_ = %p\n", writable_, &cb_);
}

AsyncVisitor::~AsyncVisitor() {
  TRACE("fields: writable_ = %d, cb_ = %p\n", writable_, &cb_);
}

const char* AsyncVisitor::visit_full(const char *kbuf, size_t ksiz,
                                     const char *vbuf, size_t vsiz, size_t *sp) {
  TRACE("arguments: kbuf = %s, ksiz = %d, vbuf = %s, vsiz = %d, sp = %d(%p)\n", kbuf, ksiz, vbuf, vsiz, *sp, sp);

  kc_async_visitor_req_t *req = (kc_async_visitor_req_t *)malloc(sizeof(kc_async_visitor_req_t));
  req->cb = cb_;
  req->flag.set(0);
  req->done.set(0);
  req->type = KC_ASYNC_VISIT_FULL;
  req->writable = writable_;
  req->key = (char *)kbuf;
  req->key_size = ksiz;
  req->value = (char *)vbuf;
  req->value_size = vsiz;
  req->rv = (char *)NOP;
  req->sp = *sp;

  TRACE("before: rv = %s(%p), sp = %d\n", req->rv, req->rv, req->sp);

  TRACE("lock ...\n");
  pthread_mutex_lock(&async_mtx);

  async_req_notifier.data = req;
  int ret = uv_async_send(&async_req_notifier);
  TRACE("uv_async_send: ret = %d\n", ret);

  TRACE("callback wating ... \n");
  if (req->done.get() == 0) {
    req->flag.set(1);
    int ret = pthread_cond_wait(&async_cond, &async_mtx);
    TRACE("cond wait result: ret = %d\n", ret);
  }
  TRACE("... callback done\n");

  pthread_mutex_unlock(&async_mtx);
  TRACE("... unlock\n");

  TRACE("after: rv = %p, sp = %d\n", req->rv, req->sp);
  const char *rv = (const char *)req->rv;
  if (rv != NOP && rv != REMOVE) {
    *sp = req->sp;
  }

  free(req);

  return rv;
}

const char* AsyncVisitor::visit_empty(const char *kbuf, size_t ksiz, size_t *sp) {
  TRACE("arguments: kbuf = %s, ksiz = %d, sp = %d\n", kbuf, ksiz, *sp);

  kc_async_visitor_req_t *req = (kc_async_visitor_req_t *)malloc(sizeof(kc_async_visitor_req_t));
  req->cb = cb_;
  req->flag.set(0);
  req->done.set(0);
  req->type = KC_ASYNC_VISIT_EMPTY;
  req->writable = writable_;
  req->key = (char *)kbuf;
  req->key_size = ksiz;
  req->rv = (char *)NOP;
  req->sp = *sp;

  TRACE("before: rv = %s(%p), sp = %d\n", req->rv, req->rv, req->sp);

  TRACE("lock ...\n");
  pthread_mutex_lock(&async_mtx);

  async_req_notifier.data = req;
  int ret = uv_async_send(&async_req_notifier);
  TRACE("uv_async_send: ret = %d\n", ret);

  TRACE("callback wating ... \n");
  if (req->done.get() == 0) {
    req->flag.set(1);
    int ret = pthread_cond_wait(&async_cond, &async_mtx);
    TRACE("cond wait result: ret = %d\n", ret);
  }
  TRACE("... callback done\n");

  pthread_mutex_unlock(&async_mtx);
  TRACE("... unlock\n");

  TRACE("after: rv = %p, sp = %d\n", req->rv, req->sp);
  const char *rv = (const char *)req->rv;
  if (rv != NOP && rv != REMOVE) {
    *sp = req->sp;
  }

  free(req);

  return rv;
}

void AsyncVisitor::visit_before() {
  TRACE("\n");
}

void AsyncVisitor::visit_after() {
  TRACE("\n");
}


AsyncFileProcessor::AsyncFileProcessor(Persistent<Object> &cb) : cb_(cb) {
  TRACE("fields: cb_ = %p\n", &cb_);
}

AsyncFileProcessor::~AsyncFileProcessor() {
  TRACE("fields: cb_ = %p\n", &cb_);
}

bool AsyncFileProcessor::process(const std::string &path, int64_t count, int64_t size) {
  TRACE("arguments: path = %s, count = %ld, size = %ld\n", path.c_str(), count, size);

  kc_async_file_proc_req_t *req = (kc_async_file_proc_req_t *)malloc(sizeof(kc_async_file_proc_req_t));
  req->cb = cb_;
  req->flag.set(0);
  req->done.set(0);
  req->type = KC_ASYNC_FILE_PROCESS;
  req->count = count;
  req->size = size;
  req->path = (char *)path.c_str();
  req->rv = false;

  TRACE("before: rv = %d\n", req->rv);

  TRACE("lock ...\n");
  pthread_mutex_lock(&async_mtx);

  async_req_notifier.data = req;
  int ret = uv_async_send(&async_req_notifier);
  TRACE("uv_async_send: ret = %d\n", ret);

  TRACE("callback wating ... \n");
  if (req->done.get() == 0) {
    req->flag.set(1);
    int ret = pthread_cond_wait(&async_cond, &async_mtx);
    TRACE("cond wait result: ret = %d\n", ret);
  }
  TRACE("... callback done\n");

  pthread_mutex_unlock(&async_mtx);
  TRACE("... unlock\n");

  TRACE("after: rv = %d\n", req->rv);
  bool rv = req->rv;

  free(req);

  return rv;
}


static void kc_async_req_done_notifier_cb(uv_idle_t *notifier, int status) {
  TRACE("notifier = %p, status = %d\n", notifier, status);

  kc_async_base_req_t *req = static_cast<kc_async_base_req_t*>(notifier->data);
  assert(req != NULL);

  if (req->flag.get() == 1) {
    int ret = pthread_cond_signal(&async_cond);
    TRACE("cond siglal: ret = %d\n", ret);
    req->done.set(1);
    uv_idle_stop(notifier);
  }
}

static void kc_async_req_notifier_cb(uv_async_t *notifier, int status) {
  HandleScope scope;
  TRACE("notifier = %p, status = %d\n", notifier, status);
  assert(notifier->data != NULL);

  switch (((kc_async_base_req_t *)notifier->data)->type) {
    case KC_ASYNC_VISIT_FULL:
      {
        kc_async_visitor_req_t *req = static_cast<kc_async_visitor_req_t*>(notifier->data);
        assert(req != NULL);
        TRACE("req(%p): key = %s, key_size = %d, value = %s, value_size = %d\n", 
            req, req->key, req->key_size, req->value, req->value_size);

        Local<Value> ret;
        Local<String> method_name = String::NewSymbol("visit_full");
        Local<Function> cb;
        if (!req->cb->IsFunction()) {
          if (!req->cb->ToObject()->Has(method_name) 
              && !req->cb->ToObject()->Get(method_name)->IsFunction()) {
            req->rv = (char *)PolyDB::Visitor::NOP;
          }
          cb = Local<Function>::New(Handle<Function>::Cast(req->cb->ToObject()->Get(method_name)));
        } else {
          cb = Local<Function>::New(Handle<Function>::Cast(req->cb));
        }

        if (!cb.IsEmpty()) {
          Local<Value> argv[2] = { 
            String::New(req->key, req->key_size),
            String::New(req->value, req->value_size),
          };
          TryCatch try_catch;
          ret = cb->Call(Context::GetCurrent()->Global(), 2, argv);
          if (try_catch.HasCaught()) {
            FatalException(try_catch);
          }

          if (req->writable) {
            if (!ret.IsEmpty()) {
              if (ret->IsNumber()) {
                int64_t num_ret = ret->IntegerValue();
                if (num_ret == 1) {
                  const char *rv = PolyDB::Visitor::REMOVE;
                  req->rv = (char *)rv;
                }
              } else if (ret->IsString()) {
                String::Utf8Value str_ret(ret->ToString());
                if (*str_ret) {
                  req->rv = kc::strdup(*str_ret);
                  req->sp = strlen(req->rv);
                }
              }
            }
          } else {
            req->rv = NULL;
          }
        }
        break;
      }
    case KC_ASYNC_VISIT_EMPTY:
      {
        kc_async_visitor_req_t *req = static_cast<kc_async_visitor_req_t*>(notifier->data);
        assert(req != NULL);
        TRACE("req(%p): key = %s, key_size = %d\n", req, req->key, req->key_size);

        Local<Value> ret;
        Local<String> method_name = String::NewSymbol("visit_empty");
        Local<Function> cb;
        if (!req->cb->IsFunction()) {
          if (!req->cb->ToObject()->Has(method_name) 
              && !req->cb->ToObject()->Get(method_name)->IsFunction()) {
            req->rv = (char *)PolyDB::Visitor::NOP;
          }
          cb = Local<Function>::New(Handle<Function>::Cast(req->cb->ToObject()->Get(method_name)));
        } else {
          cb = Local<Function>::New(Handle<Function>::Cast(req->cb));
        }

        if (!cb.IsEmpty()) {
          Local<Value> argv[1] = { 
            String::New(req->key, req->key_size),
          };
          TryCatch try_catch;
          ret = cb->Call(Context::GetCurrent()->Global(), 1, argv);
          if (try_catch.HasCaught()) {
            FatalException(try_catch);
          }

          if (req->writable) {
            if (!ret.IsEmpty()) {
              if (ret->IsNumber()) {
                int64_t num_ret = ret->IntegerValue();
                if (num_ret == 1) {
                  const char *rv = PolyDB::Visitor::REMOVE;
                  req->rv = (char *)rv;
                }
              } else if (ret->IsString()) {
                String::Utf8Value str_ret(ret->ToString());
                if (*str_ret) {
                  req->rv = kc::strdup(*str_ret);
                  req->sp = strlen(req->rv);
                }
              }
            }
          } else {
            req->rv = NULL;
          }
        }
        break;
      }
    case KC_ASYNC_FILE_PROCESS:
      {
        kc_async_file_proc_req_t *req = static_cast<kc_async_file_proc_req_t*>(notifier->data);
        assert(req != NULL);
        TRACE("req(%p): path = %s, count = %d, size = %d\n", req, req->path, req->count, req->size);

        Local<Value> ret;
        Local<Function> cb = Local<Function>::New(Handle<Function>::Cast(req->cb));
        Local<Value> argv[3] = { 
          String::New(req->path, strlen(req->path)),
          Integer::New(req->count),
          Integer::New(req->size),
        };
        TryCatch try_catch;
        ret = cb->Call(Context::GetCurrent()->Global(), 3, argv);
        if (try_catch.HasCaught()) {
          FatalException(try_catch);
        }

        if (!ret.IsEmpty() && ret->IsBoolean()) {
          req->rv = ret->BooleanValue();
        }
        break;
      }
    default:
      assert(0);
      break;
  }

  async_req_done_notifier.data = notifier->data;
  uv_idle_start(&async_req_done_notifier, kc_async_req_done_notifier_cb);
}

void kc_async_init(uv_loop_t *loop) {
  TRACE("arguments: loop = %p\n", loop);
  if (async_init) {
    return;
  }
  async_init = true;
  
  uv_idle_init(loop, &async_req_done_notifier);
  uv_async_init(loop, &async_req_notifier, kc_async_req_notifier_cb);
  uv_unref((uv_handle_t *)&async_req_notifier);
}

