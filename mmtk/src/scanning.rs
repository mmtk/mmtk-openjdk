use super::gc_works::*;
use super::{NewBuffer, SINGLETON, UPCALLS};
use crate::OpenJDK;
use mmtk::scheduler::gc_works::ProcessEdgesWork;
use mmtk::util::OpaquePointer;
use mmtk::util::{Address, ObjectReference, SynchronizedCounter};
use mmtk::vm::Scanning;
use mmtk::MutatorContext;
use mmtk::{Mutator, TraceLocal, TransitiveClosure};
use std::mem;

pub struct VMScanning {}

pub extern "C" fn create_process_edges_work<W: ProcessEdgesWork<VM = OpenJDK>>(
    ptr: *mut Address,
    length: usize,
    capacity: usize,
) -> NewBuffer {
    if !ptr.is_null() {
        let mut buf = unsafe { Vec::<Address>::from_raw_parts(ptr, length, capacity) };
        SINGLETON.scheduler.closure_stage.add(W::new(buf, false, &SINGLETON));
    }
    let (ptr, _, capacity) = Vec::with_capacity(W::CAPACITY).into_raw_parts();
    NewBuffer { ptr, capacity }
}

impl Scanning<OpenJDK> for VMScanning {
    const SCAN_MUTATORS_IN_SAFEPOINT: bool = false;
    const SINGLE_THREAD_MUTATOR_SCANNING: bool = false;

    fn scan_object<T: TransitiveClosure>(
        trace: &mut T,
        object: ObjectReference,
        tls: OpaquePointer,
    ) {
        crate::object_scanning::scan_object(object, trace, tls)
    }

    fn notify_initial_thread_scan_complete(_partial_scan: bool, _tls: OpaquePointer) {
        // unimplemented!()
        // TODO
    }

    fn scan_objects<W: ProcessEdgesWork<VM = OpenJDK>>(objects: &[ObjectReference]) {
        crate::object_scanning::scan_objects_and_create_edges_work::<W>(&objects);
    }

    fn scan_thread_roots<W: ProcessEdgesWork<VM = OpenJDK>>() {
        let process_edges = create_process_edges_work::<W>;
        unsafe {
            ((*UPCALLS).scan_thread_roots)(process_edges as _, OpaquePointer::UNINITIALIZED);
        }
    }

    fn scan_thread_root<W: ProcessEdgesWork<VM = OpenJDK>>(
        mutator: &'static mut Mutator<OpenJDK>,
        _tls: OpaquePointer,
    ) {
        let tls = mutator.get_tls();
        let process_edges = create_process_edges_work::<W>;
        unsafe {
            ((*UPCALLS).scan_thread_root)(process_edges as _, tls);
        }
    }

    fn scan_vm_specific_roots<W: ProcessEdgesWork<VM = OpenJDK>>() {
        SINGLETON.scheduler.prepare_stage.bulk_add(
            1000,
            vec![
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
            ],
        );
        if !(Self::SCAN_MUTATORS_IN_SAFEPOINT && Self::SINGLE_THREAD_MUTATOR_SCANNING) {
            SINGLETON
                .scheduler
                .prepare_stage
                .add(ScanVMThreadRoots::<W>::new());
        }
    }

    fn supports_return_barrier() -> bool {
        unimplemented!()
    }
}
