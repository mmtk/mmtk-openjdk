set -xe

. $(dirname "$0")/common.sh

# With a heap size larger than 4G, OpenJDK encodes compressed pointers differently.
# Run a few plans with heap size larger than 4G.

MMTK_PLAN=Immix runbms_dacapo2006_with_heap_size fop 20 8000
MMTK_PLAN=GenImmix runbms_dacapo2006_with_heap_size fop 20 8000
MMTK_PLAN=StickyImmix runbms_dacapo2006_with_heap_size fop 20 8000
MMTK_PLAN=GenCopy runbms_dacapo2006_with_heap_size fop 20 8000
MMTK_PLAN=MarkCompact runbms_dacapo2006_with_heap_size fop 20 8000
MMTK_PLAN=MarkSweep runbms_dacapo2006_with_heap_size fop 20 8000
