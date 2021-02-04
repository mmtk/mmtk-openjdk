#include "mmtkObjectBarrier.hpp"
#include "runtime/interfaceSupport.inline.hpp"

void MMTkObjectBarrierRuntime::record_modified_node_slow(void* obj) {
  ::record_modified_node((MMTk_Mutator) &Thread::current()->third_party_heap_mutator, (void*) obj);
}

#define __ ce->masm()->
void MMTkObjectBarrierAssembler::gen_write_barrier_stub(LIR_Assembler* ce, MMTkObjectBarrierStub* stub) {
  MMTkObjectBarrierC1* bs = (MMTkObjectBarrierC1*) ((MMTkBarrierSet*) BarrierSet::barrier_set())->_c1;
  __ bind(*stub->entry());
  ce->store_parameter(stub->_src->as_pointer_register(), 0);
  __ call(RuntimeAddress(bs->_write_barrier_c1_runtime_code_blob->code_begin()));
  __ jmp(*stub->continuation());
}
#undef __