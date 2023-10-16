use crate::edges::OpenJDKEdge;
use crate::OpenJDK;
use crate::OpenJDK_Upcalls;
use crate::BUILDER;
use crate::UPCALLS;
use libc::c_char;
use mmtk::memory_manager;
use mmtk::plan::BarrierSelector;
use mmtk::scheduler::GCController;
use mmtk::scheduler::GCWorker;
use mmtk::util::alloc::AllocatorSelector;
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
            // In case we have more barriers in mmtk-core.
            #[allow(unreachable_patterns)]
            _ => unimplemented!(),
        }
    })
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
        } else if cfg!(feature = "markcompact") {
            Some(PlanSelector::MarkCompact)
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
// We trust the gc_collector pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn start_control_collector(tls: VMWorkerThread, gc_controller: *mut libc::c_void) {
    if crate::use_compressed_oops() {
        let mut gc_controller =
            unsafe { Box::from_raw(gc_controller as *mut GCController<OpenJDK<true>>) };
        memory_manager::start_control_collector(
            crate::singleton::<true>(),
            tls,
            &mut gc_controller,
        );
    } else {
        let mut gc_controller =
            unsafe { Box::from_raw(gc_controller as *mut GCController<OpenJDK<false>>) };
        memory_manager::start_control_collector(
            crate::singleton::<false>(),
            tls,
            &mut gc_controller,
        );
    }
}

#[no_mangle]
// We trust the worker pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn start_worker(tls: VMWorkerThread, worker: *mut libc::c_void) {
    if crate::use_compressed_oops() {
        let mut worker = unsafe { Box::from_raw(worker as *mut GCWorker<OpenJDK<true>>) };
        memory_manager::start_worker::<OpenJDK<true>>(crate::singleton::<true>(), tls, &mut worker)
    } else {
        let mut worker = unsafe { Box::from_raw(worker as *mut GCWorker<OpenJDK<false>>) };
        memory_manager::start_worker::<OpenJDK<false>>(
            crate::singleton::<false>(),
            tls,
            &mut worker,
        )
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
#[cfg(feature = "sanity")]
pub extern "C" fn scan_region() {
    with_singleton!(|singleton| memory_manager::scan_region(singleton))
}

#[no_mangle]
pub extern "C" fn handle_user_collection_request(tls: VMMutatorThread) {
    with_singleton!(|singleton| {
        memory_manager::handle_user_collection_request(singleton, tls);
    })
}

#[no_mangle]
pub extern "C" fn mmtk_enable_compressed_oops() {
    crate::edges::enable_compressed_oops()
}

#[no_mangle]
pub extern "C" fn mmtk_set_compressed_klass_base_and_shift(base: Address, shift: usize) {
    crate::abi::set_compressed_klass_base_and_shift(base, shift)
}

#[no_mangle]
pub extern "C" fn is_in_mmtk_spaces(object: ObjectReference) -> bool {
    if crate::use_compressed_oops() {
        memory_manager::is_in_mmtk_spaces::<OpenJDK<true>>(object)
    } else {
        memory_manager::is_in_mmtk_spaces::<OpenJDK<false>>(object)
    }
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
    crate::edges::BASE.load(Ordering::Relaxed)
}

#[no_mangle]
pub extern "C" fn mmtk_narrow_oop_shift() -> usize {
    debug_assert!(crate::use_compressed_oops());
    crate::edges::SHIFT.load(Ordering::Relaxed)
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

/// Full pre barrier
#[no_mangle]
pub extern "C" fn mmtk_object_reference_write_pre(
    mutator: *mut libc::c_void,
    src: ObjectReference,
    slot: Address,
    target: ObjectReference,
) {
    with_mutator!(|mutator| {
        mutator
            .barrier()
            .object_reference_write_pre(src, slot.into(), target);
    })
}

/// Full post barrier
#[no_mangle]
pub extern "C" fn mmtk_object_reference_write_post(
    mutator: *mut libc::c_void,
    src: ObjectReference,
    slot: Address,
    target: ObjectReference,
) {
    with_mutator!(|mutator| {
        mutator
            .barrier()
            .object_reference_write_post(src, slot.into(), target);
    })
}

/// Barrier slow-path call
#[no_mangle]
pub extern "C" fn mmtk_object_reference_write_slow(
    mutator: *mut libc::c_void,
    src: ObjectReference,
    slot: Address,
    target: ObjectReference,
) {
    with_mutator!(|mutator| {
        mutator
            .barrier()
            .object_reference_write_slow(src, slot.into(), target);
    })
}

fn log_bytes_in_edge() -> usize {
    if crate::use_compressed_oops() {
        OpenJDKEdge::<true>::LOG_BYTES_IN_EDGE
    } else {
        OpenJDKEdge::<false>::LOG_BYTES_IN_EDGE
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
    let bytes = count << log_bytes_in_edge();
    with_mutator!(|mutator| {
        mutator
            .barrier()
            .memory_region_copy_pre((src..src + bytes).into(), (dst..dst + bytes).into());
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
        let bytes = count << log_bytes_in_edge();
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
pub extern "C" fn get_finalized_object() -> ObjectReference {
    with_singleton!(|singleton| {
        match memory_manager::get_finalized_object(singleton) {
            Some(obj) => obj,
            None => ObjectReference::NULL,
        }
    })
}

thread_local! {
    /// Cache all the pointers reported by the current thread.
    static NMETHOD_SLOTS: RefCell<Vec<Address>> = RefCell::new(vec![]);
}

/// Report a list of pointers in nmethod to mmtk.
#[no_mangle]
pub extern "C" fn mmtk_add_nmethod_oop(addr: Address) {
    NMETHOD_SLOTS.with(|x| x.borrow_mut().push(addr))
}

/// Register a nmethod.
/// The c++ part of the binding should scan the nmethod and report all the pointers to mmtk first, before calling this function.
/// This function will transfer all the locally cached pointers of this nmethod to the global storage.
#[no_mangle]
pub extern "C" fn mmtk_register_nmethod(nm: Address) {
    let slots = NMETHOD_SLOTS.with(|x| {
        if x.borrow().len() == 0 {
            return None;
        }
        Some(x.replace(vec![]))
    });
    let slots = match slots {
        Some(slots) => slots,
        _ => return,
    };
    let mut roots = crate::CODE_CACHE_ROOTS.lock().unwrap();
    // Relaxed add instead of `fetch_add`, since we've already acquired the lock.
    crate::CODE_CACHE_ROOTS_SIZE.store(
        crate::CODE_CACHE_ROOTS_SIZE.load(Ordering::Relaxed) + slots.len(),
        Ordering::Relaxed,
    );
    roots.insert(nm, slots);
}

/// Unregister a nmethod.
#[no_mangle]
pub extern "C" fn mmtk_unregister_nmethod(nm: Address) {
    let mut roots = crate::CODE_CACHE_ROOTS.lock().unwrap();
    if let Some(slots) = roots.remove(&nm) {
        // Relaxed sub instead of `fetch_sub`, since we've already acquired the lock.
        crate::CODE_CACHE_ROOTS_SIZE.store(
            crate::CODE_CACHE_ROOTS_SIZE.load(Ordering::Relaxed) - slots.len(),
            Ordering::Relaxed,
        );
    }
}
