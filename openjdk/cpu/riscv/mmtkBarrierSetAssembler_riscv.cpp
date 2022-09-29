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
#include "asm/macroAssembler.inline.hpp"
#include "interpreter/interp_masm.hpp"
#include "mmtkBarrierSet.hpp"
#include "mmtkBarrierSetAssembler_riscv.hpp"
#include "mmtkBarrierSetC1.hpp"
#include "mmtkMutator.hpp"
#include "runtime/sharedRuntime.hpp"
#include "utilities/macros.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"

#define __ masm->

void MMTkBarrierSetAssembler::eden_allocate(MacroAssembler* masm, Register thread, Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register t1, Label& slow_case) {
  // assert(false, "Not implemented");
  __ j(slow_case); // just jal with x0/zr
}

#undef __

#define __ sasm->

void MMTkBarrierSetAssembler::generate_c1_write_barrier_runtime_stub(StubAssembler* sasm) const {
  // assert(false, "Not implemented");
}

#undef __

#define __ ce->masm()->

void MMTkBarrierSetAssembler::generate_c1_write_barrier_stub_call(LIR_Assembler* ce, MMTkC1BarrierStub* stub) {
//  assert(false, "Not implemented");
}

#undef __
