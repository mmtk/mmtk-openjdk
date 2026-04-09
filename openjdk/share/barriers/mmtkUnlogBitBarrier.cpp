#include "precompiled.hpp"
#include "mmtkUnlogBitBarrier.hpp"
#include "mmtkMutator.hpp"

#include "runtime/interfaceSupport.inline.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"

//////////////////// C1 ////////////////////

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void MMTkUnlogBitBarrierSetC1::emit_check_unlog_bit_fast_path(LIRGenerator* gen, LIR_Opr src, CodeStub* slow) {
  // We need to do bit operations on the address of `src`. In order to move `src` (`T_OBJECT` or
  // `T_ARRAY`) to a pointer regiseter (`T_LONG` on 64 bit), the source operand must be in
  // register, in which case `LIR_Assembler::reg2reg` works as expected.  Otherwise `stack2ref`
  // will complain that the source (`T_OBJECT` or `T_ARRAY` is single-cpu while the destination
  // `T_LONG` is double-cpu).
  //
  // However, checking `src.is_register()` won't work because the same LIR code may be compiled
  // again. Even it is register the first time, `src.is_stack()` may instead be true at the second
  // time.
  //
  // So we introduce an intermediate step.  We move `src` into `addr` which is a `T_OBJECT`
  // register first to make sure it is in register.  Then we move `addr` to newly created pointer
  // registers.
  LIR_Opr addr = gen->new_register(T_OBJECT);
  __ move(src, addr);

  // uint8_t* meta_addr = (uint8_t*) (UNLOG_BIT_BASE_ADDRESS + (addr >> 6));
  LIR_Opr offset = gen->new_pointer_register();
  __ move(addr, offset);
  __ unsigned_shift_right(offset, 6, offset);
  LIR_Opr base = gen->new_pointer_register();
  __ move(LIR_OprFact::longConst(UNLOG_BIT_BASE_ADDRESS), base);
  LIR_Address* meta_addr = new LIR_Address(base, offset, T_BYTE);

  // uint8_t byte_val = *meta_addr;
  LIR_Opr byte_val = gen->new_register(T_INT);
  __ move(meta_addr, byte_val);

  // uint32_t shift = ((uint32_t)addr >> 3) & 0b111;
  LIR_Opr shift = gen->new_register(T_INT);
  __ move(addr, shift);
  __ unsigned_shift_right(shift, 3, shift);
  __ logical_and(shift, LIR_OprFact::intConst(0b111), shift);

  // if (((byte_val >> shift) & 1) == 1) slow;
  LIR_Opr result = byte_val;
  __ unsigned_shift_right(result, shift, result, LIR_OprFact::illegalOpr);
  __ logical_and(result, LIR_OprFact::intConst(1), result);
  __ cmp(lir_cond_equal, result, LIR_OprFact::intConst(1));
  __ branch(lir_cond_equal, slow);
}

void MMTkUnlogBitBarrierSetC1::object_reference_write_pre_or_post(LIRAccess& access, LIR_Opr src, bool pre) {
  LIRGenerator* gen = access.gen();
  DecoratorSet decorators = access.decorators();
  if ((decorators & IN_HEAP) == 0) return;

  CodeStub* slow = new MMTkC1UnlogBitBarrierSlowPathStub(src, mmtk_enable_barrier_fastpath, pre);

  if (mmtk_enable_barrier_fastpath) {
    emit_check_unlog_bit_fast_path(gen, src, slow);
  } else {
    __ jump(slow);
  }

  __ branch_destination(slow->continuation());
}

#undef __

//////////////////// C2 ////////////////////

#define __ ideal.

Node* MMTkUnlogBitBarrierSetC2::emit_check_unlog_bit_fast_path(MMTkIdealKit& ideal, Node* obj) {
  Node* addr = __ CastPX(__ ctrl(), obj);
  Node* no_base = __ top();
  Node* meta_addr = __ AddP(no_base, __ ConP(UNLOG_BIT_BASE_ADDRESS), __ URShiftX(addr, __ ConI(6)));
  Node* byte = __ load(__ ctrl(), meta_addr, TypeInt::INT, T_BYTE, Compile::AliasIdxRaw);
  Node* shift = __ URShiftX(addr, __ ConI(3));
  shift = __ AndI(__ ConvL2I(shift), __ ConI(7));
  Node* result = __ AndI(__ URShiftI(byte, shift), __ ConI(1));

  return result;
}

void MMTkUnlogBitBarrierSetC2::object_reference_write_pre_or_post(MMTkIdealKit& ideal, Node* src, bool pre) {
  Node* no_base = __ top();
  Node* tls = __ thread();
  Node* mutator = __ AddP(no_base, tls, __ ConX(in_bytes(JavaThread::third_party_heap_mutator_offset())));
  if (mmtk_enable_barrier_fastpath) {
    Node* result = emit_check_unlog_bit_fast_path(ideal, src);

    Node* zero = __ ConI(0);
    float unlikely = PROB_UNLIKELY(0.999);
    __ if_then(result, BoolTest::ne, zero, unlikely); {
      const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeRawPtr::NOTNULL);
      Node* null_node = __ NullP();
      Node* x = __ make_leaf_call(tf, FN_ADDR(mmtk_object_reference_write_slow), "mmtk_barrier_call", src, null_node, null_node, mutator);
    } __ end_if();
  } else {
    const TypeFunc* tf = __ func_type(TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeOopPtr::BOTTOM, TypeRawPtr::NOTNULL);
    Node* null_node = __ NullP();
    Node* x = __ make_leaf_call(tf, FN_ADDR(mmtk_object_reference_write_slow), "mmtk_barrier_call", src, null_node, null_node, mutator);
  }
}

#undef __
