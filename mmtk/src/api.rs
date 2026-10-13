use crate::slots::OpenJDKSlot;
use crate::OpenJDK;
use crate::OpenJDK_Upcalls;
use crate::BUILDER;
use crate::UPCALLS;
use libc::c_char;
use mmtk::memory_manager;
use mmtk::plan::BarrierSelector;
use mmtk::scheduler::GCWorker;
use mmtk::util::alloc::AllocatorSelector;
use mmtk::util::api_util::NullableObjectReference;
use mmtk::util::opaque_pointer::*;
use mmtk::util::{Address, ObjectReference};
use mmtk::AllocationSemantics;
use mmtk::Mutator;
use mmtk::MutatorContext;
use once_cell::sync;
use std::cell::RefCell;
use std::ffi::{CStr, CString};
use std::sync::atomic::Ordering;

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

macro_rules! with_mutator {
    (|$x: ident| $($expr:tt)*) => {
        if crate::use_compressed_oops() {
            let $x = unsafe { &mut *($x as *mut Mutator<OpenJDK<true>>) };
            $($expr)*
        } else {
            let $x = unsafe { &mut *($x as *mut Mutator<OpenJDK<false>>) };
            $($expr)*
        }
    };
}

// Supported barriers:
static NO_BARRIER: sync::Lazy<CString> = sync::Lazy::new(|| CString::new("NoBarrier").unwrap());
static OBJECT_BARRIER: sync::Lazy<CString> =
    sync::Lazy::new(|| CString::new("ObjectBarrier").unwrap());
static FIELD_LOGGING_BARRIER: sync::Lazy<CString> =
    sync::Lazy::new(|| CString::new("FieldBarrier").unwrap());
static SATB_BARRIER: sync::Lazy<CString> = sync::Lazy::new(|| CString::new("SATBBarrier").unwrap());
static HYBRID_BARRIER: sync::Lazy<CString> =
    sync::Lazy::new(|| CString::new("HybridBarrier").unwrap());

#[no_mangle]
pub extern "C" fn get_mmtk_version() -> *const c_char {
    crate::build_info::MMTK_OPENJDK_FULL_VERSION.as_ptr() as _
}

#[no_mangle]
pub extern "C" fn mmtk_active_barrier() -> *const c_char {
    with_singleton!(|singleton| {
        match singleton.get_plan().constraints().barrier {
            BarrierSelector::NoBarrier => NO_BARRIER.as_ptr(),
            BarrierSelector::ObjectBarrier => OBJECT_BARRIER.as_ptr(),
            BarrierSelector::FieldBarrier => FIELD_LOGGING_BARRIER.as_ptr(),
            BarrierSelector::SATBBarrier => SATB_BARRIER.as_ptr(),
            BarrierSelector::HybridBarrier => HYBRID_BARRIER.as_ptr(),
            // In case we have more barriers in mmtk-core.
            #[allow(unreachable_patterns)]
            _ => unimplemented!(),
        }
    })
}

/// The address of the global flag of the hybrid barrier (a byte that is non-zero while the
/// barrier is active).  Compiled code tests this byte inline.
#[no_mangle]
pub extern "C" fn mmtk_barrier_active_address() -> *mut u8 {
    memory_manager::barrier_active_address().to_mut_ptr()
}

/// Debugging aid for the hybrid barrier: if `MMTK_HYBRID_BARRIER_DEBUG_ALWAYS_ON=1`, set the
/// barrier flag once at start-up and never clear it, so that every barrier path (interpreter, C1,
/// C2, arraycopy, clone, weak loads) calls into MMTk.  mmtk-core drops the buffered objects while
/// no marking is in progress, so this only tests that the paths are reached and do not crash.
fn maybe_force_hybrid_barrier_on() {
    let force = std::env::var("MMTK_HYBRID_BARRIER_DEBUG_ALWAYS_ON").is_ok_and(|v| v == "1");
    if !force {
        return;
    }
    let barrier = with_singleton!(|singleton| singleton.get_plan().constraints().barrier);
    if barrier != BarrierSelector::HybridBarrier {
        log::warn!(
            "MMTK_HYBRID_BARRIER_DEBUG_ALWAYS_ON ignored: the plan does not use HybridBarrier"
        );
        return;
    }
    let flag = memory_manager::barrier_active_address().to_ptr::<std::sync::atomic::AtomicU8>();
    // Safety: the address is that of a static `AtomicU8` in mmtk-core.
    unsafe { &*flag }.store(1, Ordering::SeqCst);
    eprintln!("[mmtk-openjdk] MMTK_HYBRID_BARRIER_DEBUG_ALWAYS_ON=1: the hybrid barrier flag is forced on");
}

#[no_mangle]
pub extern "C" fn mmtk_report_gc_start() {
    with_singleton!(|singleton| mmtk::memory_manager::report_gc_start(singleton));
}

/// # Safety
/// Caller needs to make sure the ptr is a valid vector pointer.
#[no_mangle]
pub unsafe extern "C" fn release_buffer(ptr: *mut Address, length: usize, capacity: usize) {
    let _vec = Vec::<Address>::from_raw_parts(ptr, length, capacity);
}

#[no_mangle]
pub extern "C" fn openjdk_gc_init(calls: *const OpenJDK_Upcalls) {
    unsafe { UPCALLS = calls };
    crate::abi::validate_memory_layouts();

    // We don't really need this, as we can dynamically set plans. However, for compatability of our CI scripts,
    // we allow selecting a plan using feature at build time.
    // We should be able to remove this very soon.
    {
        use mmtk::util::options::PlanSelector;
        let force_plan = if cfg!(feature = "nogc") {
            Some(PlanSelector::NoGC)
        } else if cfg!(feature = "semispace") {
            Some(PlanSelector::SemiSpace)
        } else if cfg!(feature = "gencopy") {
            Some(PlanSelector::GenCopy)
        } else if cfg!(feature = "marksweep") {
            Some(PlanSelector::MarkSweep)
        } else if cfg!(feature = "lisp2") {
            Some(PlanSelector::Lisp2)
        } else if cfg!(feature = "pageprotect") {
            Some(PlanSelector::PageProtect)
        } else if cfg!(feature = "immix") {
            Some(PlanSelector::Immix)
        } else if cfg!(feature = "genimmix") {
            Some(PlanSelector::GenImmix)
        } else if cfg!(feature = "stickyimmix") {
            Some(PlanSelector::StickyImmix)
        } else {
            None
        };
        if let Some(plan) = force_plan {
            BUILDER.lock().unwrap().options.plan.set(plan);
        }
    }

    // Make sure that we haven't initialized MMTk (by accident) yet
    assert!(!crate::MMTK_INITIALIZED.load(Ordering::SeqCst));
    // Make sure we initialize MMTk here
    if crate::use_compressed_oops() {
        lazy_static::initialize(&crate::SINGLETON_COMPRESSED);
    } else {
        lazy_static::initialize(&crate::SINGLETON_UNCOMPRESSED);
    }
    maybe_force_hybrid_barrier_on();
}

#[no_mangle]
pub extern "C" fn openjdk_is_gc_initialized() -> bool {
    crate::MMTK_INITIALIZED.load(std::sync::atomic::Ordering::SeqCst)
}

#[no_mangle]
pub extern "C" fn mmtk_set_heap_size(min: usize, max: usize) -> bool {
    use mmtk::util::options::GCTriggerSelector;
    let mut builder = BUILDER.lock().unwrap();
    let policy = if min == max {
        GCTriggerSelector::FixedHeapSize(min)
    } else {
        GCTriggerSelector::DynamicHeapSize(min, max)
    };
    builder.options.gc_trigger.set(policy)
}

#[no_mangle]
pub extern "C" fn bind_mutator(tls: VMMutatorThread) -> *mut libc::c_void {
    with_singleton!(|singleton| {
        Box::into_raw(memory_manager::bind_mutator(singleton, tls)) as *mut libc::c_void
    })
}

#[no_mangle]
// It is fine we turn the pointer back to box, as we turned a boxed value to the raw pointer in bind_mutator()
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn destroy_mutator(mutator: *mut libc::c_void) {
    with_mutator!(|mutator| memory_manager::destroy_mutator(mutator))
}

#[no_mangle]
// We trust the mutator pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn flush_mutator(mutator: *mut libc::c_void) {
    with_mutator!(|mutator| memory_manager::flush_mutator(mutator))
}

#[no_mangle]
// We trust the mutator pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn alloc(
    mutator: *mut libc::c_void,
    size: usize,
    align: usize,
    offset: usize,
    allocator: AllocationSemantics,
) -> Address {
    with_mutator!(|mutator| memory_manager::alloc(mutator, size, align, offset, allocator))
}

#[no_mangle]
pub extern "C" fn get_allocator_mapping(allocator: AllocationSemantics) -> AllocatorSelector {
    with_singleton!(|singleton| memory_manager::get_allocator_mapping(singleton, allocator))
}

/// What an inline fast path for a `SpanAllocator` (Go's `nextFreeFast`) needs. Mirrored by
/// `MMTkSpanFastPathInfo` in `mmtk.h`. All offsets are in bytes.
#[repr(C)]
pub struct SpanFastPathInfo {
    /// Whether the allocator for the semantics is a `SpanAllocator` that allocates in scan spans
    /// (if not, the other fields are zero).
    pub valid: bool,
    /// From the mutator to the allocator's array of cached spans, indexed by span class.
    pub spans_offset: usize,
    /// From the mutator to the allocator's space pointer.
    pub space_offset: usize,
    /// From the space pointer to its allocate-black flag (a byte).
    pub allocate_black_offset: usize,
    /// From the space pointer to the flag (a byte) that lets the fast path allocate black.
    pub inline_black_offset: usize,
    /// From the mutator to the allocator's count of bytes allocated black.
    pub black_bytes_offset: usize,
    /// Fields of a span descriptor.
    pub alloc_cache_offset: usize,
    pub freeindex_offset: usize,
    pub nelems_offset: usize,
    pub elemsize_offset: usize,
    pub start_offset: usize,
    pub alloc_count_offset: usize,
    pub needzero_offset: usize,
    pub mark_bits_offset: usize,
    pub scan_bits_offset: usize,
    pub black_scan_offset: usize,
    /// `size_to_span_class[size >> 3]` is the span class index for an allocation of `size` bytes
    /// (a multiple of 8, at most `max_size`) aligned to 8 bytes. 0 (an unused class whose slot
    /// always holds the empty span) if no small class fits.
    pub size_to_span_class: *const u8,
    pub max_size: usize,
}

/// Largest size in the span class table: Go's largest small size class.
const SPAN_TABLE_MAX_SIZE: usize = 32768;

static SPAN_CLASS_TABLE: sync::Lazy<Box<[u8]>> = sync::Lazy::new(|| {
    use mmtk::util::alloc::span_allocator::span_class_index;
    (0..=SPAN_TABLE_MAX_SIZE >> 3)
        .map(|i| {
            // OpenJDK allocates with an alignment of one heap word and no offset (see
            // `MMTkMutatorContext::alloc`), from the scan spans of the default allocator.
            let index = span_class_index(i << 3, 8, false).unwrap_or(0);
            u8::try_from(index).expect("span class index does not fit in a byte")
        })
        .collect()
});

/// Fill `info` for the allocator of `semantics`.
///
/// # Safety
/// `info` must point to a writable `SpanFastPathInfo`.
#[no_mangle]
pub unsafe extern "C" fn mmtk_span_fastpath_info(
    semantics: AllocationSemantics,
    info: *mut SpanFastPathInfo,
) {
    use mmtk::util::alloc::AllocatorInfo;
    let selector = get_allocator_mapping(semantics);
    let alloc_info = if crate::use_compressed_oops() {
        AllocatorInfo::new::<OpenJDK<true>>(selector)
    } else {
        AllocatorInfo::new::<OpenJDK<false>>(selector)
    };
    let result = match (selector, alloc_info) {
        // Only the scan allocator: `SpanAllocator`s other than the first are noscan in the plans
        // that have them, and OpenJDK never uses NoScan.
        (
            AllocatorSelector::Span(0),
            AllocatorInfo::Span {
                spans_offset,
                space_offset,
                allocate_black_offset,
                inline_black_offset,
                black_bytes_offset,
                alloc_cache_offset,
                freeindex_offset,
                nelems_offset,
                elemsize_offset,
                start_offset,
                alloc_count_offset,
                needzero_offset,
                mark_bits_offset,
                scan_bits_offset,
                black_scan_offset,
            },
        ) => SpanFastPathInfo {
            valid: true,
            spans_offset,
            space_offset,
            allocate_black_offset,
            inline_black_offset,
            black_bytes_offset,
            alloc_cache_offset,
            freeindex_offset,
            nelems_offset,
            elemsize_offset,
            start_offset,
            alloc_count_offset,
            needzero_offset,
            mark_bits_offset,
            scan_bits_offset,
            black_scan_offset,
            size_to_span_class: SPAN_CLASS_TABLE.as_ptr(),
            max_size: SPAN_TABLE_MAX_SIZE,
        },
        _ => SpanFastPathInfo {
            valid: false,
            spans_offset: 0,
            space_offset: 0,
            allocate_black_offset: 0,
            inline_black_offset: 0,
            black_bytes_offset: 0,
            alloc_cache_offset: 0,
            freeindex_offset: 0,
            nelems_offset: 0,
            elemsize_offset: 0,
            start_offset: 0,
            alloc_count_offset: 0,
            needzero_offset: 0,
            mark_bits_offset: 0,
            scan_bits_offset: 0,
            black_scan_offset: 0,
            size_to_span_class: std::ptr::null(),
            max_size: 0,
        },
    };
    info.write(result);
}

/// Allocate-black for an object that C2's inline SpanAllocator fast path allocated at `cell`
/// (what the interpreter and C1 fast paths do inline). A leaf: it neither blocks nor allocates.
#[no_mangle]
pub extern "C" fn mmtk_span_allocate_black(mutator: *mut libc::c_void, cell: Address) {
    use mmtk::util::alloc::SpanAllocator;
    with_mutator!(|mutator| unsafe {
        mutator
            .allocator_impl_mut_for_semantic::<SpanAllocator<_>>(AllocationSemantics::Default)
            .allocate_black_inline(cell)
    })
}

#[no_mangle]
pub extern "C" fn get_max_non_los_default_alloc_bytes() -> usize {
    with_singleton!(|singleton| {
        singleton
            .get_plan()
            .constraints()
            .max_non_los_default_alloc_bytes
    })
}

#[no_mangle]
// We trust the mutator pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn post_alloc(
    mutator: *mut libc::c_void,
    refer: ObjectReference,
    bytes: usize,
    allocator: AllocationSemantics,
) {
    with_mutator!(|mutator| memory_manager::post_alloc(mutator, refer, bytes, allocator))
}

#[no_mangle]
pub extern "C" fn will_never_move(object: ObjectReference) -> bool {
    !object.is_movable()
}

#[no_mangle]
// We trust the worker pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn start_worker(tls: VMWorkerThread, worker: *mut libc::c_void) {
    if crate::use_compressed_oops() {
        let worker = unsafe { Box::from_raw(worker as *mut GCWorker<OpenJDK<true>>) };
        memory_manager::start_worker::<OpenJDK<true>>(crate::singleton::<true>(), tls, worker)
    } else {
        let worker = unsafe { Box::from_raw(worker as *mut GCWorker<OpenJDK<false>>) };
        memory_manager::start_worker::<OpenJDK<false>>(crate::singleton::<false>(), tls, worker)
    }
}

#[no_mangle]
pub extern "C" fn initialize_collection(tls: VMThread) {
    with_singleton!(|singleton| memory_manager::initialize_collection(singleton, tls))
}

#[no_mangle]
pub extern "C" fn used_bytes() -> usize {
    with_singleton!(|singleton| memory_manager::used_bytes(singleton))
}

#[no_mangle]
pub extern "C" fn free_bytes() -> usize {
    with_singleton!(|singleton| memory_manager::free_bytes(singleton))
}

#[no_mangle]
pub extern "C" fn total_bytes() -> usize {
    with_singleton!(|singleton| memory_manager::total_bytes(singleton))
}

#[no_mangle]
pub extern "C" fn handle_user_collection_request(tls: VMMutatorThread, force: bool) {
    with_singleton!(|singleton| {
        memory_manager::handle_user_collection_request(singleton, tls, force);
    })
}

#[no_mangle]
pub extern "C" fn mmtk_enable_compressed_oops() {
    crate::slots::enable_compressed_oops()
}

#[no_mangle]
pub extern "C" fn mmtk_set_compressed_klass_base_and_shift(base: Address, shift: usize) {
    crate::abi::set_compressed_klass_base_and_shift(base, shift)
}

#[no_mangle]
pub extern "C" fn is_in_mmtk_spaces(object: ObjectReference) -> bool {
    memory_manager::is_in_mmtk_spaces(object)
}

#[no_mangle]
pub extern "C" fn is_mapped_address(addr: Address) -> bool {
    memory_manager::is_mapped_address(addr)
}

#[no_mangle]
pub extern "C" fn add_weak_candidate(reff: ObjectReference) {
    with_singleton!(|singleton| memory_manager::add_weak_candidate(singleton, reff))
}

#[no_mangle]
pub extern "C" fn add_soft_candidate(reff: ObjectReference) {
    with_singleton!(|singleton| memory_manager::add_soft_candidate(singleton, reff))
}

#[no_mangle]
pub extern "C" fn add_phantom_candidate(reff: ObjectReference) {
    with_singleton!(|singleton| memory_manager::add_phantom_candidate(singleton, reff))
}

// The harness_begin()/end() functions are different than other API functions in terms of the thread state.
// Other functions are called by the VM, thus the thread should already be in the VM state. But the harness
// functions are called by the probe, and the thread is in JNI/application/native state. Thus we need call
// into VM to switch the thread state and VM will then call into mmtk-core again to do the actual work of
// harness_begin() and harness_end()

#[no_mangle]
pub extern "C" fn harness_begin(_id: usize) {
    unsafe { ((*UPCALLS).harness_begin)() };
}

#[no_mangle]
pub extern "C" fn mmtk_harness_begin_impl() {
    // Pass null as tls, OpenJDK binding does not rely on the tls value to block the current thread and do a GC
    with_singleton!(|singleton| {
        memory_manager::harness_begin(singleton, VMMutatorThread(VMThread::UNINITIALIZED));
    })
}

#[no_mangle]
pub extern "C" fn harness_end(_id: usize) {
    unsafe { ((*UPCALLS).harness_end)() };
}

#[no_mangle]
pub extern "C" fn mmtk_harness_end_impl() {
    with_singleton!(|singleton| memory_manager::harness_end(singleton))
}

#[no_mangle]
// We trust the name/value pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn process(name: *const c_char, value: *const c_char) -> bool {
    let name_str: &CStr = unsafe { CStr::from_ptr(name) };
    let value_str: &CStr = unsafe { CStr::from_ptr(value) };
    let mut builder = BUILDER.lock().unwrap();
    memory_manager::process(
        &mut builder,
        name_str.to_str().unwrap(),
        value_str.to_str().unwrap(),
    )
}

#[no_mangle]
pub extern "C" fn mmtk_builder_read_env_var_settings() {
    let mut builder = BUILDER.lock().unwrap();
    builder.options.read_env_var_settings();
}

/// Pass hotspot `ParallelGCThreads` flag to mmtk
#[no_mangle]
pub extern "C" fn mmtk_builder_set_threads(value: usize) {
    let mut builder = BUILDER.lock().unwrap();
    builder.options.threads.set(value);
}

/// Pass hotspot `ConcGCThreads` flag to mmtk
#[no_mangle]
pub extern "C" fn mmtk_builder_set_conc_threads(value: usize) {
    let mut builder = BUILDER.lock().unwrap();
    builder.options.concurrent_threads.set(value);
}

/// Pass hotspot `UseTransparentHugePages` flag to mmtk
#[no_mangle]
pub extern "C" fn mmtk_builder_set_transparent_hugepages(value: bool) {
    let mut builder = BUILDER.lock().unwrap();
    builder.options.transparent_hugepages.set(value);
}

#[no_mangle]
// We trust the name/value pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn process_bulk(options: *const c_char) -> bool {
    let options_str: &CStr = unsafe { CStr::from_ptr(options) };
    let mut builder = BUILDER.lock().unwrap();
    memory_manager::process_bulk(&mut builder, options_str.to_str().unwrap())
}

#[no_mangle]
pub extern "C" fn mmtk_narrow_oop_base() -> Address {
    debug_assert!(crate::use_compressed_oops());
    crate::slots::BASE.load(Ordering::Relaxed)
}

#[no_mangle]
pub extern "C" fn mmtk_narrow_oop_shift() -> usize {
    debug_assert!(crate::use_compressed_oops());
    crate::slots::SHIFT.load(Ordering::Relaxed)
}

#[no_mangle]
pub extern "C" fn starting_heap_address() -> Address {
    memory_manager::starting_heap_address()
}

#[no_mangle]
pub extern "C" fn last_heap_address() -> Address {
    memory_manager::last_heap_address()
}

#[no_mangle]
pub extern "C" fn openjdk_max_capacity() -> usize {
    with_singleton!(|singleton| memory_manager::total_bytes(singleton))
}

#[no_mangle]
pub extern "C" fn executable() -> bool {
    true
}

#[no_mangle]
pub extern "C" fn mmtk_load_reference(mutator: *mut libc::c_void, o: ObjectReference) {
    with_mutator!(|mutator| mutator.barrier().load_weak_reference(o))
}

/// Full pre barrier
#[no_mangle]
pub extern "C" fn mmtk_object_reference_write_pre(
    mutator: *mut libc::c_void,
    src: ObjectReference,
    slot: Address,
    target: NullableObjectReference,
) {
    with_mutator!(|mutator| {
        mutator
            .barrier()
            .object_reference_write_pre(src, slot.into(), target.into());
    })
}

/// Full post barrier
#[no_mangle]
pub extern "C" fn mmtk_object_reference_write_post(
    mutator: *mut libc::c_void,
    src: ObjectReference,
    slot: Address,
    target: NullableObjectReference,
) {
    with_mutator!(|mutator| {
        mutator
            .barrier()
            .object_reference_write_post(src, slot.into(), target.into());
    })
}

/// Barrier slow-path call
#[no_mangle]
pub extern "C" fn mmtk_object_reference_write_slow(
    mutator: *mut libc::c_void,
    src: ObjectReference,
    slot: Address,
    target: NullableObjectReference,
) {
    with_mutator!(|mutator| {
        mutator
            .barrier()
            .object_reference_write_slow(src, slot.into(), target.into());
    })
}

fn log_bytes_in_slot() -> usize {
    if crate::use_compressed_oops() {
        OpenJDKSlot::<true>::LOG_BYTES_IN_SLOT
    } else {
        OpenJDKSlot::<false>::LOG_BYTES_IN_SLOT
    }
}

/// Array-copy pre-barrier
#[no_mangle]
pub extern "C" fn mmtk_array_copy_pre(
    mutator: *mut libc::c_void,
    src: Address,
    dst: Address,
    count: usize,
) {
    let bytes = count << log_bytes_in_slot();
    with_mutator!(|mutator| {
        mutator
            .barrier()
            .memory_region_copy_pre((src..src + bytes).into(), (dst..dst + bytes).into());
    })
}

/// Array-copy pre-barrier that only reads the source.  Used when the destination is not
/// initialised yet (a new array that is filled by the copy, or an object filled by `clone`), so its
/// old contents must not be read.  The destination range passed to MMTk is empty.
#[no_mangle]
pub extern "C" fn mmtk_array_copy_pre_src_only(
    mutator: *mut libc::c_void,
    src: Address,
    dst: Address,
    count: usize,
) {
    let bytes = count << log_bytes_in_slot();
    with_mutator!(|mutator| {
        mutator
            .barrier()
            .memory_region_copy_pre((src..src + bytes).into(), (dst..dst).into());
    })
}

/// Array-copy post-barrier
#[no_mangle]
pub extern "C" fn mmtk_array_copy_post(
    mutator: *mut libc::c_void,
    src: Address,
    dst: Address,
    count: usize,
) {
    with_mutator!(|mutator| {
        let bytes = count << log_bytes_in_slot();
        mutator
            .barrier()
            .memory_region_copy_post((src..src + bytes).into(), (dst..dst + bytes).into());
    })
}

/// C2 Slowpath allocation barrier
#[no_mangle]
pub extern "C" fn mmtk_object_probable_write(mutator: *mut libc::c_void, obj: ObjectReference) {
    with_mutator!(|mutator| mutator.barrier().object_probable_write(obj));
}

// finalization
#[no_mangle]
pub extern "C" fn add_finalizer(object: ObjectReference) {
    with_singleton!(|singleton| memory_manager::add_finalizer(singleton, object));
}

#[no_mangle]
pub extern "C" fn get_finalized_object() -> NullableObjectReference {
    with_singleton!(|singleton| memory_manager::get_finalized_object(singleton).into())
}

/// Test if an object is live at the end of a GC.
/// Note: only call this method after the liveness tracing and before gc release.
#[no_mangle]
pub extern "C" fn mmtk_is_live(object: NullableObjectReference) -> usize {
    let o: Option<ObjectReference> = object.into();
    let Some(object) = o else {
        return 0;
    };
    debug_assert!(
        object.to_raw_address().is_mapped(),
        "{:?} is not mapped",
        object
    );
    object.is_live() as _
}

/// If the object is non-null and forwarded, return the forwarded pointer. Otherwise, return the original pointer.
#[no_mangle]
pub extern "C" fn mmtk_get_forwarded_ref(
    object: NullableObjectReference,
) -> NullableObjectReference {
    let o: Option<ObjectReference> = object.into();
    let Some(o) = o else {
        return None.into();
    };
    match o.get_forwarded_object() {
        Some(o) => Some(o).into(),
        None => object,
    }
}

thread_local! {
    /// Cache reference slots of an nmethod while the current thread is executing
    /// `MMTkRegisterNMethodOopClosure`.
    static NMETHOD_SLOTS: RefCell<Vec<Address>> = const { RefCell::new(vec![]) };
}

/// Report one reference slot in an nmethod to MMTk.
#[no_mangle]
pub extern "C" fn mmtk_add_nmethod_oop(addr: Address) {
    NMETHOD_SLOTS.with_borrow_mut(|x| x.push(addr))
}

/// Register an nmethod.
///
/// The C++ part of the binding should have scanned the nmethod and reported all the reference slots
/// using `mmtk_add_nmethod_oop` before calling this function. This function will transfer all the
/// locally cached slots of this nmethod to the global storage.
#[no_mangle]
pub extern "C" fn mmtk_register_nmethod(nm: Address) {
    NMETHOD_SLOTS.with_borrow_mut(|slots| {
        if !slots.is_empty() {
            let mut roots = crate::NURSERY_CODE_CACHE_ROOTS.lock().unwrap();
            let mut mature_roots = crate::MATURE_CODE_CACHE_ROOTS.lock().unwrap();
            // The nmethod might already be in the mature roots, if we
            // are re-registering the nmethod due to code patching.
            // If the nmethod is already in the mature roots, we demote
            // the nmethod to the nursery roots, as the nmethod might
            // now refer to a young object.
            mature_roots.remove(&nm);
            roots.insert(nm, std::mem::take(slots));
        }
    });
}

/// Unregister an nmethod.
#[no_mangle]
pub extern "C" fn mmtk_unregister_nmethod(nm: Address) {
    {
        let mut roots = crate::NURSERY_CODE_CACHE_ROOTS.lock().unwrap();
        roots.remove(&nm);
    }
    {
        let mut roots = crate::MATURE_CODE_CACHE_ROOTS.lock().unwrap();
        roots.remove(&nm);
    }
}
