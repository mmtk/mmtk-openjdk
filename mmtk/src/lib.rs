// specialization is considered as an incomplete feature.
#![allow(incomplete_features)]
#![feature(specialization)]
#![feature(box_syntax)]
#![feature(vec_into_raw_parts)]
#![feature(once_cell)]

extern crate libc;
extern crate mmtk;
#[macro_use]
extern crate lazy_static;

use std::ptr::null_mut;

use libc::{c_char, c_void, uintptr_t};
use mmtk::scheduler::GCWorker;
use mmtk::util::opaque_pointer::*;
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::VMBinding;
use mmtk::Mutator;
use mmtk::MMTK;
mod abi;
pub mod active_plan;
pub mod api;
pub mod collection;
mod gc_work;
pub mod object_model;
mod object_scanning;
pub mod reference_glue;
pub mod scanning;
pub(crate) mod vm_metadata;

#[repr(C)]
pub struct NewBuffer {
    pub ptr: *mut Address,
    pub capacity: usize,
}

type ProcessEdgesFn = *const extern "C" fn(buf: *mut Address, size: usize, cap: usize) -> NewBuffer;

#[repr(C)]
pub struct OpenJDK_Upcalls {
    pub stop_all_mutators: extern "C" fn(
        tls: VMWorkerThread,
        create_stack_scan_work: *const extern "C" fn(&'static mut Mutator<OpenJDK>),
    ),
    pub resume_mutators: extern "C" fn(tls: VMWorkerThread),
    pub spawn_worker_thread: extern "C" fn(tls: VMThread, ctx: *mut GCWorker<OpenJDK>),
    pub block_for_gc: extern "C" fn(),
    pub get_next_mutator: extern "C" fn() -> *mut Mutator<OpenJDK>,
    pub reset_mutator_iterator: extern "C" fn(),
    pub compute_static_roots: extern "C" fn(trace: *mut c_void, tls: OpaquePointer),
    pub compute_global_roots: extern "C" fn(trace: *mut c_void, tls: OpaquePointer),
    pub compute_thread_roots: extern "C" fn(trace: *mut c_void, tls: OpaquePointer),
    pub scan_object: extern "C" fn(trace: *mut c_void, object: ObjectReference, tls: OpaquePointer),
    pub dump_object: extern "C" fn(object: ObjectReference),
    pub get_object_size: extern "C" fn(object: ObjectReference) -> usize,
    pub get_mmtk_mutator: extern "C" fn(tls: VMMutatorThread) -> *mut Mutator<OpenJDK>,
    pub is_mutator: extern "C" fn(tls: VMThread) -> bool,
    pub enter_vm: extern "C" fn() -> i32,
    pub leave_vm: extern "C" fn(st: i32),
    pub compute_klass_mem_layout_checksum: extern "C" fn() -> usize,
    pub offset_of_static_fields: extern "C" fn() -> i32,
    pub static_oop_field_count_offset: extern "C" fn() -> i32,
    pub referent_offset: extern "C" fn() -> i32,
    pub discovered_offset: extern "C" fn() -> i32,
    pub dump_object_string: extern "C" fn(object: ObjectReference) -> *const c_char,
    pub scan_thread_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub scan_thread_root: extern "C" fn(process_edges: ProcessEdgesFn, tls: VMMutatorThread),
    pub scan_universe_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub scan_jni_handle_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub scan_object_synchronizer_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub scan_management_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub scan_jvmti_export_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub scan_aot_loader_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub scan_system_dictionary_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub scan_code_cache_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub scan_string_table_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub scan_class_loader_data_graph_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub scan_weak_processor_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub scan_vm_thread_roots: extern "C" fn(process_edges: ProcessEdgesFn),
    pub number_of_mutators: extern "C" fn() -> usize,
    pub schedule_finalizer: extern "C" fn(),
}

pub static mut UPCALLS: *const OpenJDK_Upcalls = null_mut();

#[no_mangle]
pub static GLOBAL_SIDE_METADATA_BASE_ADDRESS: uintptr_t =
    crate::mmtk::util::metadata::side_metadata::GLOBAL_SIDE_METADATA_BASE_ADDRESS.as_usize();

#[no_mangle]
pub static GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS: uintptr_t =
    crate::mmtk::util::metadata::side_metadata::GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS.as_usize();

#[no_mangle]
pub static GLOBAL_ALLOC_BIT_ADDRESS: uintptr_t =
    crate::mmtk::util::metadata::side_metadata::ALLOC_SIDE_METADATA_ADDR.as_usize();


#[derive(Default)]
pub struct OpenJDK;

impl VMBinding for OpenJDK {
    type VMObjectModel = object_model::VMObjectModel;
    type VMScanning = scanning::VMScanning;
    type VMCollection = collection::VMCollection;
    type VMActivePlan = active_plan::VMActivePlan;
    type VMReferenceGlue = reference_glue::VMReferenceGlue;
}

lazy_static! {
    pub static ref SINGLETON: MMTK<OpenJDK> = {
        #[cfg(feature = "nogc")]
        std::env::set_var("MMTK_PLAN", "NoGC");
        #[cfg(feature = "semispace")]
        std::env::set_var("MMTK_PLAN", "SemiSpace");
        #[cfg(feature = "gencopy")]
        std::env::set_var("MMTK_PLAN", "GenCopy");
        #[cfg(feature = "marksweep")]
        std::env::set_var("MMTK_PLAN", "MarkSweep");
        #[cfg(feature = "pageprotect")]
        std::env::set_var("MMTK_PLAN", "PageProtect");
        #[cfg(feature = "immix")]
        std::env::set_var("MMTK_PLAN", "Immix");
        MMTK::new()
    };
}
