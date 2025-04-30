#ifndef MMTK_OPENJDK_BARRIERS_MMTK_NO_BARRIER_HPP
#define MMTK_OPENJDK_BARRIERS_MMTK_NO_BARRIER_HPP

#include "../mmtkBarrierSet.hpp"
#include "utilities/macros.hpp"
#include CPU_HEADER(mmtkBarrierSetAssembler)
#include CPU_HEADER(mmtkNoBarrierSetAssembler)
#ifdef COMPILER1
#include "../mmtkBarrierSetC1.hpp"
class MMTkNoBarrierSetC1: public MMTkBarrierSetC1 {};
#else
class MMTkNoBarrierSetC1;
#endif

#ifdef COMPILER2
#include "../mmtkBarrierSetC2.hpp"
class MMTkNoBarrierSetC2: public MMTkBarrierSetC2 {};
#else
class MMTkNoBarrierSetC2;
#endif

class MMTkNoBarrierSetRuntime: public MMTkBarrierSetRuntime {};

struct MMTkNoBarrier: MMTkBarrierImpl<
  MMTkNoBarrierSetRuntime,
  MMTkNoBarrierSetAssembler,
  MMTkNoBarrierSetC1,
  MMTkNoBarrierSetC2
> {};

#endif // MMTK_OPENJDK_BARRIERS_MMTK_NO_BARRIER_HPP
