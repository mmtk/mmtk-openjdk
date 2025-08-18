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

#ifndef MMTK_OPENJDK_MMTK_VM_OPERATION_HPP
#define MMTK_OPENJDK_MMTK_VM_OPERATION_HPP

#include "runtime/vmOperations.hpp"
#include "runtime/vmThread.hpp"
#include "thirdPartyHeapVMOperation.hpp"

class VM_MMTkOperation : public VM_ThirdPartyOperation {
};

class MMTkVMCompanionThread;
class VM_MMTkSTWOperation : public VM_MMTkOperation {
private:
  MMTkVMCompanionThread* _companion_thread;

public:
  VM_MMTkSTWOperation(MMTkVMCompanionThread *companion_thread);
  virtual void doit() override;
};

#endif // MMTK_OPENJDK_MMTK_VM_OPERATION_HPP
