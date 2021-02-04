#ifndef MMTK_BARRIERS_NO_BARRIER
#define MMTK_BARRIERS_NO_BARRIER

#include "../mmtkBarrierSet.hpp"
#include "../mmtkBarrierSetAssembler_x86.hpp"
#include "../mmtkBarrierSetC1.hpp"
#include "../mmtkBarrierSetC2.hpp"

class MMTkNoBarrierRuntime: public MMTkBarrierRuntime {};

class MMTkNoBarrierAssembler: public MMTkBarrierAssembler {};

class MMTkNoBarrierC1: public MMTkBarrierC1 {};

class MMTkNoBarrierC2: public MMTkBarrierC2 {};

struct MMTkNoBarrier {
  typedef MMTkNoBarrierRuntime Runtime;
  typedef MMTkNoBarrierAssembler Assembler;
  typedef MMTkNoBarrierC1 C1;
  typedef MMTkNoBarrierC2 C2;
};

#endif