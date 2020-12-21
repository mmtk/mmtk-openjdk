/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "c1/c1_LIRGenerator.hpp"
#include "c1/c1_CodeStubs.hpp"
#include "mmtkBarrierSetC1.hpp"
#include "mmtkBarrierSet.hpp"
#include "mmtkBarrierSetAssembler_x86.hpp"
#include "utilities/macros.hpp"

#ifdef ASSERT
#define __ gen->lir(__FILE__, __LINE__)->
#else
#define __ gen->lir()->
#endif

void MMTkWriteBarrierStub::emit_code(LIR_Assembler* ce) {
  MMTkBarrierSetAssembler* bs = (MMTkBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
  bs->gen_write_barrier_stub(ce, this);
}

void MMTkBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  BarrierSetC1::store_at_resolved(access, value);

  if (MMTK_ENABLE_WRITE_BARRIER && access.is_oop()) {
    write_barrier(access, access.base().opr(), access.resolved_addr(), value);
  }
}


void MMTkBarrierSetC1::write_barrier(LIRAccess& access, LIR_Opr src, LIR_Opr slot, LIR_Opr new_val) {
  LIRGenerator* gen = access.gen();
  DecoratorSet decorators = access.decorators();

  if ((decorators & IN_HEAP) == 0) return;

  if (!src->is_register()) {
    LIR_Opr reg = gen->new_pointer_register();
    if (src->is_constant()) {
      __ move(new_val, reg);
    } else {
      __ leal(new_val, reg);
    }
    src = reg;
  }
  assert(src->is_register(), "must be a register at this point");

  if (!slot->is_register()) {
    LIR_Opr reg = gen->new_pointer_register();
    if (slot->is_constant()) {
      __ move(slot, reg);
    } else {
      __ leal(slot, reg);
    }
    slot = reg;
  }
  assert(slot->is_register(), "must be a register at this point");

  if (!new_val->is_register()) {
    LIR_Opr new_val_reg = gen->new_register(T_OBJECT);
    if (new_val->is_constant()) {
      __ move(new_val, new_val_reg);
    } else {
      __ leal(new_val, new_val_reg);
    }
    new_val = new_val_reg;
  }
  assert(new_val->is_register(), "must be a register at this point");

  CodeStub* slow = new MMTkWriteBarrierStub(src, slot, new_val);

#if MMTK_ENABLE_WRITE_BARRIER_FASTPATH
  LIR_Opr byte_offset = gen->new_pointer_register();
  __ move(src, byte_offset);
  __ shift_right(byte_offset, 6, byte_offset);
  LIR_Opr byte = gen->new_register(T_INT);
  LIR_Opr heap_end = gen->new_pointer_register();
  __ move(LIR_OprFact::intptrConst(MMTK_HEAP_END), heap_end);
  LIR_Address* addr = new LIR_Address(byte_offset, heap_end, T_BYTE);
  __ move(addr, byte);
  LIR_Opr bit_offset = gen->new_register(T_LONG);
  __ move(src, bit_offset);
  __ shift_right(bit_offset, 3, bit_offset);
  __ logical_and(bit_offset, LIR_OprFact::longConst(7), bit_offset);
  LIR_Opr bit_offset_int = gen->new_register(T_INT);
  __ convert(Bytecodes::_l2i, bit_offset, bit_offset_int);
  LIR_Opr bit_offset_reg = LIRGenerator::shiftCountOpr();
  __ move(bit_offset_int, bit_offset_reg);
  LIR_Opr result = byte;
  __ shift_right(result, bit_offset_reg, result, LIR_OprFact::illegalOpr);
  __ logical_and(result, LIR_OprFact::intConst(1), result);
  __ cmp(lir_cond_equal, result, LIR_OprFact::intConst(0));

  __ branch(lir_cond_equal, LP64_ONLY(T_LONG) NOT_LP64(T_INT), slow);
#else
  __ jump(slow);
#endif

  __ branch_destination(slow->continuation());
}


class C1MMTkWriteBarrierCodeGenClosure : public StubAssemblerCodeGenClosure {
  virtual OopMapSet* generate_code(StubAssembler* sasm) {
    MMTkBarrierSetAssembler* bs = (MMTkBarrierSetAssembler*) BarrierSet::barrier_set()->barrier_set_assembler();
    bs->generate_c1_write_barrier_runtime_stub(sasm);
    return NULL;
  }
};

void MMTkBarrierSetC1::generate_c1_runtime_stubs(BufferBlob* buffer_blob) {
  C1MMTkWriteBarrierCodeGenClosure write_code_gen_cl;
  _write_barrier_c1_runtime_code_blob = Runtime1::generate_blob(buffer_blob, -1, "write_code_gen_cl", false, &write_code_gen_cl);
}

LIR_Opr MMTkBarrierSetC1::atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
  LIR_Opr result = BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
  if (MMTK_ENABLE_WRITE_BARRIER && access.is_oop()) {
    write_barrier(access, access.base().opr(), access.resolved_addr(), new_value.result());
  }
  return result;
}

LIR_Opr MMTkBarrierSetC1::atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value) {
  LIR_Opr result = BarrierSetC1::atomic_xchg_at_resolved(access, value);
  if (MMTK_ENABLE_WRITE_BARRIER && access.is_oop()) {
    write_barrier(access, access.base().opr(), access.resolved_addr(), value.result());
  }
  return result;
}

// This overrides the default to resolve the address into a register,
// assuming it will be used by a write barrier anyway.
LIR_Opr MMTkBarrierSetC1::resolve_address(LIRAccess& access, bool resolve_in_register) {
  if (!MMTK_ENABLE_WRITE_BARRIER) return BarrierSetC1::resolve_address(access, resolve_in_register);
  DecoratorSet decorators = access.decorators();
  bool needs_patching = (decorators & C1_NEEDS_PATCHING) != 0;
  bool is_write = (decorators & C1_WRITE_ACCESS) != 0;
  bool is_array = (decorators & IS_ARRAY) != 0;
  bool on_anonymous = (decorators & ON_UNKNOWN_OOP_REF) != 0;
  bool precise = is_array || on_anonymous;
  resolve_in_register |= !needs_patching && is_write && access.is_oop() && precise;
  return BarrierSetC1::resolve_address(access, resolve_in_register);
}
