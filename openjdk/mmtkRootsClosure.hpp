#ifndef MMTK_OPENJDK_MMTK_ROOTS_CLOSURE_HPP
#define MMTK_OPENJDK_MMTK_ROOTS_CLOSURE_HPP

#include "memory/iterator.hpp"
#include "mmtk.h"
#include "oops/oop.hpp"
#include "oops/oop.inline.hpp"
#include "utilities/globalDefinitions.hpp"

class MMTkRootsClosure : public OopClosure {
  EdgesClosure _edges_closure;
  void** _buffer;
  size_t _cap;
  size_t _cursor;

  template <class T>
  void do_oop_work(T* p, bool narrow) {
    T heap_oop = RawAccess<>::oop_load(p);
    if (!CompressedOops::is_null(heap_oop)) {
      if (UseCompressedOops && !narrow) {
        guarantee((uintptr_t(p) & (1ull << 63)) == 0, "test");
        p = (T*) (uintptr_t(p) | (1ull << 63));
      }
      _buffer[_cursor++] = (void*) p;
      if (_cursor >= _cap) {
        flush();
      }
    }
  }

  void flush() {
    if (_cursor > 0) {
      NewBuffer buf = _edges_closure.invoke(_buffer, _cursor, _cap);
      _buffer = buf.buf;
      _cap = buf.cap;
      _cursor = 0;
    }
  }

public:
  MMTkRootsClosure(EdgesClosure edges_closure): _edges_closure(edges_closure), _cursor(0) {
    NewBuffer buf = edges_closure.invoke(NULL, 0, 0);
    _buffer = buf.buf;
    _cap = buf.cap;
  }

  ~MMTkRootsClosure() {
    if (_cursor > 0) flush();
    if (_buffer != NULL) {
      release_buffer(_buffer, _cursor, _cap);
    }
  }

  virtual void do_oop(oop* p)       { do_oop_work(p, false); }
  virtual void do_oop(narrowOop* p) { do_oop_work(p, true); }
};

class MMTkScanObjectClosure : public BasicOopIterateClosure {
  void* _trace;
  CLDToOopClosure follow_cld_closure;

  template <class T>
  void do_oop_work(T* p, bool narrow) {
    if (UseCompressedOops && !narrow) {
      guarantee((uintptr_t(p) & (1ull << 63)) == 0, "test");
      p = (T*) (uintptr_t(p) | (1ull << 63));
    }
  }

public:
  MMTkScanObjectClosure(void* trace): _trace(trace), follow_cld_closure(this, false) {}

  virtual void do_oop(oop* p)       { do_oop_work(p, false); }
  virtual void do_oop(narrowOop* p) { do_oop_work(p, true); }

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
