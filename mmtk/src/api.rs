use crate::OpenJDK;
use crate::OpenJDK_Upcalls;
use crate::SINGLETON;
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
use mmtk::MMTK;
use std::ffi::{CStr, CString};
use std::lazy::SyncLazy;

// Supported barriers:
static NO_BARRIER: SyncLazy<CString> = SyncLazy::new(|| CString::new("NoBarrier").unwrap());
static OBJECT_BARRIER: SyncLazy<CString> = SyncLazy::new(|| CString::new("ObjectBarrier").unwrap());

#[no_mangle]
pub extern "C" fn mmtk_active_barrier() -> *const c_char {
    match SINGLETON.get_plan().constraints().barrier {
        BarrierSelector::NoBarrier => NO_BARRIER.as_ptr(),
        BarrierSelector::ObjectBarrier => OBJECT_BARRIER.as_ptr(),
        // In case we have more barriers in mmtk-core.
        #[allow(unreachable_patterns)]
        _ => unimplemented!(),
    }
}

/// # Safety
/// Caller needs to make sure the ptr is a valid vector pointer.
#[no_mangle]
pub unsafe extern "C" fn release_buffer(ptr: *mut Address, length: usize, capacity: usize) {
    let _vec = Vec::<Address>::from_raw_parts(ptr, length, capacity);
}

#[no_mangle]
pub extern "C" fn openjdk_gc_init(calls: *const OpenJDK_Upcalls, heap_size: usize) {
    unsafe { UPCALLS = calls };
    crate::abi::validate_memory_layouts();
    // MMTk should not be used before gc_init, and gc_init is single threaded. It is fine we get a mutable reference from the singleton.
    #[allow(clippy::cast_ref_to_mut)]
    let singleton_mut =
        unsafe { &mut *(&*SINGLETON as *const MMTK<OpenJDK> as *mut MMTK<OpenJDK>) };
    memory_manager::gc_init(singleton_mut, heap_size);
}

#[no_mangle]
pub extern "C" fn bind_mutator(tls: VMMutatorThread) -> *mut Mutator<OpenJDK> {
    Box::into_raw(memory_manager::bind_mutator(&SINGLETON, tls))
}

#[no_mangle]
// It is fine we turn the pointer back to box, as we turned a boxed value to the raw pointer in bind_mutator()
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn destroy_mutator(mutator: *mut Mutator<OpenJDK>) {
    memory_manager::destroy_mutator(unsafe { Box::from_raw(mutator) })
}

#[no_mangle]
// We trust the mutator pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn flush_mutator(mutator: *mut Mutator<OpenJDK>) {
    memory_manager::flush_mutator(unsafe { &mut *mutator })
}

#[no_mangle]
// We trust the mutator pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn alloc(
    mutator: *mut Mutator<OpenJDK>,
    size: usize,
    align: usize,
    offset: isize,
    allocator: AllocationSemantics,
) -> Address {
    memory_manager::alloc::<OpenJDK>(unsafe { &mut *mutator }, size, align, offset, allocator)
}

#[no_mangle]
pub extern "C" fn get_allocator_mapping(allocator: AllocationSemantics) -> AllocatorSelector {
    memory_manager::get_allocator_mapping(&SINGLETON, allocator)
}

#[no_mangle]
pub extern "C" fn get_max_non_los_default_alloc_bytes() -> usize {
    SINGLETON
        .get_plan()
        .constraints()
        .max_non_los_default_alloc_bytes
}

#[no_mangle]
// We trust the mutator pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn post_alloc(
    mutator: *mut Mutator<OpenJDK>,
    refer: ObjectReference,
    bytes: usize,
    allocator: AllocationSemantics,
) {
    memory_manager::post_alloc::<OpenJDK>(unsafe { &mut *mutator }, refer, bytes, allocator)
}

#[no_mangle]
pub extern "C" fn will_never_move(object: ObjectReference) -> bool {
    !object.is_movable()
}

#[no_mangle]
// We trust the gc_collector pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn start_control_collector(
    tls: VMWorkerThread,
    gc_controller: *mut GCController<OpenJDK>,
) {
    let mut gc_controller = unsafe { Box::from_raw(gc_controller) };
    memory_manager::start_control_collector(&SINGLETON, tls, &mut gc_controller);
}

#[no_mangle]
// We trust the worker pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn start_worker(tls: VMWorkerThread, worker: *mut GCWorker<OpenJDK>) {
    let mut worker = unsafe { Box::from_raw(worker) };
    memory_manager::start_worker::<OpenJDK>(&SINGLETON, tls, &mut worker)
}

#[no_mangle]
pub extern "C" fn initialize_collection(tls: VMThread) {
    memory_manager::initialize_collection(&SINGLETON, tls)
}

#[no_mangle]
pub extern "C" fn used_bytes() -> usize {
    memory_manager::used_bytes(&SINGLETON)
}

#[no_mangle]
pub extern "C" fn free_bytes() -> usize {
    memory_manager::free_bytes(&SINGLETON)
}

#[no_mangle]
pub extern "C" fn total_bytes() -> usize {
    memory_manager::total_bytes(&SINGLETON)
}

#[no_mangle]
#[cfg(feature = "sanity")]
pub extern "C" fn scan_region() {
    memory_manager::scan_region(&SINGLETON)
}

#[no_mangle]
pub extern "C" fn handle_user_collection_request(tls: VMMutatorThread) {
    memory_manager::handle_user_collection_request::<OpenJDK>(&SINGLETON, tls);
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
pub extern "C" fn modify_check(object: ObjectReference) {
    memory_manager::modify_check(&SINGLETON, object)
}

#[no_mangle]
pub extern "C" fn add_weak_candidate(reff: ObjectReference, referent: ObjectReference) {
    memory_manager::add_weak_candidate(&SINGLETON, reff, referent)
}

#[no_mangle]
pub extern "C" fn add_soft_candidate(reff: ObjectReference, referent: ObjectReference) {
    memory_manager::add_soft_candidate(&SINGLETON, reff, referent)
}

#[no_mangle]
pub extern "C" fn add_phantom_candidate(reff: ObjectReference, referent: ObjectReference) {
    memory_manager::add_phantom_candidate(&SINGLETON, reff, referent)
}

// The harness_begin()/end() functions are different than other API functions in terms of the thread state.
// Other functions are called by the VM, thus the thread should already be in the VM state. But the harness
// functions are called by the probe, and the thread is in JNI/application/native state. Thus we need an extra call
// to switch the thread state (enter_vm/leave_vm)

#[no_mangle]
pub extern "C" fn harness_begin(_id: usize) {
    let state = unsafe { ((*UPCALLS).enter_vm)() };
    // Pass null as tls, OpenJDK binding does not rely on the tls value to block the current thread and do a GC
    memory_manager::harness_begin(&SINGLETON, VMMutatorThread(VMThread::UNINITIALIZED));
    unsafe { ((*UPCALLS).leave_vm)(state) };
}

#[no_mangle]
pub extern "C" fn harness_end(_id: usize) {
    let state = unsafe { ((*UPCALLS).enter_vm)() };
    memory_manager::harness_end(&SINGLETON);
    unsafe { ((*UPCALLS).leave_vm)(state) };
}

#[no_mangle]
// We trust the name/value pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn process(name: *const c_char, value: *const c_char) -> bool {
    let name_str: &CStr = unsafe { CStr::from_ptr(name) };
    let value_str: &CStr = unsafe { CStr::from_ptr(value) };
    memory_manager::process(
        &SINGLETON,
        name_str.to_str().unwrap(),
        value_str.to_str().unwrap(),
    )
}

#[no_mangle]
// We trust the name/value pointer is valid.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub extern "C" fn process_bulk(options: *const c_char) -> bool {
    let options_str: &CStr = unsafe { CStr::from_ptr(options) };
    memory_manager::process_bulk(&SINGLETON, options_str.to_str().unwrap())
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
    memory_manager::total_bytes(&SINGLETON)
}

#[no_mangle]
pub extern "C" fn executable() -> bool {
    true
}

#[no_mangle]
pub extern "C" fn record_modified_node(
    mutator: &'static mut Mutator<OpenJDK>,
    obj: ObjectReference,
) {
    mutator.record_modified_node(obj);
}

// finalization
#[no_mangle]
pub extern "C" fn add_finalizer(object: ObjectReference) {
    memory_manager::add_finalizer(&SINGLETON, object);
}

#[no_mangle]
pub extern "C" fn get_finalized_object() -> ObjectReference {
    match memory_manager::get_finalized_object(&SINGLETON) {
        Some(obj) => obj,
        None => unsafe { Address::ZERO.to_object_reference() },
    }
}
