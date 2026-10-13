use crate::gc_work::*;
use crate::Slot;
use crate::{NewBuffer, OpenJDKSlot, UPCALLS};
use crate::{OpenJDK, SlotsClosure};
use mmtk::memory_manager;
use mmtk::scheduler::RootKind;
use mmtk::util::opaque_pointer::*;
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::{RootsWorkFactory, Scanning, SlotVisitor};
use mmtk::Mutator;
use mmtk::MutatorContext;

pub struct VMScanning {}

pub(crate) const WORK_PACKET_CAPACITY: usize = mmtk::scheduler::EDGES_WORK_BUFFER_SIZE;

extern "C" fn report_slots_and_renew_buffer<S: Slot, F: RootsWorkFactory<S>>(
    ptr: *mut Address,
    length: usize,
    capacity: usize,
    factory_ptr: *mut libc::c_void,
) -> NewBuffer {
    if !ptr.is_null() {
        // Note: Currently OpenJDKSlot has the same layout as Address.  If the layout changes, we
        // should fix the Rust-to-C interface.
        let buf = unsafe { Vec::<S>::from_raw_parts(ptr as _, length, capacity) };
        let factory: &mut F = unsafe { &mut *(factory_ptr as *mut F) };
        factory.create_process_roots_work_with_root_kind(buf, RootKind::Strong);
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

/// Shade objects during concurrent marking: see [`mmtk::memory_manager::shade_objects`].
///
/// # Safety
/// `objects` must point to `len` valid object references.
#[no_mangle]
pub unsafe extern "C" fn mmtk_shade_objects(objects: *const ObjectReference, len: usize) {
    if len == 0 {
        return;
    }
    let objects = std::slice::from_raw_parts(objects, len);
    if crate::use_compressed_oops() {
        memory_manager::shade_objects(crate::singleton::<true>(), objects);
    } else {
        memory_manager::shade_objects(crate::singleton::<false>(), objects);
    }
}

/// Like [`report_slots_and_renew_buffer`], but the factory is behind a mutex, so the closure can be
/// invoked from several threads at the same time (for example from handshake closures).
extern "C" fn report_slots_and_renew_buffer_sync<S: Slot, F: RootsWorkFactory<S>>(
    ptr: *mut Address,
    length: usize,
    capacity: usize,
    factory_ptr: *mut libc::c_void,
) -> NewBuffer {
    let factory: &std::sync::Mutex<F> = unsafe { &*(factory_ptr as *const std::sync::Mutex<F>) };
    let mut factory = factory.lock().unwrap();
    report_slots_and_renew_buffer::<S, F>(ptr, length, capacity, &mut *factory as *mut F as _)
}

pub(crate) fn to_slots_closure<S: Slot, F: RootsWorkFactory<S>>(factory: &mut F) -> SlotsClosure {
    SlotsClosure {
        func: report_slots_and_renew_buffer::<S, F>,
        data: factory as *mut F as *mut libc::c_void,
    }
}

impl<const COMPRESSED: bool> Scanning<OpenJDK<COMPRESSED>> for VMScanning {
    fn scan_object(
        tls: VMWorkerThread,
        object: ObjectReference,
        slot_visitor: &mut impl SlotVisitor<OpenJDKSlot<COMPRESSED>>,
    ) {
        crate::object_scanning::scan_object::<COMPRESSED>(object, slot_visitor, tls);
    }

    fn notify_initial_thread_scan_complete(_partial_scan: bool, _tls: VMWorkerThread) {
        // unimplemented!()
        // TODO
    }

    fn scan_roots_in_mutator_thread(
        _tls: VMWorkerThread,
        mutator: &'static mut Mutator<OpenJDK<COMPRESSED>>,
        mut factory: impl RootsWorkFactory<OpenJDKSlot<COMPRESSED>>,
    ) {
        let tls = mutator.get_tls();
        unsafe {
            ((*UPCALLS).scan_roots_in_mutator_thread)(to_slots_closure(&mut factory), tls);
        }
    }

    fn scan_vm_specific_roots(
        _tls: VMWorkerThread,
        factory: impl RootsWorkFactory<OpenJDKSlot<COMPRESSED>>,
    ) {
        let mut w: Vec<Box<dyn mmtk::scheduler::GCWork<OpenJDK<COMPRESSED>>>> = vec![
            Box::new(ScanCodeCacheRoots::new(factory.clone())),
            Box::new(ScanClassLoaderDataGraphRoots::new(factory.clone())),
            Box::new(ScanVMThreadRoots::new(factory.clone())),
        ];
        for _ in 0..*crate::singleton::<COMPRESSED>().get_options().threads {
            w.push(Box::new(ScanOopStorageSetRoots::new(factory.clone())));
            w.push(Box::new(ScanWeakProcessorRoots::new(factory.clone())));
        }
        let mmtk = crate::singleton::<COMPRESSED>();
        let stage = crate::singleton::<COMPRESSED>()
            .get_plan()
            .root_scanning_stage();
        memory_manager::add_work_packets(mmtk, stage, w);
    }

    fn supports_return_barrier() -> bool {
        unimplemented!()
    }

    fn supports_concurrent_stack_scanning() -> bool {
        true
    }

    fn scan_roots_in_mutator_threads_concurrently(
        _tls: VMWorkerThread,
        factory: impl RootsWorkFactory<OpenJDKSlot<COMPRESSED>>,
    ) {
        // Each Java thread scans its own stack in a handshake, and the VM thread scans the
        // stacks of blocked threads, so the factory is shared between threads.
        let factory = std::sync::Mutex::new(factory);
        fn closure<S: Slot, T: RootsWorkFactory<S>>(factory: &std::sync::Mutex<T>) -> SlotsClosure {
            SlotsClosure {
                func: report_slots_and_renew_buffer_sync::<S, T>,
                data: factory as *const std::sync::Mutex<T> as *mut libc::c_void,
            }
        }
        unsafe {
            ((*UPCALLS).scan_roots_in_mutator_threads_concurrently)(closure(&factory));
        }
    }

    fn prepare_for_roots_re_scanning() {
        unsafe {
            ((*UPCALLS).prepare_for_roots_re_scanning)();
        }
    }
}
