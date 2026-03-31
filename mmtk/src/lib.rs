#[macro_use]
extern crate lazy_static;
extern crate atomic;
extern crate once_cell;
#[macro_use]
extern crate probe;
extern crate spin;

use std::collections::HashMap;
use std::ptr::null_mut;
use std::sync::Mutex;

use libc::{c_char, c_void, uintptr_t};
use mmtk::plan::lxr::LXR;
use mmtk::util::alloc::AllocationError;
use mmtk::util::constants::LOG_BYTES_IN_GBYTE;
use mmtk::util::heap::vm_layout::{VMLayout, BYTES_IN_CHUNK};
use mmtk::util::{conversions, opaque_pointer::*};
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::slot::Slot;
use mmtk::vm::VMBinding;
use mmtk::{MMTKBuilder, Mutator, MMTK};
pub use slots::use_compressed_oops;
use slots::{OpenJDKSlot, OpenJDKSlotRange};

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
mod slots;
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

/// A closure for reporting root slots.  The C++ code should pass `data` back as the last argument.
#[repr(C)]
pub struct SlotsClosure {
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
        closure: MutatorClosure,
        current_gc_should_unload_classes: bool,
    ),
    pub resume_mutators: extern "C" fn(tls: VMWorkerThread),
    pub spawn_gc_thread: extern "C" fn(tls: VMThread, kind: libc::c_int, ctx: *mut libc::c_void),
    pub block_for_gc: extern "C" fn(),
    pub out_of_memory: extern "C" fn(tls: VMThread, err_kind: AllocationError),
    pub get_mutators: extern "C" fn(closure: MutatorClosure),
    pub scan_object: extern "C" fn(
        trace: *mut c_void,
        object: ObjectReference,
        tls: OpaquePointer,
        follow_clds: bool,
        claim_clds: bool,
    ),
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
    pub scan_roots_in_all_mutator_threads: extern "C" fn(closure: SlotsClosure),
    pub scan_roots_in_mutator_thread: extern "C" fn(closure: SlotsClosure, tls: VMMutatorThread),
    pub scan_multiple_thread_roots:
        extern "C" fn(closure: SlotsClosure, ptr: OpaquePointer, len: usize),
    pub scan_code_cache_roots: extern "C" fn(closure: SlotsClosure),
    pub scan_class_loader_data_graph_roots:
        extern "C" fn(closure: SlotsClosure, weak_closure: SlotsClosure, scan_weak: bool),
    pub scan_oop_storage_set_roots: extern "C" fn(closure: SlotsClosure),
    pub scan_weak_processor_roots: extern "C" fn(closure: SlotsClosure),
    // pub scan_string_table_roots: extern "C" fn(closure: SlotsClosure, rc_non_stuck_objs_only: bool),
    pub scan_vm_thread_roots: extern "C" fn(closure: SlotsClosure),
    pub number_of_mutators: extern "C" fn() -> usize,
    pub schedule_finalizer: extern "C" fn(),
    pub prepare_for_roots_re_scanning: extern "C" fn(),
    pub update_weak_processor: extern "C" fn(lxr: bool),
    pub enqueue_references: extern "C" fn(objects: *const ObjectReference, len: usize),
    pub swap_reference_pending_list: extern "C" fn(objects: ObjectReference) -> ObjectReference,
    pub java_lang_class_klass_offset_in_bytes: extern "C" fn() -> usize,
    pub java_lang_classloader_loader_data_offset: extern "C" fn() -> usize,
    pub fix_oop_relocations: extern "C" fn(lxr: bool, forward_only: bool, nmethods: *mut libc::c_void, len: usize),
    // pub nmethod_fix_relocation: extern "C" fn(Address),
    pub clear_claimed_marks: extern "C" fn(),
    pub unload_classes: extern "C" fn(),
    pub gc_epilogue: extern "C" fn(),
    pub oops_do_marking_epilogue: extern "C" fn(),
}

lazy_static! {
    pub static ref JAVA_LANG_CLASS_KLASS_OFFSET_IN_BYTES: usize =
        unsafe { ((*UPCALLS).java_lang_class_klass_offset_in_bytes)() };
    pub static ref JAVA_LANG_CLASSLOADER_LOADER_DATA_OFFSET: usize =
        unsafe { ((*UPCALLS).java_lang_classloader_loader_data_offset)() };
}

pub static mut UPCALLS: *const OpenJDK_Upcalls = null_mut();

#[no_mangle]
pub static GLOBAL_SIDE_METADATA_BASE_ADDRESS: uintptr_t =
    mmtk::util::metadata::side_metadata::GLOBAL_SIDE_METADATA_BASE_ADDRESS.as_usize();

#[no_mangle]
pub static GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS: uintptr_t =
    mmtk::util::metadata::side_metadata::GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS.as_usize();

#[no_mangle]
pub static FIELD_UNLOG_BITS_BASE_ADDRESS: uintptr_t =
    mmtk::util::metadata::side_metadata::GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS.as_usize();

#[no_mangle]
pub static FIELD_UNLOG_BITS_BASE_ADDRESS_COMPRESSED: uintptr_t =
    mmtk::util::metadata::side_metadata::GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS.as_usize();

#[no_mangle]
pub static RC_TABLE_BASE_ADDRESS: uintptr_t =
    mmtk::util::rc::RC_TABLE.get_absolute_offset().as_usize();

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
pub static FIELD_BARRIER_NO_EAGER_BRANCH: u8 = cfg!(feature = "field_barrier_no_eager_branch") as _;
#[no_mangle]
pub static FIELD_BARRIER_NO_ARRAYCOPY: u8 = cfg!(feature = "field_barrier_no_arraycopy") as _;
#[no_mangle]
pub static FIELD_BARRIER_NO_ARRAYCOPY_SLOW: u8 =
    cfg!(feature = "field_barrier_no_arraycopy_slow") as _;
#[no_mangle]
pub static FIELD_BARRIER_NO_C2_SLOW_CALL: u8 = cfg!(feature = "field_barrier_no_c2_slow_call") as _;
#[no_mangle]
pub static FIELD_BARRIER_NO_C2_RUST_CALL: u8 = cfg!(feature = "field_barrier_no_c2_rust_call") as _;

#[no_mangle]
pub static mut CONCURRENT_MARKING_ACTIVE: u8 = 0;

#[no_mangle]
pub static mut RC_ENABLED: u8 = 0;

#[no_mangle]
pub static mut REQUIRES_WEAK_HANDLE_BARRIER: u8 = 0;

#[no_mangle]
pub static mut CLASS_UNLOADING_ENABLED: u8 = 0;

#[derive(Default)]
pub struct OpenJDK<const COMPRESSED: bool>;

impl<const COMPRESSED: bool> VMBinding for OpenJDK<COMPRESSED> {
    type VMObjectModel = object_model::VMObjectModel<COMPRESSED>;
    type VMScanning = scanning::VMScanning;
    type VMCollection = collection::VMCollection;
    type VMActivePlan = active_plan::VMActivePlan;
    type VMReferenceGlue = reference_glue::VMReferenceGlue;

    type VMSlot = OpenJDKSlot<COMPRESSED>;
    type VMMemorySlice = OpenJDKSlotRange<COMPRESSED>;

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
        slots::initialize_compressed_oops_base_and_shift();
        unsafe {
            RC_ENABLED = ret
                .get_plan()
                .downcast_ref::<LXR<OpenJDK<true>>>()
                .is_some() as _;
            REQUIRES_WEAK_HANDLE_BARRIER = RC_ENABLED;
        }
        *ret
    };
    pub static ref SINGLETON_UNCOMPRESSED: MMTK<OpenJDK<false>> = {
        assert!(!use_compressed_oops());
        let mut builder = BUILDER.lock().unwrap();
        assert!(!MMTK_INITIALIZED.load(Ordering::Relaxed));
        if cfg!(feature = "discontig_space") {
            set_no_compressed_pointer_discontig_vm_layout(&mut builder);
        }
        let ret = mmtk::memory_manager::mmtk_init(&builder);
        MMTK_INITIALIZED.store(true, std::sync::atomic::Ordering::SeqCst);
        unsafe {
            RC_ENABLED = ret
                .get_plan()
                .downcast_ref::<LXR<OpenJDK<false>>>()
                .is_some() as _;
            REQUIRES_WEAK_HANDLE_BARRIER = RC_ENABLED;
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
    static ref NURSERY_CODE_CACHE_ROOTS: Mutex<HashMap<Address, Vec<Address>>> = Mutex::new(HashMap::new());
    static ref MATURE_CODE_CACHE_ROOTS: Mutex<HashMap<Address, Vec<Address>>> = Mutex::new(HashMap::new());
    static ref NURSERY_WEAK_HANDLE_ROOTS: Mutex<Vec<Address>> = Mutex::new(Vec::new());
}

lazy_static! {
    static ref OBJ_COUNT: Mutex<HashMap<usize, (usize, usize)>> = Mutex::new(HashMap::new());
}

fn record_alloc(size: usize) {
    assert!(cfg!(feature = "object_size_distribution"));
    let mut counts = OBJ_COUNT.lock().unwrap();
    counts
        .entry(size.next_power_of_two())
        .and_modify(|x| {
            x.0 += 1;
            x.1 += size;
        })
        .or_insert((1, size));
}

extern "C" fn dump_and_reset_obj_dist() {
    assert!(cfg!(feature = "object_size_distribution"));
    mmtk::dump_and_reset_obj_dist("Dynamic", &mut OBJ_COUNT.lock().unwrap());
}

fn set_compressed_pointer_vm_layout(builder: &mut MMTKBuilder) {
    let max_heap_size = builder.options.gc_trigger.max_heap_size();
    assert!(
        max_heap_size <= (32usize << LOG_BYTES_IN_GBYTE),
        "Heap size is larger than 32 GB"
    );
    let rounded_heap_size = (max_heap_size + (BYTES_IN_CHUNK - 1)) & !(BYTES_IN_CHUNK - 1);
    let mut start: usize = 0x4000_0000; // block lowest 1G
    let (end, mut small_chunk_space_size) = if cfg!(feature = "force_narrow_oop_mode") {
        assert!(rounded_heap_size <= (2 << 30));
        let heap = rounded_heap_size;
        if cfg!(feature = "narrow_oop_mode_32bit") {
            let end = 4usize << 30;
            let small_space = 2 << 30;
            (end, small_space)
        } else if cfg!(feature = "narrow_oop_mode_shift") {
            let end = 32usize << 30;
            let small_space = usize::min(heap * 3 / 2, 29 << 30);
            (end, small_space)
        } else if cfg!(feature = "narrow_oop_mode_base") {
            // start = 0x200_0000_0000;
            // let end = start + ((4usize << 30) - BYTES_IN_CHUNK);
            // let small_space = 2 << 30;
            // (end, small_space)
            unreachable!()
        } else if cfg!(feature = "narrow_oop_mode_base_and_shift") {
            start = 0x200_0000_0000;
            let end = start + 0x8_0000_0000 - BYTES_IN_CHUNK;
            let small_space = usize::min(heap * 3 / 2, 30 << 30);
            (end, small_space)
        } else {
            unreachable!()
        }
    } else {
        match rounded_heap_size {
            // heap <= 2G; virtual = 3G; max-small-space=2G; min-small-space=1.5G
            heap if heap <= 2 << 30 => {
                let end = 4usize << 30;
                let small_space = 2 << 30;
                (end, small_space)
            }
            // heap <= 29G; virtual = 31G; max-small-space=29G;
            heap if heap <= 29 << 30 => {
                let end = 32usize << 30;
                let small_space = usize::min(heap * 3 / 2, 29 << 30);
                (end, small_space)
            }
            // heap > 29G; virtual = 32G - 1chunk; max-small-space=30G; start=0x200_0000_0000
            heap => {
                // A workaround to avoid address conflict with the OpenJDK
                // MetaSpace, which may start from 0x8_0000_0000
                start = 0x200_0000_0000;
                let end = start + 0x8_0000_0000 - BYTES_IN_CHUNK;
                let small_space = usize::min(heap * 3 / 2, 30 << 30);
                (end, small_space)
            }
        }
    };
    small_chunk_space_size =
        (small_chunk_space_size + (BYTES_IN_CHUNK - 1)) & !(BYTES_IN_CHUNK - 1);
    let constants = VMLayout {
        log_address_space: 35,
        heap_start: conversions::chunk_align_down(unsafe { Address::from_usize(start) }),
        heap_end: conversions::chunk_align_up(unsafe { Address::from_usize(end) }),
        log_space_extent: 31,
        force_use_contiguous_spaces: false,
        small_chunk_space_size: Some(small_chunk_space_size),
    };
    builder.set_vm_layout(constants);
}

fn set_no_compressed_pointer_discontig_vm_layout(builder: &mut MMTKBuilder) {
    let max_heap_size = builder.options.gc_trigger.max_heap_size();
    assert!(
        max_heap_size <= (32usize << LOG_BYTES_IN_GBYTE),
        "Heap size is larger than 32 GB"
    );
    let rounded_heap_size = (max_heap_size + (BYTES_IN_CHUNK - 1)) & !(BYTES_IN_CHUNK - 1);
    let mut start: usize = 0x4000_0000; // block lowest 1G
    let (end, mut small_chunk_space_size) = match rounded_heap_size {
        // heap <= 2G; virtual = 3G; max-small-space=2G; min-small-space=1.5G
        heap if heap <= 2 << 30 => {
            let end = 4usize << 30;
            let small_space = 2 << 30;
            (end, small_space)
        }
        // heap <= 29G; virtual = 31G; max-small-space=29G;
        heap if heap <= 29 << 30 => {
            let end = 32usize << 30;
            let small_space = usize::min(heap * 3 / 2, 29 << 30);
            (end, small_space)
        }
        // heap > 29G; virtual = 32G - 1chunk; max-small-space=30G; start=0x200_0000_0000
        heap => {
            // A workaround to avoid address conflict with the OpenJDK
            // MetaSpace, which may start from 0x8_0000_0000
            start = 0x200_0000_0000;
            let end = start + 0x8_0000_0000 - BYTES_IN_CHUNK;
            let small_space = usize::min(heap * 3 / 2, 30 << 30);
            (end, small_space)
        }
    };
    small_chunk_space_size =
        (small_chunk_space_size + (BYTES_IN_CHUNK - 1)) & !(BYTES_IN_CHUNK - 1);
    let constants = VMLayout {
        log_address_space: 35,
        heap_start: conversions::chunk_align_down(unsafe { Address::from_usize(start) }),
        heap_end: conversions::chunk_align_up(unsafe { Address::from_usize(end) }),
        log_space_extent: 31,
        force_use_contiguous_spaces: false,
        small_chunk_space_size: Some(small_chunk_space_size),
    };
    builder.set_vm_layout(constants);
}
