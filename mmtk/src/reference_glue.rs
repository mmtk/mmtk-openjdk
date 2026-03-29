use std::cell::UnsafeCell;
use std::marker::PhantomData;
use std::sync::atomic::AtomicBool;
use std::sync::Mutex;

use crate::abi::{InstanceRefKlass, Oop, ReferenceType};
use crate::slots::OpenJDKSlot;
use crate::OpenJDK;
use atomic::Ordering;
use mmtk::scheduler::{GCWork, GCWorker, ProcessEdgesWork, WorkBucketStage};
use mmtk::util::opaque_pointer::VMWorkerThread;
use mmtk::util::ObjectReference;
use mmtk::vm::slot::Slot;
use mmtk::vm::ReferenceGlue;
use mmtk::MMTK;

fn set_referent<const COMPRESSED: bool>(reff: ObjectReference, referent: Option<ObjectReference>) {
    let oop = Oop::from(reff);
    let slot = InstanceRefKlass::referent_address::<COMPRESSED>(oop);
    mmtk::plan::lxr::record_slot_for_validation(slot, referent);
    slot.store(referent)
}

fn get_referent<const COMPRESSED: bool>(object: ObjectReference) -> Option<ObjectReference> {
    let oop = Oop::from(object);
    InstanceRefKlass::referent_address::<COMPRESSED>(oop).load()
}

fn get_next_reference_slot<const COMPRESSED: bool>(
    object: ObjectReference,
) -> OpenJDKSlot<COMPRESSED> {
    let oop = Oop::from(object);
    InstanceRefKlass::discovered_address::<COMPRESSED>(oop)
}

fn get_next_reference<const COMPRESSED: bool>(object: ObjectReference) -> Option<ObjectReference> {
    get_next_reference_slot::<COMPRESSED>(object).load()
}

fn set_next_reference<const COMPRESSED: bool>(
    object: ObjectReference,
    next: Option<ObjectReference>,
) {
    let slot = get_next_reference_slot::<COMPRESSED>(object);
    mmtk::plan::lxr::record_slot_for_validation(slot, next);
    slot.store(next)
}

pub struct VMReferenceGlue {}

impl<const COMPRESSED: bool> ReferenceGlue<OpenJDK<COMPRESSED>> for VMReferenceGlue {
    type FinalizableType = ObjectReference;

    fn set_referent(_reff: ObjectReference, _referent: ObjectReference) {
        unreachable!();
    }
    fn get_referent(_object: ObjectReference) -> Option<ObjectReference> {
        unreachable!();
    }
    fn enqueue_references(_references: &[ObjectReference], _tls: VMWorkerThread) {}
    fn clear_referent(_new_reference: ObjectReference) {}
}

pub struct DiscoveredList {
    head: UnsafeCell<Option<ObjectReference>>,
    _rt: ReferenceType,
}

impl DiscoveredList {
    fn new(rt: ReferenceType) -> Self {
        Self {
            head: UnsafeCell::new(ObjectReference::NULL),
            _rt: rt,
        }
    }

    pub fn add<const COMPRESSED: bool>(
        &self,
        reference: ObjectReference,
        referent: ObjectReference,
    ) {
        // Keep reference and referent alive during SATB
        crate::singleton::<COMPRESSED>()
            .get_plan()
            .discover_reference(reference, referent);
        // Add to the corresponding list
        // Note that the list is a singly-linked list, and the tail object should point to itself.
        let oop = Oop::from(reference);
        let head = unsafe { *self.head.get() };
        let addr = InstanceRefKlass::discovered_address::<COMPRESSED>(oop);
        let next_discovered = if head.is_none() {
            reference
        } else {
            head.unwrap()
        };
        // debug_assert!(!next_discovered.is_null());
        if addr
            .compare_exchange(
                ObjectReference::NULL,
                Some(next_discovered),
                Ordering::SeqCst,
                Ordering::SeqCst,
            )
            .is_ok()
        {
            unsafe {
                *self.head.get() = Some(reference);
            }
        }
        debug_assert!(!get_next_reference::<COMPRESSED>(reference).is_none());
    }
}

pub struct DiscoveredLists {
    pub soft: Vec<DiscoveredList>,
    pub weak: Vec<DiscoveredList>,
    pub r#final: Vec<DiscoveredList>,
    pub phantom: Vec<DiscoveredList>,
    allow_discover: AtomicBool,
}

unsafe impl Sync for DiscoveredLists {}

impl DiscoveredLists {
    pub fn new() -> Self {
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
        let workers = with_singleton!(|singleton| singleton.scheduler.num_workers());
        let build_lists = |rt| (0..workers).map(|_| DiscoveredList::new(rt)).collect();
        Self {
            soft: build_lists(ReferenceType::Soft),
            weak: build_lists(ReferenceType::Weak),
            r#final: build_lists(ReferenceType::Final),
            phantom: build_lists(ReferenceType::Phantom),
            allow_discover: AtomicBool::new(true),
        }
    }

    pub fn enable_discover(&self) {
        self.allow_discover.store(true, Ordering::SeqCst)
    }

    pub fn allow_discover(&self) -> bool {
        self.allow_discover.load(Ordering::SeqCst)
    }

    pub fn is_discovered<const COMPRESSED: bool>(&self, reference: ObjectReference) -> bool {
        !get_next_reference::<COMPRESSED>(reference).is_none()
    }

    pub fn get_by_rt_and_index(&self, rt: ReferenceType, index: usize) -> &DiscoveredList {
        match rt {
            ReferenceType::Soft => &self.soft[index],
            ReferenceType::Weak => &self.weak[index],
            ReferenceType::Phantom => &self.phantom[index],
            ReferenceType::Final => &self.r#final[index],
            _ => unimplemented!(),
        }
    }

    pub fn get(&self, rt: ReferenceType) -> &DiscoveredList {
        let id = mmtk::scheduler::worker::current_worker_ordinal().unwrap();
        self.get_by_rt_and_index(rt, id)
    }

    fn process_lists<E: ProcessEdgesWork, const COMPRESSED: bool>(
        &self,
        worker: &mut GCWorker<E::VM>,
        rt: ReferenceType,
        lists: &[DiscoveredList],
        clear: bool,
    ) {
        let mut packets = vec![];
        let mut packets_step2 = vec![];
        if rt == ReferenceType::Weak || rt == ReferenceType::Soft {
            for i in 0..lists.len() {
                let head = unsafe { *lists[i].head.get() };
                if clear {
                    unsafe { *lists[i].head.get() = ObjectReference::NULL };
                }
                if head.is_none() {
                    continue;
                }
                let w = ProcessDiscoveredListForSoftAndWeakRefs::<_, COMPRESSED> {
                    head: head.unwrap(),
                    rt,
                    _p: PhantomData::<E>,
                };
                packets.push(Box::new(w) as Box<dyn GCWork<E::VM>>);
                let w2 = ProcessDeadRefs::<_, COMPRESSED> {
                    list_index: i,
                    head: head.unwrap(),
                    rt,
                    _p: PhantomData::<E>,
                };
                packets_step2.push(Box::new(w2) as Box<dyn GCWork<E::VM>>);
            }
        } else {
            for i in 0..lists.len() {
                let head = unsafe { *lists[i].head.get() };
                if clear {
                    unsafe { *lists[i].head.get() = ObjectReference::NULL };
                }
                if head.is_none() {
                    continue;
                }
                let w = ProcessDiscoveredList::<_, COMPRESSED> {
                    list_index: i,
                    head: head.unwrap(),
                    rt,
                    _p: PhantomData::<E>,
                };
                packets.push(Box::new(w) as Box<dyn GCWork<E::VM>>);
            }
        }
        worker.scheduler().work_buckets[WorkBucketStage::Unconstrained].bulk_add(packets);
        if rt == ReferenceType::Weak || rt == ReferenceType::Soft {
            worker.scheduler().work_buckets[WorkBucketStage::PhantomRefClosure]
                .bulk_add(packets_step2);
        }
    }

    pub fn reconsider_soft_refs<E: ProcessEdgesWork>(&self, _worker: &mut GCWorker<E::VM>) {}

    pub fn process_soft_weak_final_refs<E: ProcessEdgesWork, const COMPRESSED: bool>(
        &self,
        worker: &mut GCWorker<E::VM>,
    ) {
        self.allow_discover.store(false, Ordering::SeqCst);
        if !*worker.mmtk.get_options().no_reference_types {
            self.process_lists::<E, COMPRESSED>(worker, ReferenceType::Soft, &self.soft, true);
            self.process_lists::<E, COMPRESSED>(worker, ReferenceType::Weak, &self.weak, true);
        }
        if !*worker.mmtk.get_options().no_finalizer {
            self.process_lists::<E, COMPRESSED>(worker, ReferenceType::Final, &self.r#final, false);
        }
    }

    pub fn resurrect_final_refs<E: ProcessEdgesWork, const COMPRESSED: bool>(
        &self,
        worker: &mut GCWorker<E::VM>,
    ) {
        assert!(!*worker.mmtk.get_options().no_finalizer);
        let lists = &self.r#final;
        let mut packets = vec![];
        for i in 0..lists.len() {
            let head = unsafe { *lists[i].head.get() };
            unsafe { *lists[i].head.get() = ObjectReference::NULL };
            if head.is_none() {
                continue;
            }
            let w = ResurrectFinalizables::<_, COMPRESSED> {
                list_index: i,
                head: head.unwrap(),
                _p: PhantomData::<E>,
            };
            packets.push(Box::new(w) as Box<dyn GCWork<E::VM>>);
        }
        worker.scheduler().work_buckets[WorkBucketStage::Unconstrained].bulk_add(packets);
    }

    pub fn process_phantom_refs<E: ProcessEdgesWork, const COMPRESSED: bool>(
        &self,
        worker: &mut GCWorker<E::VM>,
    ) {
        assert!(!*worker.mmtk.get_options().no_reference_types);
        self.process_lists::<E, COMPRESSED>(worker, ReferenceType::Phantom, &self.phantom, true);
    }
}

lazy_static! {
    pub static ref LOCK: Mutex<()> = Mutex::new(());
    pub static ref DISCOVERED_LISTS: DiscoveredLists = DiscoveredLists::new();
}

pub struct ProcessDiscoveredList<E: ProcessEdgesWork, const COMPRESSED: bool> {
    list_index: usize,
    head: ObjectReference,
    rt: ReferenceType,
    _p: PhantomData<E>,
}

impl<E: ProcessEdgesWork, const COMPRESSED: bool> GCWork<E::VM>
    for ProcessDiscoveredList<E, COMPRESSED>
{
    fn do_work(&mut self, worker: &mut GCWorker<E::VM>, mmtk: &'static MMTK<E::VM>) {
        let mut trace = E::new(vec![], false, mmtk, WorkBucketStage::Unconstrained);
        trace.set_worker(worker);
        let retain = self.rt == ReferenceType::Soft
            && !mmtk.is_emergency_collection()
            && !mmtk.is_user_triggered_collection();

        let new_list = iterate_list::<_, COMPRESSED>(self.head, |reference| {
            debug_assert!(
                get_next_reference::<COMPRESSED>(reference).is_none(),
                "next must be null. ref={:?} {:?} bin={}",
                reference,
                self.rt,
                self.list_index
            );
            let reference = trace.trace_object(reference);
            let referent = get_referent::<COMPRESSED>(reference);
            if referent.is_none() {
                // Remove from the discovered list
                return DiscoveredListIterationResult::Remove;
            }
            let referent = referent.unwrap();
            if referent.is_reachable() || retain {
                // Keep this referent
                let forwarded = trace.trace_object(referent);
                debug_assert!(forwarded.get_forwarded_object().is_none());
                debug_assert!(reference.get_forwarded_object().is_none());
                set_referent::<COMPRESSED>(reference, Some(forwarded));
                // Remove from the discovered list
                return DiscoveredListIterationResult::Remove;
            } else {
                if self.rt != ReferenceType::Final {
                    // Clear this referent
                    set_referent::<COMPRESSED>(reference, ObjectReference::NULL);
                    // set_referent(reference, ObjectReference::NULL);
                }
                // Keep the reference
                return DiscoveredListIterationResult::Enqueue(reference);
            }
        });
        // Flush the list to the Universe::pending_list
        if let Some((head, tail)) = new_list {
            // debug_assert!(!head.is_null() && !tail.is_null());
            // debug_assert_eq!(ObjectReference::NULL, get_next_reference(tail));
            if self.rt == ReferenceType::Final {
                let slot = DISCOVERED_LISTS.r#final[self.list_index].head.get();
                unsafe { *slot = Some(head) };
            } else {
                let old_head = unsafe { ((*crate::UPCALLS).swap_reference_pending_list)(head) };
                set_next_reference::<COMPRESSED>(tail, Some(old_head));
            }
        } else {
            if self.rt == ReferenceType::Final {
                let slot = DISCOVERED_LISTS.r#final[self.list_index].head.get();
                unsafe { *slot = ObjectReference::NULL };
            }
        }
        trace.process_slots();
        trace.flush();
    }
}

pub struct ProcessDiscoveredListForSoftAndWeakRefs<E: ProcessEdgesWork, const COMPRESSED: bool> {
    head: ObjectReference,
    rt: ReferenceType,
    _p: PhantomData<E>,
}

impl<E: ProcessEdgesWork, const COMPRESSED: bool> GCWork<E::VM>
    for ProcessDiscoveredListForSoftAndWeakRefs<E, COMPRESSED>
{
    fn do_work(&mut self, worker: &mut GCWorker<E::VM>, mmtk: &'static MMTK<E::VM>) {
        let mut trace = E::new(vec![], false, mmtk, WorkBucketStage::Unconstrained);
        trace.set_worker(worker);
        let retain = self.rt == ReferenceType::Soft
            && !mmtk.is_emergency_collection()
            && !mmtk.is_user_triggered_collection();

        let mut reference_opt = Some(self.head);

        while let Some(reference) = reference_opt {
            let next_ref = get_next_reference::<COMPRESSED>(reference);
            let next_ref = next_ref
                .map(|x| x.get_forwarded_object())
                .flatten()
                .or_else(|| next_ref);
            if let Some(o) = next_ref {
                debug_assert!(o.get_forwarded_object().is_none());
            }
            // Reaches the end of the list?
            let end_of_list = next_ref == Some(reference) || next_ref.is_none();
            reference_opt = if end_of_list { None } else { next_ref };

            let reference = trace.trace_object(reference);
            let referent = get_referent::<COMPRESSED>(reference);
            if referent.is_none() {
                continue;
            }
            let referent = referent.unwrap();
            if referent.is_reachable() || retain {
                // Keep this referent
                let forwarded = trace.trace_object(referent);
                debug_assert!(forwarded.get_forwarded_object().is_none());
                debug_assert!(reference.get_forwarded_object().is_none());
                set_referent::<COMPRESSED>(reference, Some(forwarded));
            }
        }
        trace.process_slots();
        trace.flush();
    }
}

pub struct ProcessDeadRefs<E: ProcessEdgesWork, const COMPRESSED: bool> {
    list_index: usize,
    head: ObjectReference,
    rt: ReferenceType,
    _p: PhantomData<E>,
}

impl<E: ProcessEdgesWork, const COMPRESSED: bool> GCWork<E::VM> for ProcessDeadRefs<E, COMPRESSED> {
    fn do_work(&mut self, worker: &mut GCWorker<E::VM>, mmtk: &'static MMTK<E::VM>) {
        let mut trace = E::new(vec![], false, mmtk, WorkBucketStage::Unconstrained);
        trace.set_worker(worker);
        let retain = self.rt == ReferenceType::Soft
            && !mmtk.is_emergency_collection()
            && !mmtk.is_user_triggered_collection();

        let new_list = iterate_list::<_, COMPRESSED>(self.head, |reference| {
            debug_assert!(
                get_next_reference::<COMPRESSED>(reference).is_none(),
                "next must be null. ref={:?} {:?} bin={}",
                reference,
                self.rt,
                self.list_index
            );
            let referent = get_referent::<COMPRESSED>(reference);
            if referent.is_none() {
                // Remove from the discovered list
                return DiscoveredListIterationResult::Remove;
            }
            let referent = referent.unwrap();
            if referent.is_reachable() || retain {
                // Keep this referent
                let forwarded = referent.get_forwarded_object().unwrap_or(referent);
                debug_assert!(forwarded.get_forwarded_object().is_none());
                debug_assert!(reference.get_forwarded_object().is_none());
                if forwarded != referent {
                    set_referent::<COMPRESSED>(reference, Some(forwarded));
                }
                // Remove from the discovered list
                return DiscoveredListIterationResult::Remove;
            } else {
                if self.rt != ReferenceType::Final {
                    // Clear this referent
                    set_referent::<COMPRESSED>(reference, ObjectReference::NULL);
                    // set_referent(reference, ObjectReference::NULL);
                }
                // Keep the reference
                return DiscoveredListIterationResult::Enqueue(reference);
            }
        });
        // Flush the list to the Universe::pending_list
        if let Some((head, tail)) = new_list {
            // debug_assert!(!head.is_null() && !tail.is_null());
            // debug_assert_eq!(ObjectReference::NULL, get_next_reference(tail));
            if self.rt == ReferenceType::Final {
                let slot = DISCOVERED_LISTS.r#final[self.list_index].head.get();
                unsafe { *slot = Some(head) };
            } else {
                let old_head = unsafe { ((*crate::UPCALLS).swap_reference_pending_list)(head) };
                set_next_reference::<COMPRESSED>(tail, Some(old_head));
            }
        } else {
            if self.rt == ReferenceType::Final {
                let slot = DISCOVERED_LISTS.r#final[self.list_index].head.get();
                unsafe { *slot = ObjectReference::NULL };
            }
        }
        // assert!(trace.is_empty());
        trace.process_slots();
        trace.flush();
    }
}

pub struct ResurrectFinalizables<E: ProcessEdgesWork, const COMPRESSED: bool> {
    list_index: usize,
    head: ObjectReference,
    _p: PhantomData<E>,
}

impl<E: ProcessEdgesWork, const COMPRESSED: bool> GCWork<E::VM>
    for ResurrectFinalizables<E, COMPRESSED>
{
    fn do_work(&mut self, worker: &mut GCWorker<E::VM>, mmtk: &'static MMTK<E::VM>) {
        let mut trace = E::new(vec![], false, mmtk, WorkBucketStage::Unconstrained);
        trace.set_worker(worker);
        let new_list = iterate_list::<_, COMPRESSED>(self.head, |reference| {
            let reference = trace.trace_object(reference);
            let referent = get_referent::<COMPRESSED>(reference);
            let forwarded = match referent {
                Some(o) => Some(trace.trace_object(o)),
                None => None,
            };
            set_referent::<COMPRESSED>(reference, forwarded);
            if let Some(forwarded) = forwarded {
                debug_assert!(forwarded.get_forwarded_object().is_none());
            }
            return DiscoveredListIterationResult::Enqueue(reference);
        });

        if let Some((head, tail)) = new_list {
            // debug_assert!(!head.is_null() && !tail.is_null());
            // debug_assert_eq!(ObjectReference::NULL, get_next_reference(tail));
            let slot = DISCOVERED_LISTS.r#final[self.list_index].head.get();
            unsafe { *slot = ObjectReference::NULL };
            let old_head = unsafe { ((*crate::UPCALLS).swap_reference_pending_list)(head) };
            set_next_reference::<COMPRESSED>(tail, Some(old_head));
        }
        trace.process_slots();
        trace.flush();
    }
}

enum DiscoveredListIterationResult {
    Remove,
    Enqueue(ObjectReference),
}

fn iterate_list<
    F: FnMut(ObjectReference) -> DiscoveredListIterationResult,
    const COMPRESSED: bool,
>(
    head: ObjectReference,
    mut visitor: F,
) -> Option<(ObjectReference, ObjectReference)> {
    let mut new_head: Option<ObjectReference> = None;
    let mut new_tail: Option<ObjectReference> = None;
    let mut reference = head;
    loop {
        // debug_assert!(reference.is_live());
        // Update reference forwarding pointer
        if let Some(forwarded) = reference.get_forwarded_object() {
            reference = forwarded;
        }
        debug_assert!(reference.get_forwarded_object().is_none());
        debug_assert!(reference.is_reachable());
        // Update next_ref forwarding pointer
        let next_ref = get_next_reference::<COMPRESSED>(reference);
        let next_ref = next_ref
            .map(|x| x.get_forwarded_object())
            .flatten()
            .or_else(|| next_ref);
        if let Some(o) = next_ref {
            debug_assert!(o.get_forwarded_object().is_none());
        }
        // Reaches the end of the list?
        let end_of_list = next_ref == Some(reference) || next_ref.is_none();
        // Remove `reference` from current list
        set_next_reference::<COMPRESSED>(reference, ObjectReference::NULL);
        if let Some(forwarded_ref) = reference.get_forwarded_object() {
            set_next_reference::<COMPRESSED>(forwarded_ref, ObjectReference::NULL);
        }
        // Process reference
        let result = visitor(reference);
        match result {
            DiscoveredListIterationResult::Remove => {}
            DiscoveredListIterationResult::Enqueue(reference) => {
                // Add to new list
                if let Some(new_head) = new_head {
                    set_next_reference::<COMPRESSED>(reference, Some(new_head));
                } else {
                    new_tail = Some(reference);
                }
                new_head = Some(reference);
            }
        }
        // Reached the end of the list?
        if end_of_list {
            break;
        }
        // Move to next
        reference = next_ref.unwrap();
    }
    if new_head.is_none() {
        None
    } else {
        Some((new_head.unwrap(), new_tail.unwrap()))
    }
}
