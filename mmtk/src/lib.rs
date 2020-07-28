#![feature(specialization)]
#![feature(const_fn)]
extern crate mmtk;
extern crate libc;
#[macro_use]
extern crate lazy_static;

use std::ptr::null_mut;

use mmtk::vm::VMBinding;
use mmtk::util::OpaquePointer;
use mmtk::MMTK;
use mmtk::util::{Address, ObjectReference};
use mmtk::{Plan, SelectedPlan};
use libc::c_void;
pub mod scanning;
pub mod collection;
pub mod object_model;
pub mod active_plan;
pub mod reference_glue;
pub mod api;
mod object_scanning;

#[repr(C)]
pub struct OpenJDK_Upcalls {
    pub stop_all_mutators: extern "C" fn(tls: OpaquePointer),
    pub resume_mutators: extern "C" fn(tls: OpaquePointer),
    pub spawn_collector_thread: extern "C" fn(tls: OpaquePointer, ctx: *mut <SelectedPlan<OpenJDK> as Plan<OpenJDK>>::CollectorT),
    pub block_for_gc: extern "C" fn(),
    pub active_collector: extern "C" fn(tls: OpaquePointer) -> *mut <SelectedPlan<OpenJDK> as Plan<OpenJDK>>::CollectorT,
    pub get_next_mutator: extern "C" fn() -> *mut <SelectedPlan<OpenJDK> as Plan<OpenJDK>>::MutatorT,
    pub reset_mutator_iterator: extern "C" fn(),
    pub compute_static_roots: extern "C" fn(trace: *mut c_void, tls: OpaquePointer),
    pub compute_global_roots: extern "C" fn(trace: *mut c_void, tls: OpaquePointer),
    pub compute_thread_roots: extern "C" fn(trace: *mut c_void, tls: OpaquePointer),
    pub scan_object: extern "C" fn(trace: *mut c_void, object: ObjectReference, tls: OpaquePointer),
    pub dump_object: extern "C" fn(object: ObjectReference),
    pub get_object_size: extern "C" fn(object: ObjectReference) -> usize,
    pub get_mmtk_mutator: extern "C" fn(tls: OpaquePointer) -> *mut <SelectedPlan<OpenJDK> as Plan<OpenJDK>>::MutatorT,
    pub is_mutator: extern "C" fn(tls: OpaquePointer) -> bool,
    pub enter_vm: extern "C" fn() -> i32,
    pub leave_vm: extern "C" fn(st: i32),
    pub validate_klass_mem_layout: extern "C" fn(klass_size: usize, instanceklass_size: usize),
    pub start_of_static_fields: extern "C" fn(obj: *const c_void) -> Address,
    pub static_oop_field_count: extern "C" fn(obj: *const c_void) -> i32,
}

pub static mut UPCALLS: *const OpenJDK_Upcalls = null_mut();

pub struct OpenJDK;

impl VMBinding for OpenJDK {
    type VMObjectModel = object_model::VMObjectModel;
    type VMScanning = scanning::VMScanning;
    type VMCollection = collection::VMCollection;
    type VMActivePlan = active_plan::VMActivePlan;
    type VMReferenceGlue = reference_glue::VMReferenceGlue;
}

lazy_static! {
    pub static ref SINGLETON: MMTK<OpenJDK> = MMTK::new();
}
