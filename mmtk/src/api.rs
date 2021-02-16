use crate::OpenJDK;
use crate::OpenJDK_Upcalls;
use crate::SINGLETON;
use crate::UPCALLS;
use libc::{c_char, c_void};
use mmtk::memory_manager;
use mmtk::plan::barriers::BarrierSelector;
use mmtk::scheduler::GCWorker;
use mmtk::util::alloc::allocators::AllocatorSelector;
use mmtk::util::alloc::is_alloced_by_malloc;
use mmtk::util::constants::LOG_BYTES_IN_PAGE;
use mmtk::util::options::PlanSelector;
use mmtk::util::{Address, ObjectReference, OpaquePointer};
use mmtk::AllocationSemantics;
use mmtk::Mutator;
use mmtk::MutatorContext;
use mmtk::{Plan, MMTK};
use std::ffi::{CStr, CString};
use std::lazy::SyncLazy;

// Supported barriers:
static NO_BARRIER: SyncLazy<CString> = SyncLazy::new(|| CString::new("NoBarrier").unwrap());
static OBJECT_BARRIER: SyncLazy<CString> = SyncLazy::new(|| CString::new("ObjectBarrier").unwrap());

#[no_mangle]
pub extern "C" fn mmtk_active_barrier() -> *const c_char {
    match SINGLETON.plan.constraints().barrier {
        BarrierSelector::NoBarrier => NO_BARRIER.as_ptr(),
        BarrierSelector::ObjectBarrier => OBJECT_BARRIER.as_ptr(),
        _ => unimplemented!(),
    }
}

#[no_mangle]
pub extern "C" fn release_buffer(ptr: *mut Address, length: usize, capacity: usize) {
    let _vec = unsafe { Vec::<Address>::from_raw_parts(ptr, length, capacity) };
}

#[no_mangle]
pub extern "C" fn openjdk_gc_init(calls: *const OpenJDK_Upcalls, heap_size: usize) {
    unsafe { UPCALLS = calls };
    crate::abi::validate_memory_layouts();
    let singleton_mut =
        unsafe { &mut *(&*SINGLETON as *const MMTK<OpenJDK> as *mut MMTK<OpenJDK>) };
    memory_manager::gc_init(singleton_mut, heap_size);
}

#[no_mangle]
pub extern "C" fn start_control_collector(tls: OpaquePointer) {
    memory_manager::start_control_collector(&SINGLETON, tls);
}

#[no_mangle]
pub extern "C" fn bind_mutator(tls: OpaquePointer) -> *mut Mutator<OpenJDK> {
    Box::into_raw(memory_manager::bind_mutator(&SINGLETON, tls))
}

#[no_mangle]
pub extern "C" fn destroy_mutator(mutator: *mut Mutator<OpenJDK>) {
    memory_manager::destroy_mutator(unsafe { Box::from_raw(mutator) })
}

#[no_mangle]
pub extern "C" fn flush_mutator(mutator: *mut Mutator<OpenJDK>) {
    memory_manager::flush_mutator(unsafe { &mut *mutator })
}

#[no_mangle]
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

// Allocation slow path

use mmtk::util::alloc::Allocator as IAllocator;
use mmtk::util::alloc::{BumpAllocator, LargeObjectAllocator, MallocAllocator};
use mmtk::util::heap::MonotonePageResource;

#[no_mangle]
pub extern "C" fn alloc_slow_bump_monotone_immortal(
    allocator: *mut c_void,
    size: usize,
    align: usize,
    offset: isize,
) -> Address {
    use mmtk::policy::immortalspace::ImmortalSpace;
    unsafe { &mut *(allocator as *mut BumpAllocator<OpenJDK>) }.alloc_slow(size, align, offset)
}

#[no_mangle]
pub extern "C" fn is_in_reserved_malloc(obj: ObjectReference) -> bool {
    if !matches!(SINGLETON.plan.options().plan, PlanSelector::MarkSweep) {
        false
    } else if is_alloced_by_malloc(obj) {
        true
    } else {
        false
    }
}

// For plans that do not include copy space, use the other implementation
// FIXME: after we remove plan as build-time option, we should remove this conditional compilation as well.

#[no_mangle]
#[cfg(any(feature = "semispace", feature = "gencopy"))]
pub extern "C" fn alloc_slow_bump_monotone_copy(
    allocator: *mut c_void,
    size: usize,
    align: usize,
    offset: isize,
) -> Address {
    use mmtk::policy::copyspace::CopySpace;
    unsafe { &mut *(allocator as *mut BumpAllocator<OpenJDK>) }.alloc_slow(size, align, offset)
}
#[no_mangle]
#[cfg(not(any(feature = "semispace", feature = "gencopy")))]
pub extern "C" fn alloc_slow_bump_monotone_copy(
    allocator: *mut c_void,
    size: usize,
    align: usize,
    offset: isize,
) -> Address {
    unimplemented!()
}

#[no_mangle]
pub extern "C" fn alloc_slow_largeobject(
    allocator: *mut c_void,
    size: usize,
    align: usize,
    offset: isize,
) -> Address {
    unsafe { &mut *(allocator as *mut LargeObjectAllocator<OpenJDK>) }
        .alloc_slow(size, align, offset)
}

#[no_mangle]
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
pub extern "C" fn start_worker(tls: OpaquePointer, worker: *mut GCWorker<OpenJDK>) {
    memory_manager::start_worker::<OpenJDK>(tls, unsafe { worker.as_mut().unwrap() }, &SINGLETON)
}

#[no_mangle]
pub extern "C" fn enable_collection(tls: OpaquePointer) {
    memory_manager::enable_collection(&SINGLETON, tls)
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
pub extern "C" fn handle_user_collection_request(tls: OpaquePointer) {
    memory_manager::handle_user_collection_request::<OpenJDK>(&SINGLETON, tls);
}

#[no_mangle]
pub extern "C" fn is_mapped_object(object: ObjectReference) -> bool {
    object.is_mapped()
}

#[no_mangle]
pub extern "C" fn is_mapped_address(addr: Address) -> bool {
    addr.is_mapped()
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
    memory_manager::harness_begin(&SINGLETON, OpaquePointer::UNINITIALIZED);
    unsafe { ((*UPCALLS).leave_vm)(state) };
}

#[no_mangle]
pub extern "C" fn harness_end(_id: usize) {
    let state = unsafe { ((*UPCALLS).enter_vm)() };
    memory_manager::harness_end(&SINGLETON);
    unsafe { ((*UPCALLS).leave_vm)(state) };
}

#[no_mangle]
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
pub extern "C" fn starting_heap_address() -> Address {
    memory_manager::starting_heap_address()
}

#[no_mangle]
pub extern "C" fn last_heap_address() -> Address {
    memory_manager::last_heap_address()
}

#[no_mangle]
pub extern "C" fn openjdk_max_capacity() -> usize {
    SINGLETON.plan.get_total_pages() << LOG_BYTES_IN_PAGE
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

#[no_mangle]
pub extern "C" fn record_modified_edge(mutator: &'static mut Mutator<OpenJDK>, slot: Address) {
    mutator.record_modified_edge(slot);
}
