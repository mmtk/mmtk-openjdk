#ifndef MMTK_MMTKBARRIERSETC1_HPP
#define MMTK_MMTKBARRIERSETC1_HPP

#include "c1/c1_CodeStubs.hpp"
#include "gc/shared/c1/barrierSetC1.hpp"


class MMTkWriteBarrierStub: public CodeStub {
  friend class MMTkBarrierSetC1;
private:
  LIR_Opr _src;
  LIR_Opr _slot;
  LIR_Opr _new_val;

public:
  // addr (the address of the object head) and new_val must be registers.
  MMTkWriteBarrierStub(LIR_Opr src, LIR_Opr slot, LIR_Opr new_val): _src(src), _slot(slot), _new_val(new_val) {}

  LIR_Opr src() const { return _src; }
  LIR_Opr slot() const { return _slot; }
  LIR_Opr new_val() const { return _new_val; }

  virtual void emit_code(LIR_Assembler* e);
  virtual void visit(LIR_OpVisitState* visitor) {
    // don't pass in the code emit info since it's processed in the fast path
    visitor->do_slow_case();
    visitor->do_input(_src);
    visitor->do_input(_slot);
    visitor->do_input(_new_val);
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("MMTkWriteBarrierStub"); }
#endif // PRODUCT
};

class CodeBlob;

class MMTkBarrierSetC1 : public BarrierSetC1 {
protected:
  CodeBlob* _write_barrier_c1_runtime_code_blob;

  virtual void write_barrier(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val);

public:
  MMTkBarrierSetC1(): _write_barrier_c1_runtime_code_blob(NULL) {}

  virtual void store_at_resolved(LIRAccess& access, LIR_Opr value);

  CodeBlob* write_barrier_c1_runtime_code_blob() { return _write_barrier_c1_runtime_code_blob; }

  virtual void generate_c1_runtime_stubs(BufferBlob* buffer_blob);
};

#endif // MMTK_MMTKBARRIERSETC1_HPP
