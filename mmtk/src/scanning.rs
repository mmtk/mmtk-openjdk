use crate::gc_work::*;
use crate::Edge;
use crate::{EdgesClosure, OpenJDK};
use crate::{NewBuffer, OpenJDKEdge, UPCALLS};
use mmtk::memory_manager;
use mmtk::scheduler::WorkBucketStage;
use mmtk::util::opaque_pointer::*;
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::{EdgeVisitor, RootsWorkFactory, Scanning};
use mmtk::Mutator;
use mmtk::MutatorContext;

pub struct VMScanning {}

const WORK_PACKET_CAPACITY: usize = 4096;

extern "C" fn report_edges_and_renew_buffer<E: Edge, F: RootsWorkFactory<E>>(
    ptr: *mut Address,
    length: usize,
    capacity: usize,
    factory_ptr: *mut libc::c_void,
) -> NewBuffer {
    if !ptr.is_null() {
        // Note: Currently OpenJDKEdge has the same layout as Address.  If the layout changes, we
        // should fix the Rust-to-C interface.
        let buf = unsafe { Vec::<E>::from_raw_parts(ptr as _, length, capacity) };
        let factory: &mut F = unsafe { &mut *(factory_ptr as *mut F) };
        factory.create_process_edge_roots_work(buf);
    }
    let (ptr, _, capacity) = {
        // TODO: Use Vec::into_raw_parts() when the method is available.
        use std::mem::ManuallyDrop;
        let new_vec = Vec::with_capacity(WORK_PACKET_CAPACITY);
        let mut me = ManuallyDrop::new(new_vec);
        (me.as_mut_ptr(), me.len(), me.capacity())
    };
    NewBuffer { ptr, capacity }
}

pub(crate) fn to_edges_closure<E: Edge, F: RootsWorkFactory<E>>(factory: &mut F) -> EdgesClosure {
    EdgesClosure {
        func: report_edges_and_renew_buffer::<E, F>,
        data: factory as *mut F as *mut libc::c_void,
    }
}

impl<const COMPRESSED: bool> Scanning<OpenJDK<COMPRESSED>> for VMScanning {
    fn scan_object<EV: EdgeVisitor<OpenJDKEdge<COMPRESSED>>>(
        tls: VMWorkerThread,
        object: ObjectReference,
        edge_visitor: &mut EV,
    ) {
        crate::object_scanning::scan_object::<COMPRESSED>(object, edge_visitor, tls);
    }

    fn notify_initial_thread_scan_complete(_partial_scan: bool, _tls: VMWorkerThread) {
        // unimplemented!()
        // TODO
    }

    fn scan_roots_in_mutator_thread(
        _tls: VMWorkerThread,
        mutator: &'static mut Mutator<OpenJDK<COMPRESSED>>,
        mut factory: impl RootsWorkFactory<OpenJDKEdge<COMPRESSED>>,
    ) {
        let tls = mutator.get_tls();
        unsafe {
            ((*UPCALLS).scan_roots_in_mutator_thread)(to_edges_closure(&mut factory), tls);
        }
    }

    fn scan_vm_specific_roots(
        _tls: VMWorkerThread,
        factory: impl RootsWorkFactory<OpenJDKEdge<COMPRESSED>>,
    ) {
        memory_manager::add_work_packets(
            &crate::singleton::<COMPRESSED>(),
            WorkBucketStage::Prepare,
            vec![
                Box::new(ScanUniverseRoots::new(factory.clone())) as _,
                Box::new(ScanJNIHandlesRoots::new(factory.clone())) as _,
                Box::new(ScanObjectSynchronizerRoots::new(factory.clone())) as _,
                Box::new(ScanManagementRoots::new(factory.clone())) as _,
                Box::new(ScanJvmtiExportRoots::new(factory.clone())) as _,
                Box::new(ScanAOTLoaderRoots::new(factory.clone())) as _,
                Box::new(ScanSystemDictionaryRoots::new(factory.clone())) as _,
                Box::new(ScanCodeCacheRoots::new(factory.clone())) as _,
                Box::new(ScanStringTableRoots::new(factory.clone())) as _,
                Box::new(ScanClassLoaderDataGraphRoots::new(factory.clone())) as _,
                Box::new(ScanWeakProcessorRoots::new(factory.clone())) as _,
                Box::new(ScanVMThreadRoots::new(factory)) as _,
            ],
        );
    }

    fn supports_return_barrier() -> bool {
        unimplemented!()
    }

    fn prepare_for_roots_re_scanning() {
        unsafe {
            ((*UPCALLS).prepare_for_roots_re_scanning)();
        }
    }
}
