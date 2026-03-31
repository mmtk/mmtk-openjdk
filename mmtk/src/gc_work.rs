use crate::scanning;
use crate::scanning::to_slots_closure;
use crate::NewBuffer;
use crate::OpenJDK;
use crate::OpenJDKSlot;
use crate::Slot;
use crate::SlotsClosure;
use crate::UPCALLS;
use mmtk::plan::immix::Pause;
use mmtk::scheduler::*;
use mmtk::util::Address;
use mmtk::vm::RootsWorkFactory;
use mmtk::vm::*;
use mmtk::MMTK;
use std::marker::PhantomData;
use std::sync::atomic::{AtomicUsize, Ordering};

thread_local! {
    pub static COUNT: AtomicUsize = AtomicUsize::new(0);
}

pub fn record_roots(len: usize) {
    super::gc_work::COUNT.with(|x| {
        let c = x.load(Ordering::Relaxed);
        x.store(c + len, Ordering::Relaxed);
    });
}

fn report_roots(name: &str, ms: f32) {
    super::gc_work::COUNT.with(|x| {
        let c = x.load(Ordering::Relaxed);
        eprintln!(" - {} roots count: {} ({:.3}ms)", name, c, ms);
        x.store(0, Ordering::Relaxed);
    });
}

macro_rules! scan_roots_work {
    ($struct_name: ident, $func_name: ident) => {
        pub struct $struct_name<VM: VMBinding, F: RootsWorkFactory<VM::VMSlot>> {
            factory: F,
            _p: std::marker::PhantomData<VM>,
        }

        impl<VM: VMBinding, F: RootsWorkFactory<VM::VMSlot>> $struct_name<VM, F> {
            pub fn new(factory: F) -> Self {
                Self {
                    factory,
                    _p: std::marker::PhantomData,
                }
            }
        }

        impl<VM: VMBinding, F: RootsWorkFactory<VM::VMSlot>> GCWork<VM> for $struct_name<VM, F> {
            fn do_work(&mut self, _worker: &mut GCWorker<VM>, _mmtk: &'static MMTK<VM>) {
                let t = if cfg!(feature = "roots_breakdown") {
                    Some(std::time::SystemTime::now())
                } else {
                    None
                };
                unsafe {
                    ((*UPCALLS).$func_name)(to_slots_closure(&mut self.factory));
                }
                if cfg!(feature = "roots_breakdown") {
                    let name = stringify!($struct_name);
                    let ms = t.unwrap().elapsed().unwrap().as_micros() as f32 / 1000f32;
                    report_roots(&name[4..name.len() - 5], ms);
                }
            }
        }
    };
}

scan_roots_work!(ScanOopStorageSetRoots, scan_oop_storage_set_roots);
scan_roots_work!(ScanVMThreadRoots, scan_vm_thread_roots);

extern "C" fn report_slots_and_renew_buffer_cld<
    S: Slot,
    F: RootsWorkFactory<S>,
    const WEAK: bool,
    const ALL_STRONG: bool,
>(
    ptr: *mut Address,
    length: usize,
    capacity: usize,
    factory_ptr: *mut libc::c_void,
) -> NewBuffer {
    if !ptr.is_null() {
        let ptr = ptr as *mut S;
        let buf = unsafe { Vec::<S>::from_raw_parts(ptr, length, capacity) };
        if cfg!(feature = "roots_breakdown") {
            record_roots(buf.len());
        }
        let factory: &mut F = unsafe { &mut *(factory_ptr as *mut F) };
        let kind = if WEAK {
            RootKind::YoungWeakCLDRoots
        } else if ALL_STRONG {
            RootKind::StrongCLDRoots
        } else {
            RootKind::YoungStrongCLDRoots
        };
        factory.create_process_roots_work(buf, kind);
    }
    let (ptr, _, capacity) = {
        // TODO: Use Vec::into_raw_parts() when the method is available.
        use std::mem::ManuallyDrop;
        let new_vec = Vec::with_capacity(F::BUFFER_SIZE);
        let mut me = ManuallyDrop::new(new_vec);
        (me.as_mut_ptr(), me.len(), me.capacity())
    };
    NewBuffer { ptr, capacity }
}

fn to_slots_closure_cld<
    S: Slot,
    F: RootsWorkFactory<S>,
    const WEAK: bool,
    const ALL_STRONG: bool,
>(
    factory: &mut F,
) -> SlotsClosure {
    SlotsClosure {
        func: report_slots_and_renew_buffer_cld::<S, F, WEAK, ALL_STRONG>,
        data: factory as *mut F as *mut libc::c_void,
    }
}

pub struct ScanClassLoaderDataGraphRoots<S: Slot, F: RootsWorkFactory<S>> {
    factory: F,
    _p: PhantomData<S>,
}

impl<S: Slot, F: RootsWorkFactory<S>> ScanClassLoaderDataGraphRoots<S, F> {
    pub fn new(factory: F) -> Self {
        Self {
            factory,
            _p: PhantomData,
        }
    }
}

impl<VM: VMBinding, F: RootsWorkFactory<VM::VMSlot>> GCWork<VM>
    for ScanClassLoaderDataGraphRoots<VM::VMSlot, F>
{
    fn do_work(&mut self, _worker: &mut GCWorker<VM>, mmtk: &'static MMTK<VM>) {
        let t = if cfg!(feature = "roots_breakdown") {
            Some(std::time::SystemTime::now())
        } else {
            None
        };
        let scan_all_strong_roots = mmtk.get_plan().current_gc_should_perform_class_unloading();
        if scan_all_strong_roots {
            unsafe {
                ((*UPCALLS).scan_class_loader_data_graph_roots)(
                    to_slots_closure_cld::<VM::VMSlot, F, false, true>(&mut self.factory),
                    to_slots_closure_cld::<VM::VMSlot, F, true, false>(&mut self.factory),
                    scan_all_strong_roots,
                );
            }
        } else {
            unsafe {
                ((*UPCALLS).scan_class_loader_data_graph_roots)(
                    to_slots_closure_cld::<VM::VMSlot, F, false, false>(&mut self.factory),
                    to_slots_closure_cld::<VM::VMSlot, F, true, false>(&mut self.factory),
                    scan_all_strong_roots,
                );
            }
        }
        if cfg!(feature = "roots_breakdown") {
            let ms = t.unwrap().elapsed().unwrap().as_micros() as f32 / 1000f32;
            report_roots("ClassLoaderDataGraph", ms);
        }
    }
}

pub struct ScanNewWeakHandleRoots<S: Slot, F: RootsWorkFactory<S>> {
    factory: F,
    _p: PhantomData<S>,
}

impl<S: Slot, F: RootsWorkFactory<S>> ScanNewWeakHandleRoots<S, F> {
    pub fn new(factory: F) -> Self {
        Self {
            factory,
            _p: PhantomData,
        }
    }
}

impl<VM: VMBinding, F: RootsWorkFactory<VM::VMSlot>> GCWork<VM>
    for ScanNewWeakHandleRoots<VM::VMSlot, F>
{
    fn do_work(&mut self, _worker: &mut GCWorker<VM>, _mmtk: &'static MMTK<VM>) {
        let t = if cfg!(feature = "roots_breakdown") {
            Some(std::time::SystemTime::now())
        } else {
            None
        };
        let mut new_roots = crate::NURSERY_WEAK_HANDLE_ROOTS.lock().unwrap();
        if cfg!(feature = "roots_breakdown") {
            record_roots(new_roots.len());
        }
        for slice in new_roots.chunks(F::BUFFER_SIZE) {
            let slice = unsafe { std::mem::transmute::<&[Address], &[VM::VMSlot]>(slice) };
            self.factory
                .create_process_roots_work(slice.to_vec(), RootKind::YoungWeakHandleRoots);
        }
        new_roots.clear();
        if cfg!(feature = "roots_breakdown") {
            let ms = t.unwrap().elapsed().unwrap().as_micros() as f32 / 1000f32;
            report_roots("NewWeakHandleRoots", ms);
        }
    }
}

#[allow(unused)]
pub struct ScanCodeCacheRoots<const COMPRESSED: bool, F: RootsWorkFactory<OpenJDKSlot<COMPRESSED>>>
{
    factory: F,
}

impl<const COMPRESSED: bool, F: RootsWorkFactory<OpenJDKSlot<COMPRESSED>>>
    ScanCodeCacheRoots<COMPRESSED, F>
{
    pub fn new(factory: F) -> Self {
        Self { factory }
    }
}

impl<const COMPRESSED: bool, F: RootsWorkFactory<OpenJDKSlot<COMPRESSED>>>
    GCWork<OpenJDK<COMPRESSED>> for ScanCodeCacheRoots<COMPRESSED, F>
{
    fn do_work(
        &mut self,
        worker: &mut GCWorker<OpenJDK<COMPRESSED>>,
        mmtk: &'static MMTK<OpenJDK<COMPRESSED>>,
    ) {
        let is_current_gc_nursery = mmtk
            .get_plan()
            .generational()
            .is_some_and(|gen| gen.is_current_gc_nursery());
        let lxr = mmtk
            .get_plan()
            .downcast_ref::<mmtk::plan::lxr::LXR<OpenJDK<COMPRESSED>>>();
        let is_lxr = lxr.is_some();
        let is_rc_pause = lxr.is_some_and(|lxr| lxr.current_pause() == Some(Pause::RefCount));
        let class_unloading_enabled = unsafe { crate::CLASS_UNLOADING_ENABLED } == 1;
        let pause = lxr.map(|x| x.current_pause()).unwrap_or(None);

        let mut slots = Vec::with_capacity(scanning::WORK_PACKET_CAPACITY);

        let mut nursery_slots = 0;
        let mut mature_slots = 0;

        let mut add_roots = |roots: &[Address]| {
            for root in roots {
                slots.push(OpenJDKSlot::<COMPRESSED>::from(*root));
                if slots.len() >= scanning::WORK_PACKET_CAPACITY {
                    self.factory.create_process_roots_work(
                        std::mem::take(&mut slots),
                        RootKind::YoungCodeCacheRoots,
                    );
                }
            }
        };

        let moves_object = mmtk.get_plan().current_gc_may_move_object();

        // nmethods which we need to fix relocations.
        // That includes all nmethods with moved children.
        // In nursery GCs, that means nmethods added since the previous GC.
        let mut nmethods_to_fix = Vec::new();

        {
            let mut mature = crate::MATURE_CODE_CACHE_ROOTS.lock().unwrap();

            // Only scan mature roots in full-heap collections.
            if !is_current_gc_nursery && !(is_lxr && (class_unloading_enabled || is_rc_pause)) {
                for (key, roots) in mature.iter() {
                    mature_slots += roots.len();
                    add_roots(roots);
                    if moves_object {
                        nmethods_to_fix.push(*key);
                    }
                }
            }

            if is_lxr
                && class_unloading_enabled
                && (pause == Some(Pause::FinalMark) || pause == Some(Pause::Full))
            {
                for key in mature.keys() {
                    nmethods_to_fix.push(*key);
                }
            }

            {
                let mut nursery = crate::NURSERY_CODE_CACHE_ROOTS.lock().unwrap();
                for (key, roots) in nursery.drain() {
                    nursery_slots += roots.len();
                    add_roots(&roots);
                    mature.insert(key, roots);
                    if moves_object {
                        nmethods_to_fix.push(key);
                    }
                }
            }
        }

        let num_nmethods = nmethods_to_fix.len();
        probe!(
            mmtk_openjdk,
            code_cache_roots,
            nursery_slots,
            mature_slots,
            num_nmethods
        );

        if !slots.is_empty() {
            self.factory
                .create_process_roots_work(slots, RootKind::Strong);
        }

        // When class unloading is pending, use forward-only mode: forward alive oops
        // but don't null dead oops. This allows has_dead_oop() during class unloading
        // to detect nmethods that should be unloaded.
        let forward_only = (is_lxr
            && class_unloading_enabled
            && (pause == Some(Pause::FinalMark) || pause == Some(Pause::Full)))
            || !is_current_gc_nursery;

        if moves_object && !nmethods_to_fix.is_empty() {
            // Note: If the current GC doesn't move objects at all, we don't need to fix relocation.
            // FIXME: Even during copying GC, some GC algorithms (such as Immix) don't move every
            // single object.  We only need to call `fix_oop_relocations` on nmethods that actually
            // have moved children.

            let packets = nmethods_to_fix
                .chunks(FixRelocations::NMETHODS_PER_PACKET)
                .map(|chunk| {
                    let nmethods = chunk.to_vec();
                    Box::new(FixRelocations {
                        nmethods,
                        forward_only,
                    }) as _
                })
                .collect();

            // fix_oop_relocations copies the forwarded oops from the nmethod headers back to
            // immediate operands in the machine code.  This can only be done after all fields of an
            // nmethod have been forwarded.
            let stage = if mmtk.get_plan().constraints().needs_forward_after_liveness {
                // For MarkCompact, we forward the children of nmethods in the transitive closure
                // starting with SecondRoots.  RefForwarding is the first safe place to call
                // fix_oop_relocations.
                WorkBucketStage::RefForwarding
            } else {
                // For scavenging GCs, the mmtk-openjdk binding reports the *slots* of nmethods as
                // roots. They will be traced at unspecified times during the Closure stage.
                // SoftRefClosure is the first safe place to call fix_oop_relocations.
                WorkBucketStage::FixRelocations
            };
            worker.scheduler().work_buckets[stage].bulk_add(packets);
        }
    }
}

extern "C" fn report_slots_and_renew_buffer_weak<S: Slot, F: RootsWorkFactory<S>>(
    ptr: *mut Address,
    length: usize,
    capacity: usize,
    factory_ptr: *mut libc::c_void,
) -> NewBuffer {
    if !ptr.is_null() {
        let ptr = ptr as *mut S;
        let buf = unsafe { Vec::<S>::from_raw_parts(ptr, length, capacity) };
        if cfg!(feature = "roots_breakdown") {
            record_roots(buf.len());
        }
        let factory: &mut F = unsafe { &mut *(factory_ptr as *mut F) };
        let kind = RootKind::Weak;
        factory.create_process_roots_work(buf, kind);
    }
    let (ptr, _, capacity) = {
        // TODO: Use Vec::into_raw_parts() when the method is available.
        use std::mem::ManuallyDrop;
        let new_vec = Vec::with_capacity(F::BUFFER_SIZE);
        let mut me = ManuallyDrop::new(new_vec);
        (me.as_mut_ptr(), me.len(), me.capacity())
    };
    NewBuffer { ptr, capacity }
}

fn to_slots_closure_weak<S: Slot, F: RootsWorkFactory<S>>(factory: &mut F) -> SlotsClosure {
    SlotsClosure {
        func: report_slots_and_renew_buffer_weak::<S, F>,
        data: factory as *mut F as *mut libc::c_void,
    }
}

pub struct ScanWeakProcessorRoots<S: Slot, F: RootsWorkFactory<S>> {
    factory: F,
    _p: PhantomData<S>,
}

impl<S: Slot, F: RootsWorkFactory<S>> ScanWeakProcessorRoots<S, F> {
    #[allow(unused)]
    pub fn new(factory: F) -> Self {
        Self {
            factory,
            _p: PhantomData,
        }
    }
}

impl<VM: VMBinding, F: RootsWorkFactory<VM::VMSlot>> GCWork<VM>
    for ScanWeakProcessorRoots<VM::VMSlot, F>
{
    fn do_work(&mut self, _worker: &mut GCWorker<VM>, mmtk: &'static MMTK<VM>) {
        let t = if cfg!(feature = "roots_breakdown") {
            Some(std::time::SystemTime::now())
        } else {
            None
        };
        // let scan_all_strong_roots = mmtk.get_plan().current_gc_should_perform_class_unloading();
        // assert!(scan_all_strong_roots);
        unsafe {
            ((*UPCALLS).scan_weak_processor_roots)(to_slots_closure_weak::<VM::VMSlot, F>(
                &mut self.factory,
            ));
        }
        if cfg!(feature = "roots_breakdown") {
            let ms = t.unwrap().elapsed().unwrap().as_micros() as f32 / 1000f32;
            report_roots("WeakProcessorRoots", ms);
        }
    }
}

struct FixRelocations {
    nmethods: Vec<Address>,
    forward_only: bool,
}

impl FixRelocations {
    /// The number of nmethods per packet. This value is selected for load-balancing.  Processing
    /// one nmethod is significantly more expensive than processing one slot.
    pub const NMETHODS_PER_PACKET: usize = 64;
}

impl<const COMPRESSED: bool> GCWork<OpenJDK<COMPRESSED>> for FixRelocations {
    fn do_work(
        &mut self,
        _worker: &mut GCWorker<OpenJDK<COMPRESSED>>,
        mmtk: &'static MMTK<OpenJDK<COMPRESSED>>,
    ) {
        let is_lxr = mmtk
            .get_plan()
            .downcast_ref::<mmtk::plan::lxr::LXR<OpenJDK<COMPRESSED>>>()
            .is_some();
        let is_rc_pause = mmtk
            .get_plan()
            .downcast_ref::<mmtk::plan::lxr::LXR<OpenJDK<COMPRESSED>>>()
            .is_some_and(|lxr| lxr.current_pause() == Some(Pause::RefCount));

        let num_nmethods = self.nmethods.len();
        let lxr_rc_pause = is_lxr && is_rc_pause;
        unsafe {
            ((*UPCALLS).fix_oop_relocations)(
                lxr_rc_pause,
                self.forward_only,
                self.nmethods.as_mut_ptr() as *mut _,
                num_nmethods,
            );
        }
        // probe!(mmtk_openjdk, fix_relocations, num_nmethods);
    }
}
