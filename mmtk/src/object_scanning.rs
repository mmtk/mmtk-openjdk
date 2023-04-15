use super::abi::*;
use super::UPCALLS;
use mmtk::util::opaque_pointer::*;
use mmtk::util::{Address, ObjectReference};
use mmtk::vm::edge_shape::Edge;
use mmtk::vm::EdgeVisitor;
use std::cell::UnsafeCell;
use std::{mem, slice};

trait OopIterate: Sized {
    fn oop_iterate<E: Edge, V: EdgeVisitor<E>, const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut V,
    );
}

impl OopIterate for OopMapBlock {
    fn oop_iterate<E: Edge, V: EdgeVisitor<E>, const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut V,
    ) {
        let log_bytes_in_oop = if COMPRESSED { 2 } else { 3 };
        let start = oop.get_field_address(self.offset);
        for i in 0..self.count as usize {
            let edge = E::from_address(start + (i << log_bytes_in_oop));
            closure.visit_edge(edge);
        }
    }
}

impl OopIterate for InstanceKlass {
    fn oop_iterate<E: Edge, V: EdgeVisitor<E>, const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut V,
    ) {
        let oop_maps = self.nonstatic_oop_maps();
        for map in oop_maps {
            map.oop_iterate::<E, V, COMPRESSED>(oop, closure)
        }
    }
}

impl OopIterate for InstanceMirrorKlass {
    fn oop_iterate<E: Edge, V: EdgeVisitor<E>, const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut V,
    ) {
        self.instance_klass
            .oop_iterate::<_, _, COMPRESSED>(oop, closure);

        // static fields
        let start = Self::start_of_static_fields(oop);
        let len = Self::static_oop_field_count(oop);
        if COMPRESSED {
            let start: *const NarrowOop = start.to_ptr::<NarrowOop>();
            let slice = unsafe { slice::from_raw_parts(start, len as _) };
            for narrow_oop in slice {
                closure.visit_edge(E::from_address(narrow_oop.slot()));
            }
        } else {
            let start: *const Oop = start.to_ptr::<Oop>();
            let slice = unsafe { slice::from_raw_parts(start, len as _) };
            for oop in slice {
                closure.visit_edge(E::from_address(Address::from_ref(oop as &Oop)));
            }
        }
    }
}

impl OopIterate for InstanceClassLoaderKlass {
    fn oop_iterate<E: Edge, V: EdgeVisitor<E>, const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut V,
    ) {
        self.instance_klass
            .oop_iterate::<_, _, COMPRESSED>(oop, closure);
    }
}

impl OopIterate for ObjArrayKlass {
    fn oop_iterate<E: Edge, V: EdgeVisitor<E>, const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut V,
    ) {
        let array = unsafe { oop.as_array_oop() };
        if COMPRESSED {
            for narrow_oop in unsafe { array.data::<NarrowOop, COMPRESSED>(BasicType::T_OBJECT) } {
                closure.visit_edge(E::from_address(narrow_oop.slot()));
            }
        } else {
            for oop in unsafe { array.data::<Oop, COMPRESSED>(BasicType::T_OBJECT) } {
                closure.visit_edge(E::from_address(Address::from_ref(oop as &Oop)));
            }
        }
    }
}

impl OopIterate for TypeArrayKlass {
    fn oop_iterate<E: Edge, V: EdgeVisitor<E>, const COMPRESSED: bool>(
        &self,
        _oop: Oop,
        _closure: &mut V,
    ) {
        // Performance tweak: We skip processing the klass pointer since all
        // TypeArrayKlasses are guaranteed processed via the null class loader.
    }
}

impl OopIterate for InstanceRefKlass {
    fn oop_iterate<E: Edge, V: EdgeVisitor<E>, const COMPRESSED: bool>(
        &self,
        oop: Oop,
        closure: &mut V,
    ) {
        use crate::abi::*;
        use crate::api::{add_phantom_candidate, add_soft_candidate, add_weak_candidate};
        self.instance_klass
            .oop_iterate::<_, _, COMPRESSED>(oop, closure);

        if Self::should_scan_weak_refs::<COMPRESSED>() {
            let reference = ObjectReference::from(oop);
            match self.instance_klass.reference_type {
                ReferenceType::None => {
                    panic!("oop_iterate on InstanceRefKlass with reference_type as None")
                }
                ReferenceType::Weak => add_weak_candidate(reference),
                ReferenceType::Soft => add_soft_candidate(reference),
                ReferenceType::Phantom => add_phantom_candidate(reference),
                // Process these two types normally (as if they are strong refs)
                // We will handle final reference later
                ReferenceType::Final | ReferenceType::Other => {
                    Self::process_ref_as_strong(oop, closure)
                }
            }
        } else {
            Self::process_ref_as_strong(oop, closure);
        }
    }
}

impl InstanceRefKlass {
    fn should_scan_weak_refs<const COMPRESSED: bool>() -> bool {
        !*crate::singleton::<COMPRESSED>()
            .get_options()
            .no_reference_types
    }
    fn process_ref_as_strong<E: Edge, V: EdgeVisitor<E>>(oop: Oop, closure: &mut V) {
        let referent_addr = Self::referent_address::<E>(oop);
        closure.visit_edge(referent_addr);
        let discovered_addr = Self::discovered_address::<E>(oop);
        closure.visit_edge(discovered_addr);
    }
}

#[allow(unused)]
fn oop_iterate_slow<E: Edge, V: EdgeVisitor<E>>(oop: Oop, closure: &mut V, tls: OpaquePointer) {
    unsafe {
        CLOSURE.with(|x| *x.get() = closure as *mut V as *mut u8);
        ((*UPCALLS).scan_object)(
            mem::transmute(scan_object_fn::<E, V> as *const unsafe extern "C" fn(edge: Address)),
            mem::transmute(oop),
            tls,
        );
    }
}

fn oop_iterate<E: Edge, V: EdgeVisitor<E>, const COMPRESSED: bool>(
    oop: Oop,
    closure: &mut V,
    klass: Option<Address>,
) {
    let klass = if let Some(klass) = klass {
        unsafe { &*(klass.as_usize() as *const Klass) }
    } else {
        oop.klass::<COMPRESSED>()
    };
    let klass_id = klass.id;
    assert!(
        klass_id as i32 >= 0 && (klass_id as i32) < 6,
        "Invalid klass-id: {:x} for oop: {:x}",
        klass_id as i32,
        unsafe { mem::transmute::<Oop, ObjectReference>(oop) }
    );
    match klass_id {
        KlassID::Instance => {
            let instance_klass = unsafe { klass.cast::<InstanceKlass>() };
            instance_klass.oop_iterate::<E, V, COMPRESSED>(oop, closure);
        }
        KlassID::InstanceClassLoader => {
            let instance_klass = unsafe { klass.cast::<InstanceClassLoaderKlass>() };
            instance_klass.oop_iterate::<E, V, COMPRESSED>(oop, closure);
        }
        KlassID::InstanceMirror => {
            let instance_klass = unsafe { klass.cast::<InstanceMirrorKlass>() };
            instance_klass.oop_iterate::<E, V, COMPRESSED>(oop, closure);
        }
        KlassID::ObjArray => {
            let array_klass = unsafe { klass.cast::<ObjArrayKlass>() };
            array_klass.oop_iterate::<E, V, COMPRESSED>(oop, closure);
        }
        KlassID::TypeArray => {
            //     let array_klass = unsafe { oop.klass::<COMPRESSED>().cast::<TypeArrayKlass>() };
            //     array_klass.oop_iterate::<C, COMPRESSED>(oop, closure);
        }
        KlassID::InstanceRef => {
            let instance_klass = unsafe { klass.cast::<InstanceRefKlass>() };
            instance_klass.oop_iterate::<E, V, COMPRESSED>(oop, closure);
        }
        #[allow(unreachable_patterns)]
        _ => unreachable!(), // _ => oop_iterate_slow(oop, closure, OpaquePointer::UNINITIALIZED),
    }
}

thread_local! {
    static CLOSURE: UnsafeCell<*mut u8> = UnsafeCell::new(std::ptr::null_mut());
}

pub unsafe extern "C" fn scan_object_fn<E: Edge, V: EdgeVisitor<E>>(edge: Address) {
    let ptr: *mut u8 = CLOSURE.with(|x| *x.get());
    let closure = &mut *(ptr as *mut V);
    closure.visit_edge(E::from_address(edge));
}

pub fn scan_object<E: Edge, V: EdgeVisitor<E>, const COMPRESSED: bool>(
    object: ObjectReference,
    closure: &mut V,
    _tls: VMWorkerThread,
) {
    unsafe { oop_iterate::<E, V, COMPRESSED>(mem::transmute(object), closure, None) }
}
