#ifndef MMTK_MMTKBARRIERSETC1_HPP
#define MMTK_MMTKBARRIERSETC1_HPP

#include "c1/c1_CodeStubs.hpp"
#include "gc/shared/c1/barrierSetC1.hpp"



class CodeBlob;

class MMTkBarrierSetC1 : public BarrierSetC1 {
 protected:
  CodeBlob* _write_barrier_c1_runtime_code_blob;

  virtual void write_barrier(LIRAccess& access, LIR_OprDesc* addr, LIR_OprDesc* new_val) {}

 public:
  MMTkBarrierSetC1(): _write_barrier_c1_runtime_code_blob(NULL) {}

  // virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register);

  virtual void store_at_resolved(LIRAccess& access, LIR_Opr value);

  // CodeBlob* pre_barrier_c1_runtime_code_blob() { return _pre_barrier_c1_runtime_code_blob; }
  // CodeBlob* post_barrier_c1_runtime_code_blob() { return _post_barrier_c1_runtime_code_blob; }

  // virtual void generate_c1_runtime_stubs(BufferBlob* buffer_blob);
};

#endif // MMTK_MMTKBARRIERSETC1_HPP
