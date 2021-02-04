#ifndef MMTK_MMTKBARRIERSETC1_HPP
#define MMTK_MMTKBARRIERSETC1_HPP

#include "c1/c1_CodeStubs.hpp"
#include "gc/shared/c1/barrierSetC1.hpp"

class MMTkBarrierC1 : public BarrierSetC1 {
public:
  virtual void store_at_resolved(LIRAccess& access, LIR_Opr value) {
    BarrierSetC1::store_at_resolved(access, value);
  }
  virtual LIR_Opr atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
    return BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
  }
  virtual LIR_Opr atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value) {
    return BarrierSetC1::atomic_xchg_at_resolved(access, value);
  }
  virtual void generate_c1_runtime_stubs(BufferBlob* buffer_blob) {}
  virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register) {
    return BarrierSetC1::resolve_address(access, resolve_in_register);
  }
};

class MMTkBarrierSetC1 : public BarrierSetC1 {
public:
  MMTkBarrierSetC1() {}

  virtual void store_at_resolved(LIRAccess& access, LIR_Opr value);

  virtual void generate_c1_runtime_stubs(BufferBlob* buffer_blob);

  virtual LIR_Opr resolve_address(LIRAccess& access, bool resolve_in_register);

  virtual LIR_Opr atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value);

  virtual LIR_Opr atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value);
};

#endif // MMTK_MMTKBARRIERSETC1_HPP
