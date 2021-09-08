#ifndef MMTK_OPENJDK_BARRIERS_MMTK_NO_BARRIER_HPP
#define MMTK_OPENJDK_BARRIERS_MMTK_NO_BARRIER_HPP

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

#endif // MMTK_OPENJDK_BARRIERS_MMTK_NO_BARRIER_HPP
