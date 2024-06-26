/*
 * Copyright (c) 2017, Red Hat, Inc. and/or its affiliates.
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
 * Please contact Sun 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "mmtk.h"
#include "mmtkVMCompanionThread.hpp"
#include "mmtkVMOperation.hpp"
#include "logging/log.hpp"

VM_MMTkSTWOperation::VM_MMTkSTWOperation(MMTkVMCompanionThread *companion_thread):
    _companion_thread(companion_thread) {
}

void VM_MMTkSTWOperation::doit() {
    log_trace(vmthread)("Entered VM_MMTkSTWOperation::doit().");
    _companion_thread->do_mmtk_stw_operation();
    log_trace(vmthread)("Leaving VM_MMTkSTWOperation::doit()");
}
