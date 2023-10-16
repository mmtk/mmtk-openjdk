#[macro_use]
extern crate lazy_static;

use std::collections::HashMap;
use std::ptr::null_mut;
use std::sync::atomic::AtomicUsize;
use std::sync::Mutex;

pub use edges::use_compressed_oops;
use edges::{OpenJDKEdge, OpenJDKEdgeRange};
use libc::{c_char, c_void, uintptr_t};
use mmtk::util::alloc::AllocationError;
use mmtk::util::constants::LOG_BYTES_IN_GBYTE;
use mmtk::util::heap::vm_layout::VMLayout;
use mmtk::util::{conversions, opaque_pointer::*};
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::edge_shape::Edge;
use mmtk::vm::VMBinding;
use mmtk::{MMTKBuilder, Mutator, MMTK};

mod abi;
pub mod active_plan;
pub mod api;
mod build_info;
pub mod collection;
mod edges;
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
    pub func: extern "C" fn(mutator: *mut libc::c_void, data: *mut libc::c_void),
    pub data: *mut libc::c_void,
}

impl MutatorClosure {
    fn from_rust_closure<F, const COMPRESSED: bool>(callback: &mut F) -> Self
    where
        F: FnMut(&'static mut Mutator<OpenJDK<COMPRESSED>>),
    {
        Self {
            func: Self::call_rust_closure::<F, COMPRESSED>,
            data: callback as *mut F as *mut libc::c_void,
        }
    }

    extern "C" fn call_rust_closure<F, const COMPRESSED: bool>(
        mutator: *mut libc::c_void,
        callback_ptr: *mut libc::c_void,
    ) where
        F: FnMut(&'static mut Mutator<OpenJDK<COMPRESSED>>),
    {
        let mutator = mutator as *mut Mutator<OpenJDK<COMPRESSED>>;
        let callback: &mut F = unsafe { &mut *(callback_ptr as *mut F) };
        callback(unsafe { &mut *mutator });
    }
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
    pub stop_all_mutators: extern "C" fn(tls: VMWorkerThread, closure: MutatorClosure),
    pub resume_mutators: extern "C" fn(tls: VMWorkerThread),
    pub spawn_gc_thread: extern "C" fn(tls: VMThread, kind: libc::c_int, ctx: *mut libc::c_void),
    pub block_for_gc: extern "C" fn(),
    pub out_of_memory: extern "C" fn(tls: VMThread, err_kind: AllocationError),
    pub get_mutators: extern "C" fn(closure: MutatorClosure),
    pub scan_object: extern "C" fn(trace: *mut c_void, object: ObjectReference, tls: OpaquePointer),
    pub dump_object: extern "C" fn(object: ObjectReference),
    pub get_object_size: extern "C" fn(object: ObjectReference) -> usize,
    pub get_mmtk_mutator: extern "C" fn(tls: VMMutatorThread) -> *mut libc::c_void,
    pub is_mutator: extern "C" fn(tls: VMThread) -> bool,
    pub harness_begin: extern "C" fn(),
    pub harness_end: extern "C" fn(),
    pub compute_klass_mem_layout_checksum: extern "C" fn() -> usize,
    pub offset_of_static_fields: extern "C" fn() -> i32,
    pub static_oop_field_count_offset: extern "C" fn() -> i32,
    pub referent_offset: extern "C" fn() -> i32,
    pub discovered_offset: extern "C" fn() -> i32,
    pub dump_object_string: extern "C" fn(object: ObjectReference) -> *const c_char,
    pub scan_roots_in_all_mutator_threads: extern "C" fn(closure: EdgesClosure),
    pub scan_roots_in_mutator_thread: extern "C" fn(closure: EdgesClosure, tls: VMMutatorThread),
    pub scan_universe_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_jni_handle_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_object_synchronizer_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_management_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_jvmti_export_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_aot_loader_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_system_dictionary_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_code_cache_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_string_table_roots: extern "C" fn(closure: EdgesClosure),
    pub scan_class_loader_data_graph_roots: extern "C" fn(closure: EdgesClosure),
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
pub static VO_BIT_ADDRESS: uintptr_t =
    mmtk::util::metadata::side_metadata::VO_BIT_SIDE_METADATA_ADDR.as_usize();

#[no_mangle]
pub static FREE_LIST_ALLOCATOR_SIZE: uintptr_t =
    std::mem::size_of::<mmtk::util::alloc::FreeListAllocator<OpenJDK<false>>>();

#[derive(Default)]
pub struct OpenJDK<const COMPRESSED: bool>;

impl<const COMPRESSED: bool> VMBinding for OpenJDK<COMPRESSED> {
    type VMObjectModel = object_model::VMObjectModel<COMPRESSED>;
    type VMScanning = scanning::VMScanning;
    type VMCollection = collection::VMCollection;
    type VMActivePlan = active_plan::VMActivePlan;
    type VMReferenceGlue = reference_glue::VMReferenceGlue;

    type VMEdge = OpenJDKEdge<COMPRESSED>;
    type VMMemorySlice = OpenJDKEdgeRange<COMPRESSED>;

    const MIN_ALIGNMENT: usize = 8;
    const MAX_ALIGNMENT: usize = 8;
    const USE_ALLOCATION_OFFSET: bool = false;
}

use std::sync::atomic::AtomicBool;
use std::sync::atomic::Ordering;

pub static MMTK_INITIALIZED: AtomicBool = AtomicBool::new(false);

lazy_static! {
    pub static ref BUILDER: Mutex<MMTKBuilder> = Mutex::new(MMTKBuilder::new_no_env_vars());
    pub static ref SINGLETON_COMPRESSED: MMTK<OpenJDK<true>> = {
        assert!(use_compressed_oops());
        let mut builder = BUILDER.lock().unwrap();
        assert!(!MMTK_INITIALIZED.load(Ordering::Relaxed));
        set_compressed_pointer_vm_layout(&mut builder);
        let ret = mmtk::memory_manager::mmtk_init(&builder);
        MMTK_INITIALIZED.store(true, std::sync::atomic::Ordering::SeqCst);
        edges::initialize_compressed_oops_base_and_shift();
        *ret
    };
    pub static ref SINGLETON_UNCOMPRESSED: MMTK<OpenJDK<false>> = {
        assert!(!use_compressed_oops());
        let builder = BUILDER.lock().unwrap();
        assert!(!MMTK_INITIALIZED.load(Ordering::Relaxed));
        let ret = mmtk::memory_manager::mmtk_init(&builder);
        MMTK_INITIALIZED.store(true, std::sync::atomic::Ordering::SeqCst);
        *ret
    };
}

fn singleton<const COMPRESSED: bool>() -> &'static MMTK<OpenJDK<COMPRESSED>> {
    if COMPRESSED {
        unsafe {
            &*(&*SINGLETON_COMPRESSED as *const MMTK<OpenJDK<true>>
                as *const MMTK<OpenJDK<COMPRESSED>>)
        }
    } else {
        unsafe {
            &*(&*SINGLETON_UNCOMPRESSED as *const MMTK<OpenJDK<false>>
                as *const MMTK<OpenJDK<COMPRESSED>>)
        }
    }
}

#[no_mangle]
pub static MMTK_MARK_COMPACT_HEADER_RESERVED_IN_BYTES: usize =
    mmtk::util::alloc::MarkCompactAllocator::<OpenJDK<false>>::HEADER_RESERVED_IN_BYTES;

lazy_static! {
    /// A global storage for all the cached CodeCache root pointers
    static ref CODE_CACHE_ROOTS: Mutex<HashMap<Address, Vec<Address>>> = Mutex::new(HashMap::new());
}

/// A counter tracking the total size of the `CODE_CACHE_ROOTS`.
static CODE_CACHE_ROOTS_SIZE: AtomicUsize = AtomicUsize::new(0);

fn set_compressed_pointer_vm_layout(builder: &mut MMTKBuilder) {
    let max_heap_size = builder.options.gc_trigger.max_heap_size();
    assert!(
        max_heap_size <= (32usize << LOG_BYTES_IN_GBYTE),
        "Heap size is larger than 32 GB"
    );
    let start = 0x4000_0000;
    let end = match start + max_heap_size {
        end if end <= (4usize << 30) => 4usize << 30,
        end if end <= (32usize << 30) => 32usize << 30,
        _ => 0x4000_0000 + (32usize << 30),
    };
    let constants = VMLayout {
        log_address_space: 35,
        heap_start: conversions::chunk_align_down(unsafe { Address::from_usize(start) }),
        heap_end: conversions::chunk_align_up(unsafe { Address::from_usize(end) }),
        log_space_extent: 31,
        force_use_contiguous_spaces: false,
    };
    builder.set_vm_layout(constants);
}
