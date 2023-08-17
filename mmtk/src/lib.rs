#[macro_use]
extern crate lazy_static;

use std::collections::HashMap;
use std::ptr::null_mut;
use std::sync::atomic::AtomicUsize;
use std::sync::Mutex;

use libc::{c_char, c_void, uintptr_t};
use mmtk::util::alloc::AllocationError;
use mmtk::util::constants::{
    BYTES_IN_ADDRESS, BYTES_IN_INT, LOG_BYTES_IN_ADDRESS, LOG_BYTES_IN_GBYTE, LOG_BYTES_IN_INT,
};
use mmtk::util::heap::vm_layout_constants::VMLayoutConstants;
use mmtk::util::{conversions, opaque_pointer::*};
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::edge_shape::{Edge, MemorySlice};
use mmtk::vm::VMBinding;
use mmtk::{MMTKBuilder, Mutator, MMTK};

macro_rules! with_singleton {
    (|$x: ident| $($expr:tt)*) => {
        if crate::use_compressed_oops() {
            let $x: &'static mmtk::MMTK<crate::OpenJDK<true>> = &*crate::SINGLETON_COMPRESSED;
            $($expr)*
        } else {
            let $x: &'static mmtk::MMTK<crate::OpenJDK<false>> = &*crate::SINGLETON_UNCOMPRESSED;
            $($expr)*
        }
    };
}

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
    pub compressed_klass_base: extern "C" fn() -> Address,
    pub compressed_klass_shift: extern "C" fn() -> usize,
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

#[no_mangle]
pub static DISABLE_ALLOCATION_FAST_PATH: i32 =
    (cfg!(feature = "no_fast_alloc") || cfg!(feature = "object_size_distribution")) as _;

#[no_mangle]
pub static IMMIX_ALLOCATOR_SIZE: uintptr_t =
    std::mem::size_of::<mmtk::util::alloc::ImmixAllocator<OpenJDK<false>>>();

#[no_mangle]
pub static mut CONCURRENT_MARKING_ACTIVE: u8 = 0;

#[no_mangle]
pub static mut HEAP_START: Address = Address::ZERO;

#[no_mangle]
pub static mut HEAP_END: Address = Address::ZERO;

static mut USE_COMPRESSED_OOPS: bool = false;
static mut LOG_BYTES_IN_FIELD: usize = LOG_BYTES_IN_ADDRESS as _;
static mut BYTES_IN_FIELD: usize = BYTES_IN_ADDRESS as _;

fn init_compressed_oop_constants() {
    unsafe {
        USE_COMPRESSED_OOPS = true;
        LOG_BYTES_IN_FIELD = LOG_BYTES_IN_INT as _;
        BYTES_IN_FIELD = BYTES_IN_INT as _;
    }
}

fn use_compressed_oops() -> bool {
    unsafe { USE_COMPRESSED_OOPS }
}

fn log_bytes_in_field() -> usize {
    unsafe { LOG_BYTES_IN_FIELD }
}

fn bytes_in_field() -> usize {
    unsafe { BYTES_IN_FIELD }
}

static mut BASE: Address = Address::ZERO;
static mut SHIFT: usize = 0;

fn compress(o: ObjectReference) -> u32 {
    if o.is_null() {
        0u32
    } else {
        unsafe { ((o.to_raw_address() - BASE) >> SHIFT) as u32 }
    }
}

fn decompress(v: u32) -> ObjectReference {
    if v == 0 {
        ObjectReference::NULL
    } else {
        unsafe { ObjectReference::from_raw_address(BASE + ((v as usize) << SHIFT)) }
    }
}

fn initialize_compressed_oops() {
    let heap_end = mmtk::memory_manager::last_heap_address().as_usize();
    if heap_end <= (4usize << 30) {
        unsafe {
            BASE = Address::ZERO;
            SHIFT = 0;
        }
    } else if heap_end <= (32usize << 30) {
        unsafe {
            BASE = Address::ZERO;
            SHIFT = 3;
        }
    } else {
        unsafe {
            BASE = mmtk::memory_manager::starting_heap_address() - 4096;
            SHIFT = 3;
        }
    }
}

#[derive(Default)]
pub struct OpenJDK<const COMPRESSED: bool>;

/// The type of edges in OpenJDK.
#[derive(Debug, Clone, Copy, Hash, PartialEq, Eq)]
#[repr(transparent)]
pub struct OpenJDKEdge<const COMPRESSED: bool>(pub Address);

impl<const COMPRESSED: bool> OpenJDKEdge<COMPRESSED> {
    const MASK: usize = 1usize << 63;

    const fn is_compressed(&self) -> bool {
        self.0.as_usize() & Self::MASK == 0
    }

    const fn untagged_address(&self) -> Address {
        unsafe { Address::from_usize(self.0.as_usize() << 1 >> 1) }
    }

    pub fn to_address(&self) -> Address {
        self.untagged_address()
    }

    pub fn from_address(a: Address) -> Self {
        Self(a)
    }
}

impl<const COMPRESSED: bool> Edge for OpenJDKEdge<COMPRESSED> {
    /// Load object reference from the edge.
    fn load(&self) -> ObjectReference {
        if COMPRESSED {
            let slot = self.untagged_address();
            if self.is_compressed() {
                decompress(unsafe { slot.load::<u32>() })
            } else {
                unsafe { slot.load::<ObjectReference>() }
            }
        } else {
            unsafe { self.0.load::<ObjectReference>() }
        }
    }

    /// Store the object reference `object` into the edge.
    fn store(&self, object: ObjectReference) {
        if COMPRESSED {
            let slot = self.untagged_address();
            if self.is_compressed() {
                unsafe { slot.store(compress(object)) }
            } else {
                unsafe { slot.store(object) }
            }
        } else {
            unsafe { self.0.store(object) }
        }
    }
}

#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
pub struct OpenJDKEdgeRange<const COMPRESSED: bool> {
    pub start: OpenJDKEdge<COMPRESSED>,
    pub end: OpenJDKEdge<COMPRESSED>,
}

/// Iterate edges within `Range<Address>`.
pub struct AddressRangeIterator<const COMPRESSED: bool> {
    cursor: Address,
    limit: Address,
    width: usize,
}

impl<const COMPRESSED: bool> Iterator for AddressRangeIterator<COMPRESSED> {
    type Item = OpenJDKEdge<COMPRESSED>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.cursor >= self.limit {
            None
        } else {
            let edge = self.cursor;
            self.cursor += self.width;
            Some(OpenJDKEdge(edge))
        }
    }
}

pub struct ChunkIterator<const COMPRESSED: bool> {
    cursor: Address,
    limit: Address,
    step: usize,
}

impl<const COMPRESSED: bool> Iterator for ChunkIterator<COMPRESSED> {
    type Item = OpenJDKEdgeRange<COMPRESSED>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.cursor >= self.limit {
            None
        } else {
            let start = self.cursor;
            let mut end = start + self.step;
            if end > self.limit {
                end = self.limit;
            }
            self.cursor = end;
            Some(OpenJDKEdgeRange {
                start: OpenJDKEdge(start),
                end: OpenJDKEdge(end),
            })
        }
    }
}

impl<const COMPRESSED: bool> MemorySlice for OpenJDKEdgeRange<COMPRESSED> {
    type Edge = OpenJDKEdge<COMPRESSED>;
    type EdgeIterator = AddressRangeIterator<COMPRESSED>;

    fn iter_edges(&self) -> Self::EdgeIterator {
        AddressRangeIterator {
            cursor: self.start.0,
            limit: self.end.0,
            width: crate::bytes_in_field(),
        }
    }

    fn start(&self) -> Address {
        self.start.0
    }

    fn bytes(&self) -> usize {
        self.end.0 - self.start.0
    }

    fn copy(src: &Self, tgt: &Self) {
        debug_assert_eq!(src.bytes(), tgt.bytes());
        debug_assert_eq!(
            src.bytes() & ((1 << LOG_BYTES_IN_ADDRESS) - 1),
            0,
            "bytes are not a multiple of words"
        );
        // Raw memory copy
        if crate::use_compressed_oops() {
            unsafe {
                let words = tgt.bytes() >> LOG_BYTES_IN_INT;
                let src = src.start().to_ptr::<u32>();
                let tgt = tgt.start().to_mut_ptr::<u32>();
                std::ptr::copy(src, tgt, words)
            }
        } else {
            unsafe {
                let words = tgt.bytes() >> LOG_BYTES_IN_ADDRESS;
                let src = src.start().to_ptr::<usize>();
                let tgt = tgt.start().to_mut_ptr::<usize>();
                std::ptr::copy(src, tgt, words)
            }
        }
    }

    fn object(&self) -> Option<ObjectReference> {
        None
    }
}

impl<const COMPRESSED: bool> VMBinding for OpenJDK<COMPRESSED> {
    type VMObjectModel = object_model::VMObjectModel;
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
    pub static ref BUILDER: Mutex<MMTKBuilder> = Mutex::new(MMTKBuilder::new());
    pub static ref SINGLETON_COMPRESSED: MMTK<OpenJDK<true>> = {
        let mut builder = BUILDER.lock().unwrap();
        assert!(use_compressed_oops());
        builder.set_option("use_35bit_address_space", "true");
        assert!(!MMTK_INITIALIZED.load(Ordering::Relaxed));
        set_custom_vm_layout_constants(builder.options.gc_trigger.max_heap_size());
        let ret = mmtk::memory_manager::mmtk_init(&builder);
        MMTK_INITIALIZED.store(true, std::sync::atomic::Ordering::SeqCst);
        initialize_compressed_oops();
        unsafe {
            HEAP_START = mmtk::memory_manager::starting_heap_address();
            HEAP_END = mmtk::memory_manager::last_heap_address();
        }
        *ret
    };
    pub static ref SINGLETON_UNCOMPRESSED: MMTK<OpenJDK<false>> = {
        let builder = BUILDER.lock().unwrap();
        assert!(!MMTK_INITIALIZED.load(Ordering::Relaxed));
        let ret = mmtk::memory_manager::mmtk_init(&builder);
        MMTK_INITIALIZED.store(true, std::sync::atomic::Ordering::SeqCst);
        unsafe {
            HEAP_START = mmtk::memory_manager::starting_heap_address();
            HEAP_END = mmtk::memory_manager::last_heap_address();
        }
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

fn set_custom_vm_layout_constants(max_heap_size: usize) {
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
    let constants = VMLayoutConstants {
        log_address_space: 35,
        heap_start: conversions::chunk_align_down(unsafe { Address::from_usize(start) }),
        heap_end: conversions::chunk_align_up(unsafe { Address::from_usize(end) }),
        log_space_extent: 31,
        force_use_contiguous_spaces: false,
    };
    VMLayoutConstants::set_custom_vm_layout_constants(constants);
}
