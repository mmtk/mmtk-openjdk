
use mmtk::vm::Scanning;
use mmtk::{TransitiveClosure, TraceLocal, Mutator, SelectedPlan};
use mmtk::util::{Address, ObjectReference, SynchronizedCounter};
use mmtk::util::OpaquePointer;
use mmtk::scheduler::gc_works::ProcessEdgesWork;
use crate::OpenJDK;
use super::{UPCALLS, SINGLETON};
use std::mem;
use super::gc_works::*;
use mmtk::MutatorContext;


static COUNTER: SynchronizedCounter = SynchronizedCounter::new(0);

pub struct VMScanning {}

pub extern fn create_process_edges_work<W: ProcessEdgesWork<VM=OpenJDK>>(ptr: *const Address, len: usize) {
    let mut buf = Vec::with_capacity(len);
    for i in 0..len {
        buf.push(unsafe { *ptr.add(i) });
    }
    SINGLETON.scheduler.closure_stage.add(W::new(buf, false));
}

impl Scanning<OpenJDK> for VMScanning {
    const SCAN_MUTATORS_IN_SAFEPOINT: bool = false;
    const SINGLE_THREAD_MUTATOR_SCANNING: bool = false;

    fn scan_object<T: TransitiveClosure>(trace: &mut T, object: ObjectReference, tls: OpaquePointer) {
        crate::object_scanning::scan_object(object, trace, tls)
    }

    fn reset_thread_counter() {
        COUNTER.reset();
    }

    fn notify_initial_thread_scan_complete(_partial_scan: bool, _tls: OpaquePointer) {
        // unimplemented!()
        // TODO
    }

    fn scan_objects<W: ProcessEdgesWork<VM=OpenJDK>>(objects: &[ObjectReference]) {
        crate::object_scanning::scan_objects_and_create_edges_work::<W>(&objects);
    }

    fn scan_thread_roots<W: ProcessEdgesWork<VM=OpenJDK>>() {
        let process_edges = create_process_edges_work::<W>;
        unsafe {
            ((*UPCALLS).scan_thread_roots)(process_edges as _, OpaquePointer::UNINITIALIZED);
        }
    }

    fn scan_thread_root<W: ProcessEdgesWork<VM=OpenJDK>>(mutator: &'static mut Mutator<SelectedPlan<OpenJDK>>) {
        let tls = mutator.get_tls();
        let process_edges = create_process_edges_work::<W>;
        unsafe {
            ((*UPCALLS).scan_thread_root)(process_edges as _, tls);
        }
    }

    fn scan_vm_specific_roots<W: ProcessEdgesWork<VM=OpenJDK>>() {
        SINGLETON.scheduler.prepare_stage.bulk_add(1000, vec![
            box ScanUniverseRoots::<W>::new(),
            box ScanJNIHandlesRoots::<W>::new(),
            box ScanObjectSynchronizerRoots::<W>::new(),
            box ScanManagementRoots::<W>::new(),
            box ScanJvmtiExportRoots::<W>::new(),
            box ScanAOTLoaderRoots::<W>::new(),
            box ScanSystemDictionaryRoots::<W>::new(),
            box ScanCodeCacheRoots::<W>::new(),
            box ScanStringTableRoots::<W>::new(),
            box ScanClassLoaderDataGraphRoots::<W>::new(),
            box ScanWeakProcessorRoots::<W>::new(),
        ]);
        if !(Self::SCAN_MUTATORS_IN_SAFEPOINT && Self::SINGLE_THREAD_MUTATOR_SCANNING) {
            SINGLETON.scheduler.prepare_stage.add(ScanVMThreadRoots::<W>::new());
        }
    }

    fn supports_return_barrier() -> bool {
        unimplemented!()
    }
}