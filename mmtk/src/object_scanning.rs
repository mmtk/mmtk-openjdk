use mmtk::util::{OpaquePointer, Address, ObjectReference};
use mmtk::TransitiveClosure;
use mmtk::util::constants::*;
use std::{mem, slice};
use super::UPCALLS;
use super::abi::*;



trait OopIterate: Sized {
    fn oop_iterate(&self, oop: Oop, closure: &mut impl TransitiveClosure);
}

impl OopIterate for OopMapBlock {
    fn oop_iterate(&self, oop: Oop, closure: &mut impl TransitiveClosure) {
        let start = oop.get_field_address(self.offset);
        for i in 0..self.count as usize {
            let edge = start + (i << LOG_BYTES_IN_ADDRESS);
            closure.process_edge(edge);
        }
    }
}

impl OopIterate for InstanceKlass {
    fn oop_iterate(&self, oop: Oop, closure: &mut impl TransitiveClosure) {
        let oop_maps = self.nonstatic_oop_maps();
        for map in oop_maps {
            map.oop_iterate(oop, closure)
        }
    }
}

impl OopIterate for InstanceMirrorKlass {
    fn oop_iterate(&self, oop: Oop, closure: &mut impl TransitiveClosure) {
        self.instance_klass.oop_iterate(oop, closure);
        // if (Devirtualizer::do_metadata(closure)) {
        //     Klass* klass = java_lang_Class::as_Klass(obj);
        //     // We'll get NULL for primitive mirrors.
        //     if (klass != NULL) {
        //       if (klass->is_instance_klass() && InstanceKlass::cast(klass)->is_anonymous()) {
        //         // An anonymous class doesn't have its own class loader, so when handling
        //         // the java mirror for an anonymous class we need to make sure its class
        //         // loader data is claimed, this is done by calling do_cld explicitly.
        //         // For non-anonymous classes the call to do_cld is made when the class
        //         // loader itself is handled.
        //         Devirtualizer::do_cld(closure, klass->class_loader_data());
        //       } else {
        //         Devirtualizer::do_klass(closure, klass);
        //       }
        //     } else {
        //       // We would like to assert here (as below) that if klass has been NULL, then
        //       // this has been a mirror for a primitive type that we do not need to follow
        //       // as they are always strong roots.
        //       // However, we might get across a klass that just changed during CMS concurrent
        //       // marking if allocation occurred in the old generation.
        //       // This is benign here, as we keep alive all CLDs that were loaded during the
        //       // CMS concurrent phase in the class loading, i.e. they will be iterated over
        //       // and kept alive during remark.
        //       // assert(java_lang_Class::is_primitive(obj), "Sanity check");
        //     }
        // }

        // static fields
        let start: *const Oop = Self::start_of_static_fields(oop).to_ptr::<Oop>();
        let len = Self::static_oop_field_count(oop);
        let slice = unsafe { slice::from_raw_parts(start, len as _) };
        for oop in slice {
            closure.process_edge(Address::from_ref(oop as &Oop));
        }
    }
}

impl OopIterate for InstanceClassLoaderKlass {
    fn oop_iterate(&self, oop: Oop, closure: &mut impl TransitiveClosure) {
        self.instance_klass.oop_iterate(oop, closure);
        // if (Devirtualizer::do_metadata(closure)) {
        //     ClassLoaderData* cld = java_lang_ClassLoader::loader_data(obj);
        //     // cld can be null if we have a non-registered class loader.
        //     if (cld != NULL) {
        //         Devirtualizer::do_cld(closure, cld);
        //     }
        // }
    }
}

impl OopIterate for ObjArrayKlass {
    fn oop_iterate(&self, oop: Oop, closure: &mut impl TransitiveClosure) {
        let array = unsafe { oop.as_array_oop::<Oop>() };
        for oop in array.data() {
            closure.process_edge(Address::from_ref(oop as &Oop));
        }
    }
}

impl OopIterate for TypeArrayKlass {
    fn oop_iterate(&self, _oop: Oop, _closure: &mut impl TransitiveClosure) {
        // Performance tweak: We skip processing the klass pointer since all
        // TypeArrayKlasses are guaranteed processed via the null class loader.
    }
}

impl OopIterate for InstanceRefKlass {
    fn oop_iterate(&self, oop: Oop, closure: &mut impl TransitiveClosure) {
        self.instance_klass.oop_iterate(oop, closure);
        let referent_addr = Self::referent_address(oop);
        closure.process_edge(referent_addr);
        let discovered_addr = Self::discovered_address(oop);
        closure.process_edge(discovered_addr);
    }
}


#[allow(unused)]
fn oop_iterate_slow(oop: Oop, closure: &mut impl TransitiveClosure, tls: OpaquePointer) {
    unsafe {
        ((*UPCALLS).scan_object)(closure as *mut _ as _, mem::transmute(oop), tls);
    }
}

fn oop_iterate(oop: Oop, closure: &mut impl TransitiveClosure, _tls: OpaquePointer) {
    let klass_id = oop.klass.id;
    debug_assert!(klass_id as i32 >= 0 && (klass_id as i32) < 6, "Invalid klass-id: {:?}", klass_id as i32);
    match klass_id {
        KlassID::Instance => {
            let instance_klass = unsafe { oop.klass.cast::<InstanceKlass>() };
            instance_klass.oop_iterate(oop, closure);
        },
        KlassID::InstanceClassLoader => {
            let instance_klass = unsafe { oop.klass.cast::<InstanceClassLoaderKlass>() };
            instance_klass.oop_iterate(oop, closure);
        },
        KlassID::InstanceMirror => {
            let instance_klass = unsafe { oop.klass.cast::<InstanceMirrorKlass>() };
            instance_klass.oop_iterate(oop, closure);
        },
        KlassID::ObjArray => {
            let array_klass = unsafe { oop.klass.cast::<ObjArrayKlass>() };
            array_klass.oop_iterate(oop, closure);
        },
        KlassID::TypeArray => {
            let array_klass = unsafe { oop.klass.cast::<TypeArrayKlass>() };
            array_klass.oop_iterate(oop, closure);
        },
        KlassID::InstanceRef => {
            let instance_klass = unsafe { oop.klass.cast::<InstanceRefKlass>() };
            instance_klass.oop_iterate(oop, closure);
        },
        // _ => oop_iterate_slow(oop, closure, tls),
    }
}

pub fn scan_object(object: ObjectReference, closure: &mut impl TransitiveClosure, tls: OpaquePointer) {
    unsafe {
        oop_iterate(mem::transmute(object), closure, tls)
    }
}

pub fn validate_memory_layouts() {
    let vm_checksum = unsafe {
        ((*UPCALLS).compute_klass_mem_layout_checksum)()
    };
    let binding_checksum = {
        mem::size_of::<Klass>()
        ^ mem::size_of::<InstanceKlass>()
        ^ mem::size_of::<InstanceRefKlass>()
        ^ mem::size_of::<InstanceMirrorKlass>()
        ^ mem::size_of::<InstanceClassLoaderKlass>()
        ^ mem::size_of::<TypeArrayKlass>()
        ^ mem::size_of::<ObjArrayKlass>()
    };
    assert_eq!(vm_checksum, binding_checksum);
}

