#ifndef MMTK_BARRIERS_NO_BARRIER
#define MMTK_BARRIERS_NO_BARRIER

#include "../mmtkBarrierSet.hpp"
#include "../mmtkBarrierSetAssembler_x86.hpp"
#include "../mmtkBarrierSetC1.hpp"
#include "../mmtkBarrierSetC2.hpp"

class MMTkNoBarrierSetRuntime: public MMTkBarrierSetRuntime {};

class MMTkNoBarrierSetAssembler: public MMTkBarrierSetAssembler {};

class MMTkNoBarrierSetC1: public MMTkBarrierSetC1 {};

class MMTkNoBarrierSetC2: public MMTkBarrierSetC2 {};

struct MMTkNoBarrier: MMTkBarrierImpl<
  MMTkNoBarrierSetRuntime,
  MMTkNoBarrierSetAssembler,
  MMTkNoBarrierSetC1,
  MMTkNoBarrierSetC2
> {};

#endif