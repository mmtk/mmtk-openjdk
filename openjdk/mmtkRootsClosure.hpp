#ifndef MMTK_OPENJDK_MMTK_ROOTS_CLOSURE_HPP
#define MMTK_OPENJDK_MMTK_ROOTS_CLOSURE_HPP

#include "memory/iterator.hpp"
#include "mmtk.h"
#include "oops/oop.hpp"
#include "oops/oop.inline.hpp"
#include "utilities/globalDefinitions.hpp"

#define ROOTS_BUFFER_SIZE 4096

class MMTkRootsClosure : public OopClosure {
  void* _trace;
  void* _buffer[ROOTS_BUFFER_SIZE];
  size_t _cursor;

  template <class T>
  void do_oop_work(T* p) {
    // T heap_oop = RawAccess<>::oop_load(p);
    // if (!CompressedOops::is_null(heap_oop)) {
    //   oop obj = CompressedOops::decode_not_null(heap_oop);
    //   oop fwd = (oop) trace_root_object(_trace, obj);
    //   RawAccess<>::oop_store(p, fwd);
    // }
    _buffer[_cursor++] = (void*) p;
    if (_cursor >= ROOTS_BUFFER_SIZE) {
      flush();
    }
  }

  NOINLINE void flush() {
    // bulk_report_delayed_root_edge(_trace, _buffer, _cursor);
    _cursor = 0;
  }

public:
  MMTkRootsClosure(void* trace): _trace(trace), _cursor(0) {}

  ~MMTkRootsClosure() {
    if (_cursor > 0) flush();
  }

  virtual void do_oop(oop* p)       { do_oop_work(p); }
  virtual void do_oop(narrowOop* p) {
    // printf("narrowoop root %p -> %d %p %p\n", (void*) p, *p, *((void**) p), (void*) oopDesc::load_decode_heap_oop(p));
    do_oop_work(p);
  }
};

class MMTkRootsClosure2 : public OopClosure {
  ProcessEdgesFn _process_edges;
  void** _buffer;
  size_t _cap;
  size_t _cursor;

  template <class T>
  void do_oop_work(T* p) {
    // T heap_oop = RawAccess<>::oop_load(p);
    // if (!CompressedOops::is_null(heap_oop)) {
    //   oop obj = CompressedOops::decode_not_null(heap_oop);
    //   oop fwd = (oop) trace_root_object(_trace, obj);
    //   RawAccess<>::oop_store(p, fwd);
    // }
    _buffer[_cursor++] = (void*) p;
    if (_cursor >= _cap) {
      flush();
    }
  }

  void flush() {
    if (_cursor > 0) {
      NewBuffer buf = _process_edges(_buffer, _cursor, _cap);
      _buffer = buf.buf;
      _cap = buf.cap;
      _cursor = 0;
    }
  }

public:
  MMTkRootsClosure2(ProcessEdgesFn process_edges): _process_edges(process_edges), _cursor(0) {
    NewBuffer buf = process_edges(NULL, 0, 0);
    _buffer = buf.buf;
    _cap = buf.cap;
  }

  ~MMTkRootsClosure2() {
    if (_cursor > 0) flush();
    if (_buffer != NULL) {
      release_buffer(_buffer, _cursor, _cap);
    }
  }

  virtual void do_oop(oop* p)       { do_oop_work(p); }
  virtual void do_oop(narrowOop* p) { do_oop_work(p); }
};

class MMTkScanObjectClosure : public BasicOopIterateClosure {
  void* _trace;
  CLDToOopClosure follow_cld_closure;

  template <class T>
  void do_oop_work(T* p) {
    // oop ref = (void*) oopDesc::decode_heap_oop(oopDesc::load_heap_oop(p));
    // process_edge(_trace, (void*) p);
  }

public:
  MMTkScanObjectClosure(void* trace): _trace(trace), follow_cld_closure(this, false) {}

  virtual void do_oop(oop* p)       { do_oop_work(p); }
  virtual void do_oop(narrowOop* p) {
    // printf("narrowoop edge %p -> %d %p %p\n", (void*) p, *p, *((void**) p), (void*) oopDesc::load_decode_heap_oop(p));
    do_oop_work(p);
  }

  virtual bool do_metadata() {
    return true;
  }

  virtual void do_klass(Klass* k) {
  //  follow_cld_closure.do_cld(k->class_loader_data());
    // oop op = k->klass_holder();
    // oop new_op = (oop) trace_root_object(_trace, op);
    // guarantee(new_op == op, "trace_root_object returned a different value %p -> %p", op, new_op);
  }

  virtual void do_cld(ClassLoaderData* cld) {
    follow_cld_closure.do_cld(cld);
  }

  virtual ReferenceIterationMode reference_iteration_mode() { return DO_FIELDS; }
  virtual bool idempotent() { return true; }
};

// class MMTkCLDClosure : public CLDClosure {
// public:
//   virtual void do_cld(ClassLoaderData* cld) {

//     printf("CLD: %p", p);
//   }
// };

#endif // MMTK_OPENJDK_MMTK_ROOTS_CLOSURE_HPP
