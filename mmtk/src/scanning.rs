use crate::gc_work::*;
use crate::Slot;
use crate::{NewBuffer, OpenJDKSlot, UPCALLS};
use crate::{OpenJDK, SlotsClosure};
use mmtk::memory_manager;
use mmtk::scheduler::RootKind;
use mmtk::util::opaque_pointer::*;
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::ObjectKind;
use mmtk::vm::{RootsWorkFactory, Scanning, SlotVisitor};
use mmtk::Mutator;
use mmtk::MutatorContext;

pub struct VMScanning {}

#[allow(unused)]
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
        if cfg!(feature = "roots_breakdown") {
            super::gc_work::record_roots(buf.len());
        }
        let factory: &mut F = unsafe { &mut *(factory_ptr as *mut F) };
        factory.create_process_roots_work(buf, RootKind::Strong);
    }
    let (ptr, _, capacity) = {
        // TODO: Use Vec::into_raw_parts() when the method is available.
        use std::mem::ManuallyDrop;
        let new_vec = Vec::with_capacity(F::BUFFER_SIZE);
        let mut me = ManuallyDrop::new(new_vec);
        (me.as_mut_ptr(), me.len(), me.capacity())
    };
    NewBuffer { ptr, capacity }
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

    fn scan_object_with_klass(
        tls: VMWorkerThread,
        object: ObjectReference,
        slot_visitor: &mut impl SlotVisitor<OpenJDKSlot<COMPRESSED>>,
        klass: Address,
    ) {
        crate::object_scanning::scan_object_with_klass::<COMPRESSED>(
            object,
            slot_visitor,
            tls,
            klass,
        );
    }

    fn obj_array_data(o: ObjectReference) -> crate::OpenJDKSlotRange<COMPRESSED> {
        crate::object_scanning::obj_array_data::<COMPRESSED>(unsafe { std::mem::transmute(o) })
    }

    fn is_obj_array(o: ObjectReference) -> bool {
        crate::object_scanning::is_obj_array::<COMPRESSED>(unsafe { std::mem::transmute(o) })
    }

    fn is_val_array(o: ObjectReference) -> bool {
        crate::object_scanning::is_val_array::<COMPRESSED>(unsafe { std::mem::transmute(o) })
    }

    fn get_obj_kind(o: ObjectReference) -> ObjectKind {
        crate::object_scanning::get_obj_kind::<COMPRESSED>(unsafe { std::mem::transmute(o) })
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

    fn scan_multiple_thread_root(
        _tls: VMWorkerThread,
        mutators: Vec<VMMutatorThread>,
        mut factory: impl RootsWorkFactory<<OpenJDK<COMPRESSED> as mmtk::vm::VMBinding>::VMSlot>,
    ) {
        // let t = if cfg!(feature = "roots_breakdown") {
        //     Some(std::time::SystemTime::now())
        // } else {
        //     None
        // };
        let len = mutators.len();
        let ptr = mutators.as_ptr();
        unsafe {
            ((*UPCALLS).scan_multiple_thread_roots)(
                to_slots_closure(&mut factory),
                std::mem::transmute(ptr),
                len,
            );
        }
        // if cfg!(feature = "roots_breakdown") {
        //     let ms = t.unwrap().elapsed().unwrap().as_micros() as f32 / 1000f32;
        //     eprintln!(" - ScanThreadRoots ({:.3}ms)", ms);
        // }
    }

    fn scan_vm_specific_roots(
        _tls: VMWorkerThread,
        factory: impl RootsWorkFactory<OpenJDKSlot<COMPRESSED>>,
    ) {
        let mut w: Vec<Box<dyn mmtk::scheduler::GCWork<OpenJDK<COMPRESSED>>>> = vec![
            Box::new(ScanCodeCacheRoots::new(factory.clone())) as _,
            Box::new(ScanClassLoaderDataGraphRoots::new(factory.clone())) as _,
            Box::new(ScanOopStorageSetRoots::new(factory.clone())) as _,
            Box::new(ScanVMThreadRoots::new(factory.clone())) as _,
        ];
        if crate::singleton::<COMPRESSED>()
            .get_plan()
            .requires_weak_root_scanning()
        {
            w.push(Box::new(ScanNewWeakHandleRoots::new(factory.clone())) as _);
        }
        // if crate::singleton::<COMPRESSED>()
        //     .get_plan()
        //     .current_gc_should_perform_class_unloading()
        // {
        //     // w.push(Box::new(ScanWeakStringTableRoots::new(factory.clone())) as _);
        //     w.push(Box::new(ScanWeakCodeCacheRoots::new(factory.clone())) as _);
        // }
        memory_manager::add_work_packets(
            crate::singleton::<COMPRESSED>(),
            factory.roots_stage(),
            w,
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
