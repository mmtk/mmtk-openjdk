#ifdef COMPILER1
#include "precompiled.hpp"
#include "c1/c1_CodeStubs.hpp"
#include "c1/c1_LIRGenerator.hpp"
#include "gc/shared/c1/barrierSetC1.hpp"
#include "mmtkBarrierSet.hpp"
#include "utilities/macros.hpp"
#include CPU_HEADER(mmtkBarrierSetAssembler)
#include "mmtkBarrierSetC1.hpp"

void MMTkBarrierSetC1::generate_c1_runtime_stubs(BufferBlob* buffer_blob) {
  using GenStubFunc = void(*)(StubAssembler*);
  class RuntimeCodeBlobCodeGenClosure : public StubAssemblerCodeGenClosure {
    GenStubFunc gen_stub;
  public:
    RuntimeCodeBlobCodeGenClosure(GenStubFunc gen_stub): gen_stub(gen_stub) {}

    virtual OopMapSet* generate_code(StubAssembler* sasm) override {
      gen_stub(sasm);
      return nullptr;
    }
  };

  auto do_code_blob = [buffer_blob](const char* name, GenStubFunc gen_stub) {
    RuntimeCodeBlobCodeGenClosure closure(gen_stub);
    return Runtime1::generate_blob(buffer_blob, -1, name, false, &closure);
  };

  _load_reference_c1_runtime_code_blob              = do_code_blob("c1_load_reference_stub",              &MMTkBarrierSetAssembler::generate_c1_load_reference_runtime_stub);
  _object_reference_write_pre_c1_runtime_code_blob  = do_code_blob("c1_object_reference_write_pre_stub",  &MMTkBarrierSetAssembler::generate_c1_object_reference_write_pre_runtime_stub);
  _object_reference_write_pre_c1_runtime_code_blob_with_patch_fix  = do_code_blob("c1_object_reference_write_pre_stub_with_patch_fix",  &MMTkBarrierSetAssembler::generate_c1_object_reference_write_pre_runtime_stub_with_patch_fix);
  _object_reference_write_post_c1_runtime_code_blob = do_code_blob("c1_object_reference_write_post_stub", &MMTkBarrierSetAssembler::generate_c1_object_reference_write_post_runtime_stub);
  _object_reference_write_slow_c1_runtime_code_blob = do_code_blob("c1_object_reference_write_slow_stub", &MMTkBarrierSetAssembler::generate_c1_object_reference_write_slow_runtime_stub);
}

void MMTkC1ReferenceLoadBarrierStub::emit_code(LIR_Assembler* ce) {
  MMTkBarrierSetAssembler* bs = (MMTkBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
  bs->generate_c1_ref_load_barrier_stub_call(ce, this);
}

// Shared by the barriers that keep the referent of a weak reference alive when it is loaded while
// marking (SATBBarrier and HybridBarrier).  The barrier is gated by the byte at `flag_addr`.

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void MMTkBarrierSetC1::weak_reference_load_at_resolved(LIRAccess& access, LIR_Opr result, uintptr_t flag_addr) {
  DecoratorSet decorators = access.decorators();
  bool is_weak = (decorators & ON_WEAK_OOP_REF) != 0;
  bool is_phantom = (decorators & ON_PHANTOM_OOP_REF) != 0;
  bool is_anonymous = (decorators & ON_UNKNOWN_OOP_REF) != 0;
  LIRGenerator *gen = access.gen();

  BarrierSetC1::load_at_resolved(access, result);

  if (mmtk_enable_reference_load_barrier) {
    if (access.is_oop() && (is_weak || is_phantom || is_anonymous)) {
      // Register the value in the referent field with the pre-barrier
      LabelObj *Lcont_anonymous;
      if (is_anonymous) {
        Lcont_anonymous = new LabelObj();
        generate_referent_check(access, Lcont_anonymous);
      }
      assert(result->is_register(), "must be");
      assert(result->type() == T_OBJECT, "must be an object");
      auto slow = new MMTkC1ReferenceLoadBarrierStub(result);
      // Call slow-path only when the barrier is active
      LIR_Opr cm_flag_addr_opr = gen->new_pointer_register();
      __ move(LIR_OprFact::longConst(flag_addr), cm_flag_addr_opr);
      LIR_Address* cm_flag_addr = new LIR_Address(cm_flag_addr_opr, T_BYTE);
      LIR_Opr cm_flag = gen->new_register(T_INT);
      __ move(cm_flag_addr, cm_flag);
      // No slow-call if the barrier is not active
      __ cmp(lir_cond_notEqual, cm_flag, LIR_OprFact::intConst(0));
      __ branch(lir_cond_notEqual, slow);
      __ branch_destination(slow->continuation());
      if (is_anonymous) {
        __ branch_destination(Lcont_anonymous->label());
      }
    }
  }
}

#undef __
#endif
