
use mmtk::vm::Scanning;
use mmtk::{TransitiveClosure, TraceLocal};
use mmtk::util::{Address, ObjectReference, SynchronizedCounter};
use mmtk::util::OpaquePointer;
use mmtk::work::*;
use crate::OpenJDK;
use super::{UPCALLS, SINGLETON};
use std::mem;


static COUNTER: SynchronizedCounter = SynchronizedCounter::new(0);

pub struct VMScanning {}

extern fn create_process_edges_work<W: ProcessEdgesWork<VM=OpenJDK>>(ptr: *const Address, len: usize) {
    println!("create_process_edges_work");
    let mut buf = Vec::with_capacity(len);
    for i in 0..len {
        buf.push(unsafe { *ptr.add(i) });
    }
    SINGLETON.scheduler.closure_stage.add(box W::new(buf, false));
}

impl Scanning<OpenJDK> for VMScanning {
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
        let mut edges = Vec::with_capacity(W::CAPACITY);
        crate::object_scanning::scan_objects(&objects, &mut |edge| {
            edges.push(edge);
            if edges.len() >= W::CAPACITY {
                let mut new_edges = Vec::with_capacity(W::CAPACITY);
                mem::swap(&mut new_edges, &mut edges);
                debug_assert!(new_edges.len() > 0);
                debug_assert!(edges.len() == 0);
                SINGLETON.scheduler.closure_stage.add(box W::new(new_edges, false));
            }
        });
        if edges.len() > 0 {
            SINGLETON.scheduler.closure_stage.add(box W::new(edges, false));
        }
    }

    fn scan_thread_roots<W: ProcessEdgesWork<VM=OpenJDK>>() {
        let process_edges = create_process_edges_work::<W>;
        unsafe {
            ((*UPCALLS).scan_thread_roots)(process_edges as _, OpaquePointer::UNINITIALIZED);
        }
    }

    fn scan_global_roots<W: ProcessEdgesWork<VM=OpenJDK>>() {
        let process_edges = create_process_edges_work::<W>;
        unsafe {
            ((*UPCALLS).scan_global_roots)(process_edges as _, OpaquePointer::UNINITIALIZED);
        }
    }

    fn scan_static_roots<W: ProcessEdgesWork<VM=OpenJDK>>() {
        let process_edges = create_process_edges_work::<W>;
        unsafe {
            ((*UPCALLS).scan_static_roots)(process_edges as _, OpaquePointer::UNINITIALIZED);
        }
    }

    fn compute_static_roots<T: TransitiveClosure>(trace: &mut T, tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).compute_static_roots)(::std::mem::transmute(trace), tls);
        }
    }

    fn compute_global_roots<T: TraceLocal>(trace: &mut T, tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).compute_global_roots)(::std::mem::transmute(trace), tls);
        }
    }

    fn compute_thread_roots<T: TraceLocal>(trace: &mut T, tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).compute_thread_roots)(::std::mem::transmute(trace), tls);
        }
    }

    fn compute_new_thread_roots<T: TraceLocal>(_trace: &mut T, _tls: OpaquePointer) {
        unimplemented!()
    }

    fn compute_bootimage_roots<T: TraceLocal>(_trace: &mut T, _tls: OpaquePointer) {
        // Do nothing
    }

    fn supports_return_barrier() -> bool {
        unimplemented!()
    }
}