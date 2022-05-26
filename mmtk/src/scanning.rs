use super::gc_work::*;
use super::{NewBuffer, SINGLETON, UPCALLS};
use crate::{EdgesClosure, OpenJDK};
use mmtk::memory_manager;
use mmtk::scheduler::WorkBucketStage;
use mmtk::util::opaque_pointer::*;
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::{EdgeVisitor, RootsWorkFactory, Scanning};
use mmtk::Mutator;
use mmtk::MutatorContext;

pub struct VMScanning {}

/// This allows C++ code to call dynamic methods of the `RootsWorkFactory`.
/// We use the `as_closure` method to presents an `EdgesClosure` struct to C++
/// so that it can call back.
pub(crate) struct ScopedDynamicFactoryInvoker<'f> {
    pub factory: &'f dyn RootsWorkFactory,
}

impl<'f> ScopedDynamicFactoryInvoker<'f> {
    pub(crate) fn new(factory: &'f dyn RootsWorkFactory) -> Self {
        Self { factory }
    }

    pub(crate) fn invoke(&mut self, edges: Vec<Address>) {
        self.factory.create_process_edge_roots_work(edges);
    }

    pub(crate) fn as_closure(&self) -> EdgesClosure {
        EdgesClosure {
            func: report_edges_and_renew_buffer as *const _,
            data: unsafe { std::mem::transmute(self) },
        }
    }
}

const WORK_PACKET_CAPACITY: usize = 4096;

extern "C" fn report_edges_and_renew_buffer(
    ptr: *mut Address,
    length: usize,
    capacity: usize,
    invoker_ptr: *mut libc::c_void,
) -> NewBuffer {
    if !ptr.is_null() {
        let buf = unsafe { Vec::<Address>::from_raw_parts(ptr, length, capacity) };
        let invoker: &mut ScopedDynamicFactoryInvoker<'static> =
            unsafe { &mut *(invoker_ptr as *mut ScopedDynamicFactoryInvoker) };
        invoker.invoke(buf);
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

impl Scanning<OpenJDK> for VMScanning {
    const SCAN_MUTATORS_IN_SAFEPOINT: bool = false;
    const SINGLE_THREAD_MUTATOR_SCANNING: bool = false;

    fn scan_object<EV: EdgeVisitor>(
        tls: VMWorkerThread,
        object: ObjectReference,
        edge_visitor: &mut EV,
    ) {
        crate::object_scanning::scan_object(object, edge_visitor, tls)
    }

    fn notify_initial_thread_scan_complete(_partial_scan: bool, _tls: VMWorkerThread) {
        // unimplemented!()
        // TODO
    }

    fn scan_thread_roots(_tls: VMWorkerThread, factory: Box<dyn RootsWorkFactory>) {
        let invoker = ScopedDynamicFactoryInvoker::new(factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_thread_roots)(invoker.as_closure());
        }
    }

    fn scan_thread_root(
        _tls: VMWorkerThread,
        mutator: &'static mut Mutator<OpenJDK>,
        factory: Box<dyn RootsWorkFactory>,
    ) {
        let tls = mutator.get_tls();
        let invoker = ScopedDynamicFactoryInvoker::new(factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_thread_root)(invoker.as_closure(), tls);
        }
    }

    fn scan_vm_specific_roots(_tls: VMWorkerThread, factory: Box<dyn RootsWorkFactory>) {
        memory_manager::add_work_packets(
            &SINGLETON,
            WorkBucketStage::Prepare,
            vec![
                Box::new(ScanUniverseRoots::new(factory.fork())) as _,
                Box::new(ScanJNIHandlesRoots::new(factory.fork())) as _,
                Box::new(ScanObjectSynchronizerRoots::new(factory.fork())) as _,
                Box::new(ScanManagementRoots::new(factory.fork())) as _,
                Box::new(ScanJvmtiExportRoots::new(factory.fork())) as _,
                Box::new(ScanAOTLoaderRoots::new(factory.fork())) as _,
                Box::new(ScanSystemDictionaryRoots::new(factory.fork())) as _,
                Box::new(ScanCodeCacheRoots::new(factory.fork())) as _,
                Box::new(ScanStringTableRoots::new(factory.fork())) as _,
                Box::new(ScanClassLoaderDataGraphRoots::new(factory.fork())) as _,
                Box::new(ScanWeakProcessorRoots::new(factory.fork())) as _,
            ],
        );
        if !(Self::SCAN_MUTATORS_IN_SAFEPOINT && Self::SINGLE_THREAD_MUTATOR_SCANNING) {
            memory_manager::add_work_packet(
                &SINGLETON,
                WorkBucketStage::Prepare,
                ScanVMThreadRoots::new(factory.fork()),
            );
        }
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
