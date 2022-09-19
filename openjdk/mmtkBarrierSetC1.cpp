#include "c1/c1_CodeStubs.hpp"
#include "gc/shared/c1/barrierSetC1.hpp"
#include "mmtkBarrierSetAssembler_x86.hpp"
#include "mmtkBarrierSetC1.hpp"

void MMTkBarrierSetC1::generate_c1_runtime_stubs(BufferBlob* buffer_blob) {
  class MMTkBarrierCodeGenClosure : public StubAssemblerCodeGenClosure {
    virtual OopMapSet* generate_code(StubAssembler* sasm) override {
      MMTkBarrierSetAssembler* bs = (MMTkBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
      bs->generate_c1_write_barrier_runtime_stub(sasm);
      return NULL;
    }
  };
  MMTkBarrierCodeGenClosure write_code_gen_cl;
  _write_barrier_c1_runtime_code_blob = Runtime1::generate_blob(buffer_blob, -1, "write_code_gen_cl", false, &write_code_gen_cl);
}

void MMTkC1BarrierStub::emit_code(LIR_Assembler* ce) {
  MMTkBarrierSetAssembler* bs = (MMTkBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
  bs->generate_c1_write_barrier_stub_call(ce, this);
}
