use super::abi::*;
use super::UPCALLS;
use crate::reference_glue::DISCOVERED_LISTS;
use crate::OpenJDKSlot;
use mmtk::util::opaque_pointer::*;
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::slot::Slot;
use mmtk::vm::ObjectKind;
use mmtk::vm::SlotVisitor;
use std::cell::UnsafeCell;
use std::{mem, slice};

type S<const COMPRESSED: bool> = OpenJDKSlot<COMPRESSED>;

trait OopIterate: Sized {
    fn oop_iterate<const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut impl SlotVisitor<OpenJDKSlot<COMPRESSED>>,
    );
}

impl OopIterate for OopMapBlock {
    fn oop_iterate<const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut impl SlotVisitor<S<COMPRESSED>>,
    ) {
        let start = oop.get_field_address(self.offset);
        for i in 0..self.count as usize {
            let slot = (start + (i << S::<COMPRESSED>::LOG_BYTES_IN_SLOT)).into();
            closure.visit_slot(slot, false);
        }
    }
}

impl OopIterate for InstanceKlass {
    fn oop_iterate<const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut impl SlotVisitor<S<COMPRESSED>>,
    ) {
        if closure.should_follow_clds() {
            do_klass::<_, _, COMPRESSED>(oop.klass::<COMPRESSED>(), closure);
        }
        let oop_maps = self.nonstatic_oop_maps();
        for map in oop_maps {
            map.oop_iterate::<COMPRESSED>(oop, closure)
        }
    }
}

impl OopIterate for InstanceMirrorKlass {
    fn oop_iterate<const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut impl SlotVisitor<S<COMPRESSED>>,
    ) {
        self.instance_klass.oop_iterate::<COMPRESSED>(oop, closure);
        if closure.should_follow_clds() {
            let klass = unsafe {
                (oop.start() + *crate::JAVA_LANG_CLASS_KLASS_OFFSET_IN_BYTES).load::<*mut Klass>()
            };
            if !klass.is_null() {
                let klass = unsafe { &mut *klass };
                if klass.is_instance_klass()
                    && (unsafe { klass.cast::<InstanceKlass>().is_anonymous() })
                {
                    do_cld::<_, _, COMPRESSED>(&klass.class_loader_data, closure)
                } else {
                    do_klass::<_, _, COMPRESSED>(klass, closure)
                }
            }
        }

        // static fields
        let start = Self::start_of_static_fields(oop);
        let len = Self::static_oop_field_count(oop);
        if COMPRESSED {
            let start: *const NarrowOop = start.to_ptr::<NarrowOop>();
            let slice = unsafe { slice::from_raw_parts(start, len as _) };
            for narrow_oop in slice {
                closure.visit_slot(narrow_oop.slot().into(), false);
            }
        } else {
            let start: *const Oop = start.to_ptr::<Oop>();
            let slice = unsafe { slice::from_raw_parts(start, len as _) };
            for oop in slice {
                closure.visit_slot(Address::from_ref(oop as &Oop).into(), false);
            }
        }
    }
}

impl OopIterate for InstanceClassLoaderKlass {
    fn oop_iterate<const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut impl SlotVisitor<S<COMPRESSED>>,
    ) {
        self.instance_klass.oop_iterate::<COMPRESSED>(oop, closure);
        if closure.should_follow_clds() {
            let cld = unsafe {
                (oop.start() + *crate::JAVA_LANG_CLASSLOADER_LOADER_DATA_OFFSET)
                    .load::<*mut ClassLoaderData>()
            };
            if !cld.is_null() {
                do_cld::<_, _, COMPRESSED>(unsafe { &*cld }, closure);
            }
        }
    }
}

impl OopIterate for ObjArrayKlass {
    fn oop_iterate<const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut impl SlotVisitor<S<COMPRESSED>>,
    ) {
        if closure.should_follow_clds() {
            do_klass::<_, _, COMPRESSED>(oop.klass::<COMPRESSED>(), closure);
        }
        let array = unsafe { oop.as_array_oop() };
        if COMPRESSED {
            for narrow_oop in unsafe { array.data::<NarrowOop, COMPRESSED>(BasicType::T_OBJECT) } {
                closure.visit_slot(narrow_oop.slot().into(), false);
            }
        } else {
            for oop in unsafe { array.data::<Oop, COMPRESSED>(BasicType::T_OBJECT) } {
                closure.visit_slot(Address::from_ref(oop as &Oop).into(), false);
            }
        }
    }
}

impl OopIterate for TypeArrayKlass {
    fn oop_iterate<const COMPRESSED: bool>(
        &self,
        _oop: Oop,
        _closure: &mut impl SlotVisitor<S<COMPRESSED>>,
    ) {
        // Performance tweak: We skip processing the klass pointer since all
        // TypeArrayKlasses are guaranteed processed via the null class loader.
    }
}

impl OopIterate for InstanceRefKlass {
    fn oop_iterate<const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut impl SlotVisitor<S<COMPRESSED>>,
    ) {
        use crate::abi::*;
        self.instance_klass.oop_iterate::<COMPRESSED>(oop, closure);

        let disable_discovery = !closure.should_discover_references();

        if Self::should_discover_refs::<COMPRESSED>(
            self.instance_klass.reference_type,
            disable_discovery,
        ) {
            match self.instance_klass.reference_type {
                ReferenceType::None => {
                    panic!("oop_iterate on InstanceRefKlass with reference_type as None")
                }
                rt => {
                    if !Self::discover_reference::<COMPRESSED>(oop, rt) {
                        Self::process_ref_as_strong(oop, closure)
                    }
                }
            }
        } else {
            Self::process_ref_as_strong(oop, closure);
        }
    }
}

impl InstanceRefKlass {
    fn should_discover_refs<const COMPRESSED: bool>(
        rt: ReferenceType,
        disable_discovery: bool,
    ) -> bool {
        if disable_discovery {
            return false;
        }
        if *crate::singleton::<COMPRESSED>().get_options().no_finalizer
            && rt == ReferenceType::Final
        {
            return false;
        }
        if *crate::singleton::<COMPRESSED>()
            .get_options()
            .no_reference_types
            && rt != ReferenceType::Final
        {
            return false;
        }
        true
    }
    fn process_ref_as_strong<const COMPRESSED: bool>(
        oop: Oop,
        closure: &mut impl SlotVisitor<S<COMPRESSED>>,
    ) {
        let referent_addr = Self::referent_address::<COMPRESSED>(oop);
        closure.visit_slot(referent_addr, false);
        let discovered_addr = Self::discovered_address::<COMPRESSED>(oop);
        closure.visit_slot(discovered_addr, false);
    }
    fn discover_reference<const COMPRESSED: bool>(oop: Oop, rt: ReferenceType) -> bool {
        // Do not discover new refs during reference processing.
        if !DISCOVERED_LISTS.allow_discover() {
            return false;
        }
        // Do not discover if the referent is live.
        let addr = InstanceRefKlass::referent_address::<COMPRESSED>(oop);
        let Some(referent) = addr.load() else {
            return false;
        };
        // Skip live or null referents
        if referent.is_reachable() {
            return false;
        }
        // Skip young referents
        let reference: ObjectReference = oop.into();
        if !crate::singleton::<COMPRESSED>()
            .get_plan()
            .should_process_reference(reference, referent)
        {
            return false;
        }
        if rt == ReferenceType::Final && DISCOVERED_LISTS.is_discovered::<COMPRESSED>(reference) {
            return false;
        }
        // Add to reference list
        DISCOVERED_LISTS
            .get(rt)
            .add::<COMPRESSED>(reference, referent);
        true
    }
}

#[allow(unused)]
fn oop_iterate_slow<const COMPRESSED: bool, V: SlotVisitor<S<COMPRESSED>>>(
    oop: Oop,
    closure: &mut V,
    tls: OpaquePointer,
) {
    unsafe {
        CLOSURE.with(|x| *x.get() = closure as *mut V as *mut u8);
        ((*UPCALLS).scan_object)(
            mem::transmute::<*const unsafe extern "C" fn(Address), *mut libc::c_void>(
                scan_object_fn::<COMPRESSED, V> as *const unsafe extern "C" fn(slot: Address),
            ),
            mem::transmute::<&OopDesc, ObjectReference>(oop),
            tls,
            closure.should_follow_clds(),
            closure.should_claim_clds(),
        );
    }
}

fn oop_iterate<const COMPRESSED: bool>(
    oop: Oop,
    closure: &mut impl SlotVisitor<S<COMPRESSED>>,
    klass: Option<Address>,
) {
    let klass = if let Some(klass) = klass {
        unsafe { &*(klass.as_usize() as *const Klass) }
    } else {
        oop.klass::<COMPRESSED>()
    };
    let klass_id = klass.kind;
    assert!(
        klass_id as i32 >= 0 && (klass_id as i32) < KlassKind::Unknown as i32,
        "Invalid klass-id: {:x} for oop: {:x}",
        klass_id as i32,
        unsafe { mem::transmute::<Oop, ObjectReference>(oop) }
    );
    match klass_id {
        KlassKind::Instance => {
            let instance_klass = unsafe { klass.cast::<InstanceKlass>() };
            instance_klass.oop_iterate::<COMPRESSED>(oop, closure);
        }
        KlassKind::InstanceClassLoader => {
            let instance_klass = unsafe { klass.cast::<InstanceClassLoaderKlass>() };
            instance_klass.oop_iterate::<COMPRESSED>(oop, closure);
        }
        KlassKind::InstanceMirror => {
            let instance_klass = unsafe { klass.cast::<InstanceMirrorKlass>() };
            instance_klass.oop_iterate::<COMPRESSED>(oop, closure);
        }
        KlassKind::ObjArray => {
            let array_klass = unsafe { klass.cast::<ObjArrayKlass>() };
            array_klass.oop_iterate::<COMPRESSED>(oop, closure);
        }
        KlassKind::TypeArray => {
            // Skip scanning primitive arrays as they contain no reference fields.
        }
        KlassKind::InstanceRef => {
            let instance_klass = unsafe { klass.cast::<InstanceRefKlass>() };
            instance_klass.oop_iterate::<COMPRESSED>(oop, closure);
        }
        KlassKind::InstanceStackChunk => {
            unreachable!("StackChunkOop not supported!")
        }
        KlassKind::Unknown => {
            unreachable!("Unknown KlassKind")
        }
    }
}

fn do_cld<S: Slot, V: SlotVisitor<S>, const COMPRESSED: bool>(
    cld: &ClassLoaderData,
    closure: &mut V,
) {
    if !closure.should_follow_clds() {
        return;
    }
    cld.oops_do::<_, _, COMPRESSED>(closure)
}

fn do_klass<S: Slot, V: SlotVisitor<S>, const COMPRESSED: bool>(klass: &Klass, closure: &mut V) {
    if !closure.should_follow_clds() {
        return;
    }
    do_cld::<_, _, COMPRESSED>(klass.class_loader_data, closure)
}

pub fn is_obj_array<const COMPRESSED: bool>(oop: Oop) -> bool {
    oop.klass::<COMPRESSED>().kind == KlassKind::ObjArray
}

pub fn is_val_array<const COMPRESSED: bool>(oop: Oop) -> bool {
    oop.klass::<COMPRESSED>().kind == KlassKind::TypeArray
}

pub fn get_obj_kind<const COMPRESSED: bool>(oop: Oop) -> ObjectKind {
    let cls_id = oop.klass::<COMPRESSED>().kind;
    match cls_id {
        KlassKind::TypeArray => ObjectKind::ValArray,
        KlassKind::ObjArray => {
            ObjectKind::ObjArray(unsafe { oop.as_array_oop().length::<COMPRESSED>() as u32 })
        }
        _ => ObjectKind::Scalar,
    }
}

pub fn obj_array_data<const COMPRESSED: bool>(oop: Oop) -> crate::OpenJDKSlotRange<COMPRESSED> {
    unsafe {
        let array = oop.as_array_oop();
        array.slice::<COMPRESSED>(BasicType::T_OBJECT)
    }
}

thread_local! {
    static CLOSURE: UnsafeCell<*mut u8> = const { UnsafeCell::new(std::ptr::null_mut()) };
}

pub unsafe extern "C" fn scan_object_fn<
    const COMPRESSED: bool,
    V: SlotVisitor<OpenJDKSlot<COMPRESSED>>,
>(
    slot: Address,
) {
    let ptr: *mut u8 = CLOSURE.with(|x| *x.get());
    let closure = &mut *(ptr as *mut V);
    closure.visit_slot(slot.into(), false);
}

pub fn scan_object<const COMPRESSED: bool>(
    object: ObjectReference,
    closure: &mut impl SlotVisitor<S<COMPRESSED>>,
    _tls: VMWorkerThread,
) {
    unsafe {
        oop_iterate::<COMPRESSED>(
            mem::transmute::<ObjectReference, &OopDesc>(object),
            closure,
            None,
        )
    }
}

pub fn scan_object_with_klass<const COMPRESSED: bool>(
    object: ObjectReference,
    closure: &mut impl SlotVisitor<S<COMPRESSED>>,
    _tls: VMWorkerThread,
    klass: Address,
) {
    unsafe {
        oop_iterate::<COMPRESSED>(
            mem::transmute::<ObjectReference, &OopDesc>(object),
            closure,
            Some(klass),
        )
    }
}
