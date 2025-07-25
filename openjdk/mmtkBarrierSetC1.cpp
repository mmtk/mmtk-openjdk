#include "c1/c1_CodeStubs.hpp"
#include "gc/shared/c1/barrierSetC1.hpp"
#include "mmtkBarrierSetAssembler_x86.hpp"
#include "mmtkBarrierSetC1.hpp"

void MMTkBarrierSetC1::generate_c1_runtime_stubs(BufferBlob* buffer_blob) {
  class MMTkPreBarrierCodeGenClosure : public StubAssemblerCodeGenClosure {

    virtual OopMapSet* generate_code(StubAssembler* sasm) override {
      MMTkBarrierSetAssembler* bs = (MMTkBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
      bs->generate_c1_pre_write_barrier_runtime_stub(sasm);
      return NULL;
    }
  public:
    MMTkPreBarrierCodeGenClosure() {}
  };

  class MMTkPostBarrierCodeGenClosure : public StubAssemblerCodeGenClosure {
    virtual OopMapSet* generate_code(StubAssembler* sasm) override {
      MMTkBarrierSetAssembler* bs = (MMTkBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
      bs->generate_c1_post_write_barrier_runtime_stub(sasm);
      return NULL;
    }
  public:
    MMTkPostBarrierCodeGenClosure() {}
  };

  MMTkPreBarrierCodeGenClosure pre_write_code_gen_cl;
  _pre_barrier_c1_runtime_code_blob = Runtime1::generate_blob(buffer_blob, -1, "mmtk_pre_write_code_gen_cl", false, &pre_write_code_gen_cl);
  MMTkPostBarrierCodeGenClosure post_write_code_gen_cl;
  _post_barrier_c1_runtime_code_blob = Runtime1::generate_blob(buffer_blob, -1, "mmtk_post_write_code_gen_cl", false, &post_write_code_gen_cl);
  // MMTkBarrierCodeGenClosure write_code_gen_cl_patch_fix(true);
  // _write_barrier_c1_runtime_code_blob_with_patch_fix = Runtime1::generate_blob(buffer_blob, -1, "write_code_gen_cl_patch_fix", false, &write_code_gen_cl_patch_fix);

class MMTkRefLoadBarrierCodeGenClosure : public StubAssemblerCodeGenClosure {
    virtual OopMapSet* generate_code(StubAssembler* sasm) override {
      MMTkBarrierSetAssembler* bs = (MMTkBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
      bs->generate_c1_ref_load_barrier_runtime_stub(sasm);
      return NULL;
    }
  };
  MMTkRefLoadBarrierCodeGenClosure load_code_gen_cl;
  _ref_load_barrier_c1_runtime_code_blob = Runtime1::generate_blob(buffer_blob, -1, "load_code_gen_cl", false, &load_code_gen_cl);

}

void MMTkC1PostBarrierStub::emit_code(LIR_Assembler* ce) {
  MMTkBarrierSetAssembler* bs = (MMTkBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
  bs->generate_c1_post_write_barrier_stub(ce, this);
}

void MMTkC1PreBarrierStub::emit_code(LIR_Assembler* ce) {
  MMTkBarrierSetAssembler* bs = (MMTkBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
  bs->generate_c1_pre_write_barrier_stub(ce, this);
}


void MMTkC1ReferenceLoadBarrierStub::emit_code(LIR_Assembler* ce) {
  MMTkBarrierSetAssembler* bs = (MMTkBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
  bs->generate_c1_ref_load_barrier_stub_call(ce, this);
}