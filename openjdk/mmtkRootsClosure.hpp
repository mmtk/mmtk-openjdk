#include "memory/iterator.hpp"
#include "oops/oop.hpp"
#include "oops/oop.inline.hpp"
#include "../mmtk/api/mmtk.h"

class MMTkRootsClosure : public OopClosure {
  void* _trace;

  template <class T>
  void do_oop_work(T* p) {
    // T heap_oop = oopDesc::load_heap_oop(p);
    // if (!oopDesc::is_null(heap_oop)) {
    //   oop obj = oopDesc::decode_heap_oop_not_null(heap_oop);
    //   report_delayed_root_edge(_trace, (void*) obj);
    // }
    report_delayed_root_edge(_trace, (void*) p);
  }

public:
  MMTkRootsClosure(void* trace): _trace(trace) {}

  virtual void do_oop(oop* p)       { do_oop_work(p); }
  virtual void do_oop(narrowOop* p) {
    printf("narrowoop root %p -> %d %p %p\n", (void*) p, *p, *((void**) p), (void*) oopDesc::load_decode_heap_oop(p));
    do_oop_work(p);
  }
};

class MMTkScanObjectClosure : public ExtendedOopClosure {
  void* _trace;
  CLDToOopClosure follow_cld_closure;

  template <class T>
  void do_oop_work(T* p) {
    // oop ref = (void*) oopDesc::decode_heap_oop(oopDesc::load_heap_oop(p));
    process_edge(_trace, (void*) p);
  }

public:
  MMTkScanObjectClosure(void* trace): _trace(trace), follow_cld_closure(this, false) {}

  virtual void do_oop(oop* p)       { do_oop_work(p); }
  virtual void do_oop(narrowOop* p) {
    printf("narrowoop edge %p -> %d %p %p\n", (void*) p, *p, *((void**) p), (void*) oopDesc::load_decode_heap_oop(p));
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