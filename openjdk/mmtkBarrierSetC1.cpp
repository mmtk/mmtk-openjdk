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

void MMTkBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  return MMTkBarrierSet::_c1->store_at_resolved(access, value);
}

void MMTkBarrierSetC1::generate_c1_runtime_stubs(BufferBlob* buffer_blob) {
  return MMTkBarrierSet::_c1->generate_c1_runtime_stubs(buffer_blob);
}

LIR_Opr MMTkBarrierSetC1::atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
  return MMTkBarrierSet::_c1->atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
}

LIR_Opr MMTkBarrierSetC1::atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value) {
  return MMTkBarrierSet::_c1->atomic_xchg_at_resolved(access, value);
}

// This overrides the default to resolve the address into a register,
// assuming it will be used by a write barrier anyway.
LIR_Opr MMTkBarrierSetC1::resolve_address(LIRAccess& access, bool resolve_in_register) {
  return MMTkBarrierSet::_c1->resolve_address(access, resolve_in_register);
}
