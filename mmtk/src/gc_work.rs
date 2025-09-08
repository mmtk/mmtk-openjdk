use crate::scanning;
use crate::scanning::to_slots_closure;
use crate::OpenJDK;
use crate::OpenJDKSlot;
use crate::UPCALLS;
use mmtk::scheduler::*;
use mmtk::util::Address;
use mmtk::vm::RootsWorkFactory;
use mmtk::vm::*;
use mmtk::MMTK;

macro_rules! scan_roots_work {
    ($struct_name: ident, $func_name: ident) => {
        pub struct $struct_name<VM: VMBinding, F: RootsWorkFactory<VM::VMSlot>> {
            factory: F,
            _p: std::marker::PhantomData<VM>,
        }

        impl<VM: VMBinding, F: RootsWorkFactory<VM::VMSlot>> $struct_name<VM, F> {
            pub fn new(factory: F) -> Self {
                Self {
                    factory,
                    _p: std::marker::PhantomData,
                }
            }
        }

        impl<VM: VMBinding, F: RootsWorkFactory<VM::VMSlot>> GCWork<VM> for $struct_name<VM, F> {
            fn do_work(&mut self, _worker: &mut GCWorker<VM>, _mmtk: &'static MMTK<VM>) {
                unsafe {
                    ((*UPCALLS).$func_name)(to_slots_closure(&mut self.factory));
                }
            }
        }
    };
}

scan_roots_work!(
    ScanClassLoaderDataGraphRoots,
    scan_class_loader_data_graph_roots
);
scan_roots_work!(ScanOopStorageSetRoots, scan_oop_storage_set_roots);
scan_roots_work!(ScanWeakProcessorRoots, scan_weak_processor_roots);
scan_roots_work!(ScanVMThreadRoots, scan_vm_thread_roots);

pub struct ScanCodeCacheRoots<const COMPRESSED: bool, F: RootsWorkFactory<OpenJDKSlot<COMPRESSED>>>
{
    factory: F,
}

impl<const COMPRESSED: bool, F: RootsWorkFactory<OpenJDKSlot<COMPRESSED>>>
    ScanCodeCacheRoots<COMPRESSED, F>
{
    pub fn new(factory: F) -> Self {
        Self { factory }
    }
}

impl<const COMPRESSED: bool, F: RootsWorkFactory<OpenJDKSlot<COMPRESSED>>>
    GCWork<OpenJDK<COMPRESSED>> for ScanCodeCacheRoots<COMPRESSED, F>
{
    fn do_work(
        &mut self,
        worker: &mut GCWorker<OpenJDK<COMPRESSED>>,
        mmtk: &'static MMTK<OpenJDK<COMPRESSED>>,
    ) {
        let is_current_gc_nursery = mmtk
            .get_plan()
            .generational()
            .is_some_and(|gen| gen.is_current_gc_nursery());

        let mut slots = Vec::with_capacity(scanning::WORK_PACKET_CAPACITY);

        let mut nursery_slots = 0;
        let mut mature_slots = 0;

        let mut add_roots = |roots: &[Address]| {
            for root in roots {
                slots.push(OpenJDKSlot::<COMPRESSED>::from(*root));
                if slots.len() >= scanning::WORK_PACKET_CAPACITY {
                    self.factory
                        .create_process_roots_work(std::mem::take(&mut slots));
                }
            }
        };

        let moves_object = mmtk.get_plan().current_gc_may_move_object();

        // nmethods which we need to fix relocations.
        // That includes all nmethods with moved children.
        // In nursery GCs, that means nmethods added since the previous GC.
        let mut nmethods_to_fix = Vec::new();

        {
            let mut mature = crate::MATURE_CODE_CACHE_ROOTS.lock().unwrap();

            // Only scan mature roots in full-heap collections.
            if !is_current_gc_nursery {
                for (key, roots) in mature.iter() {
                    mature_slots += roots.len();
                    add_roots(roots);
                    if moves_object {
                        nmethods_to_fix.push(*key);
                    }
                }
            }

            {
                let mut nursery = crate::NURSERY_CODE_CACHE_ROOTS.lock().unwrap();
                for (key, roots) in nursery.drain() {
                    nursery_slots += roots.len();
                    add_roots(&roots);
                    mature.insert(key, roots);
                    if moves_object {
                        nmethods_to_fix.push(key);
                    }
                }
            }
        }

        let num_nmethods = nmethods_to_fix.len();
        probe!(
            mmtk_openjdk,
            code_cache_roots,
            nursery_slots,
            mature_slots,
            num_nmethods
        );

        if !slots.is_empty() {
            self.factory.create_process_roots_work(slots);
        }
        // Use the following code to scan CodeCache directly, instead of scanning the "remembered set".
        // unsafe {
        //     ((*UPCALLS).scan_code_cache_roots)(to_slots_closure(&mut self.factory));
        // }

        if moves_object {
            // Note: If the current GC doesn't move objects at all, we don't need to fix relocation.
            // FIXME: Even during copying GC, some GC algorithms (such as Immix) don't move every
            // single object.  We only need to call `fix_oop_relocations` on nmethods that actually
            // have moved children.

            let packets = nmethods_to_fix
                .chunks(FixRelocations::NMETHODS_PER_PACKET)
                .map(|chunk| {
                    let nmethods = chunk.to_vec();
                    Box::new(FixRelocations { nmethods }) as _
                })
                .collect();

            // fix_oop_relocations copies the forwarded oops from the nmethod headers back to
            // immediate operands in the machine code.  This can only be done after all fields of an
            // nmethod have been forwarded.
            let stage = if mmtk.get_plan().constraints().needs_forward_after_liveness {
                // For MarkCompact, we forward the children of nmethods in the transitive closure
                // starting with SecondRoots.  RefForwarding is the first safe place to call
                // fix_oop_relocations.
                WorkBucketStage::RefForwarding
            } else {
                // For scavenging GCs, the mmtk-openjdk binding reports the *slots* of nmethods as
                // roots. They will be traced at unspecified times during the Closure stage.
                // SoftRefClosure is the first safe place to call fix_oop_relocations.
                WorkBucketStage::SoftRefClosure
            };
            worker.scheduler().work_buckets[stage].bulk_add(packets);
        }
    }
}

struct FixRelocations {
    nmethods: Vec<Address>,
}

impl FixRelocations {
    /// The number of nmethods per packet. This value is selected for load-balancing.  Processing
    /// one nmethod is significantly more expensive than processing one slot.
    pub const NMETHODS_PER_PACKET: usize = 64;
}

impl<const COMPRESSED: bool> GCWork<OpenJDK<COMPRESSED>> for FixRelocations {
    fn do_work(
        &mut self,
        _worker: &mut GCWorker<OpenJDK<COMPRESSED>>,
        _mmtk: &'static MMTK<OpenJDK<COMPRESSED>>,
    ) {
        let num_nmethods = self.nmethods.len();
        for nmethod in self.nmethods.iter().copied() {
            unsafe {
                ((*UPCALLS).fix_oop_relocations)(nmethod.to_mut_ptr());
            }
        }
        probe!(mmtk_openjdk, fix_relocations, num_nmethods);
    }
}
