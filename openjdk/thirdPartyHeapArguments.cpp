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
 * Please contactSUn 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "gc/shared/adaptiveSizePolicy.hpp"
#include "gc/shared/collectorPolicy.hpp"
#include "gc/shared/gcArguments.inline.hpp"
#include "mmtkCollectorPolicy.hpp"
#include "mmtkHeap.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/globals.hpp"
#include "runtime/java.hpp"
#include "runtime/vm_version.hpp"
#include "thirdPartyHeapArguments.hpp"
#include "utilities/defaultStream.hpp"

size_t ThirdPartyHeapArguments::conservative_max_heap_alignment() {
  return CollectorPolicy::compute_heap_alignment();
}

void ThirdPartyHeapArguments::initialize() {
  GCArguments::initialize();
  assert(UseThirdPartyHeap , "Error, should UseThirdPartyHeap");
  FLAG_SET_DEFAULT(UseTLAB, false);
  FLAG_SET_DEFAULT(UseCompressedOops, false);
  FLAG_SET_DEFAULT(UseCompressedClassPointers, false);
}

CollectedHeap* ThirdPartyHeapArguments::create_heap() {
  return create_heap_with_policy<MMTkHeap, MMTkCollectorPolicy>();
}
