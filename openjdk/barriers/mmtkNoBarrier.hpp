#ifndef MMTK_BARRIERS_NO_BARRIER
#define MMTK_BARRIERS_NO_BARRIER

#include "../mmtkBarrierSet.hpp"
#include "../mmtkBarrierSetAssembler_x86.hpp"
#include "../mmtkBarrierSetC1.hpp"
#include "../mmtkBarrierSetC2.hpp"

class MMTkNoBarrierRuntime: public MMTkBarrierRuntime {
public:
  virtual void record_modified_node(void* src);
  virtual void record_modified_edge(void* slot);
};

class MMTkNoBarrierAssembler: public MMTkBarrierAssembler {};

class MMTkNoBarrierC1: public MMTkBarrierC1 {};

class MMTkNoBarrierC2: public MMTkBarrierC2 {};

#endif