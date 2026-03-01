#ifndef MMTK_OPENJDK_MMTK_ROOTS_CLOSURE_HPP
#define MMTK_OPENJDK_MMTK_ROOTS_CLOSURE_HPP

#include "memory/iterator.hpp"
#include "mmtk.h"
#include "oops/oop.hpp"
#include "oops/oop.inline.hpp"
#include "utilities/globalDefinitions.hpp"
#include "classfile/classLoaderData.inline.hpp"

class MMTkRootsClosure : public OopClosure {
  SlotsClosure _slots_closure;
  void** _buffer;
  size_t _cap;
  size_t _cursor;

  template <class T>
  inline void do_oop_work(T* p, bool narrow) {
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
      NewBuffer buf = _slots_closure.invoke(_buffer, _cursor, _cap);
      _buffer = buf.buf;
      _cap = buf.cap;
      _cursor = 0;
    }
  }

public:
  MMTkRootsClosure(SlotsClosure slots_closure): _slots_closure(slots_closure), _cursor(0) {
    NewBuffer buf = slots_closure.invoke(NULL, 0, 0);
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


template <bool MODIFIED_ONLY, bool WEAK, bool CLAIM = false>
class MMTkScanCLDClosure: public CLDClosure {
 private:
  OopClosure* _oop_closure;
 protected:
 public:
  MMTkScanCLDClosure(OopClosure* c) : _oop_closure(c) { }
  void do_cld(ClassLoaderData* cld) {
    if (MODIFIED_ONLY) {
      if (cld->has_modified_oops()) cld->oops_do(_oop_closure, CLAIM, /*clear_modified_oops*/true);
    } else {
      if (cld->has_modified_oops() || !WEAK)
        cld->oops_do(_oop_closure, CLAIM, /*clear_modified_oops*/true);
    }
  }
};

class MMTkScanObjectClosure : public BasicOopIterateClosure {
  void (*_trace)(void*);
  bool _follow_clds;
  bool _claim_clds;

  template <class T>
  void do_oop_work(T* p, bool narrow) {
    if (UseCompressedOops && !narrow) {
      guarantee((uintptr_t(p) & (1ull << 63)) == 0, "test");
      p = (T*) (uintptr_t(p) | (1ull << 63));
    }
    _trace((void*) p);
  }

public:
  MMTkScanObjectClosure(void* trace, bool follow_clds, bool claim_clds): _trace((void (*)(void*)) trace), _follow_clds(follow_clds), _claim_clds(claim_clds) {}

  virtual void do_oop(oop* p)       {  do_oop_work(p, false); }
  virtual void do_oop(narrowOop* p) { do_oop_work(p, true); }

  virtual bool do_metadata() {
    return _follow_clds;
  }

  virtual void do_klass(Klass* k) {
    if (!_follow_clds) return;
    do_cld(k->class_loader_data());
  }

  virtual void do_cld(ClassLoaderData* cld) {
    if (!_follow_clds) return;
    cld->oops_do(this, _claim_clds);
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

class CodeBlobFixRelocationClosure: public CodeBlobClosure {
 public:
  inline virtual void do_code_blob(CodeBlob* cb) {
    nmethod* nm = cb->as_nmethod_or_null();
    if (nm != NULL) {
      nm->fix_oop_relocations();
    }
  }
};

#endif // MMTK_OPENJDK_MMTK_ROOTS_CLOSURE_HPP
