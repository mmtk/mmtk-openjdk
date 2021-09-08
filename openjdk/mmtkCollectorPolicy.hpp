#ifndef MMTK_OPENJDK_MMTK_COLLECTOR_POLICY_HPP
#define MMTK_OPENJDK_MMTK_COLLECTOR_POLICY_HPP

class MMTkCollectorPolicy : public CollectorPolicy {
protected:
  virtual void initialize_alignments() {
    _space_alignment =  1 << 19;
    _heap_alignment = _space_alignment;
  }
public:
  MMTkCollectorPolicy() {}
};
#endif // MMTK_OPENJDK_MMTK_COLLECTOR_POLICY_HPP
