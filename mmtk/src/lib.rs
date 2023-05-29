#[macro_use]
extern crate lazy_static;

use std::collections::HashMap;
use std::ops::Range;
use std::ptr::null_mut;
use std::sync::atomic::AtomicUsize;
use std::sync::Mutex;

use libc::{c_char, c_void, uintptr_t};
use mmtk::util::alloc::AllocationError;
use mmtk::util::opaque_pointer::*;
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::VMBinding;
use mmtk::{MMTKBuilder, Mutator, MMTK};

mod abi;
pub mod active_plan;
pub mod api;
mod build_info;
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

/// A closure for reporting mutators.  The C++ code should pass `data` back as the last argument.
#[repr(C)]
pub struct MutatorClosure {
    pub func: extern "C" fn(mutator: *mut Mutator<OpenJDK>, data: *mut libc::c_void),
    pub data: *mut libc::c_void,
}

/// A closure for reporting root edges.  The C++ code should pass `data` back as the last argument.
#[repr(C)]
pub struct EdgesClosure {
    pub func: extern "C" fn(
        buf: *mut Address,
        size: usize,
        cap: usize,
        data: *mut libc::c_void,
    ) -> NewBuffer,
    pub data: *const libc::c_void,
}

#[repr(C)]
pub struct OpenJDK_Upcalls {
    pub stop_all_mutators: extern "C" fn(
        tls: VMWorkerThread,
        scan_mutators_in_safepoint: bool,
        closure: MutatorClosure,
    ),
    pub resume_mutators: extern "C" fn(tls: VMWorkerThread),
    pub spawn_gc_thread: extern "C" fn(tls: VMThread, kind: libc::c_int, ctx: *mut libc::c_void),
    pub block_for_gc: extern "C" fn(),
    pub out_of_memory: extern "C" fn(tls: VMThread, err_kind: AllocationError),
    pub get_next_mutator: extern "C" fn() -> *mut Mutator<OpenJDK>,
    pub reset_mutator_iterator: extern "C" fn(),
    pub scan_object: extern "C" fn(trace: *mut c_void, object: ObjectReference, tls: OpaquePointer),
    pub dump_object: extern "C" fn(object: ObjectReference),
    pub get_object_size: extern "C" fn(object: ObjectReference) -> usize,
    pub get_mmtk_mutator: extern "C" fn(tls: VMMutatorThread) -> *mut Mutator<OpenJDK>,
    pub is_mutator: extern "C" fn(tls: VMThread) -> bool,
    pub harness_begin: extern "C" fn(),
    pub harness_end: extern "C" fn(),
    pub compute_klass_mem_layout_checksum: extern "C" fn() -> usize,
    pub offset_of_static_fields: extern "C" fn() -> i32,
    pub static_oop_field_count_offset: extern "C" fn() -> i32,
    pub referent_offset: extern "C" fn() -> i32,
    pub discovered_offset: extern "C" fn() -> i32,
    pub dump_object_string: extern "C" fn(object: ObjectReference) -> *const c_char,
    pub scan_all_thread_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_thread_roots: extern "C" fn(closure: EdgesClosure, tls: VMMutatorThread),
    pub scan_code_cache_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_class_loader_data_graph_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_oop_storage_set_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_weak_processor_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_vm_thread_roots: extern "C" fn(closure: EdgesClosure),
    pub number_of_mutators: extern "C" fn() -> usize,
    pub schedule_finalizer: extern "C" fn(),
    pub prepare_for_roots_re_scanning: extern "C" fn(),
    pub enqueue_references: extern "C" fn(objects: *const ObjectReference, len: usize),
}

pub static mut UPCALLS: *const OpenJDK_Upcalls = null_mut();

#[no_mangle]
pub static GLOBAL_SIDE_METADATA_BASE_ADDRESS: uintptr_t =
    mmtk::util::metadata::side_metadata::GLOBAL_SIDE_METADATA_BASE_ADDRESS.as_usize();

#[no_mangle]
pub static GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS: uintptr_t =
    mmtk::util::metadata::side_metadata::GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS.as_usize();

#[no_mangle]
pub static GLOBAL_ALLOC_BIT_ADDRESS: uintptr_t =
    mmtk::util::metadata::side_metadata::ALLOC_SIDE_METADATA_ADDR.as_usize();

#[no_mangle]
pub static FREE_LIST_ALLOCATOR_SIZE: uintptr_t =
    std::mem::size_of::<mmtk::util::alloc::FreeListAllocator<OpenJDK>>();

#[derive(Default)]
pub struct OpenJDK;

/// The type of edges in OpenJDK.
///
/// TODO: We currently make it an alias to Address to make the change minimal.
/// If we support CompressedOOPs, we should define an enum type to support both
/// compressed and uncompressed OOPs.
pub type OpenJDKEdge = Address;

impl VMBinding for OpenJDK {
    type VMObjectModel = object_model::VMObjectModel;
    type VMScanning = scanning::VMScanning;
    type VMCollection = collection::VMCollection;
    type VMActivePlan = active_plan::VMActivePlan;
    type VMReferenceGlue = reference_glue::VMReferenceGlue;

    type VMEdge = OpenJDKEdge;
    type VMMemorySlice = Range<Address>;

    const MIN_ALIGNMENT: usize = 8;
    const MAX_ALIGNMENT: usize = 8;
    const USE_ALLOCATION_OFFSET: bool = false;
}

use std::sync::atomic::AtomicBool;
use std::sync::atomic::Ordering;

pub static MMTK_INITIALIZED: AtomicBool = AtomicBool::new(false);

lazy_static! {
    pub static ref BUILDER: Mutex<MMTKBuilder> = Mutex::new(MMTKBuilder::new());
    pub static ref SINGLETON: MMTK<OpenJDK> = {
        let builder = BUILDER.lock().unwrap();
        assert!(!MMTK_INITIALIZED.load(Ordering::Relaxed));
        let ret = mmtk::memory_manager::mmtk_init(&builder);
        MMTK_INITIALIZED.store(true, std::sync::atomic::Ordering::SeqCst);
        *ret
    };
}

#[no_mangle]
pub static MMTK_MARK_COMPACT_HEADER_RESERVED_IN_BYTES: usize =
    mmtk::util::alloc::MarkCompactAllocator::<OpenJDK>::HEADER_RESERVED_IN_BYTES;

lazy_static! {
    /// A global storage for all the cached CodeCache root pointers
    static ref CODE_CACHE_ROOTS: Mutex<HashMap<Address, Vec<Address>>> = Mutex::new(HashMap::new());
}

/// A counter tracking the total size of the `CODE_CACHE_ROOTS`.
static CODE_CACHE_ROOTS_SIZE: AtomicUsize = AtomicUsize::new(0);
