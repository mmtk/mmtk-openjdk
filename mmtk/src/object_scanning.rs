use mmtk::util::{OpaquePointer, Address, ObjectReference};
use mmtk::TransitiveClosure;
use mmtk::util::constants::*;
use mmtk::util::conversions;
use std::mem;
use std::marker::PhantomData;
use super::UPCALLS;

trait EqualTo<T> {
    const VALUE: bool;
}

impl <T, U> EqualTo<U> for T {
    default const VALUE: bool = false;
}

impl <T> EqualTo<T> for T {
    const VALUE: bool = true;
}

pub const fn type_equal<T, U>() -> bool {
    <T as EqualTo<U>>::VALUE
}

#[repr(i32)]
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
#[allow(dead_code)]
enum KlassID {
    Instance,
    InstanceRef,
    InstanceMirror,
    InstanceClassLoader,
    TypeArray,
    ObjArray,
}


#[repr(C)]
struct Klass {
    vptr: *const (),
    #[cfg(debug_assertions)]
    valid: i32,
    layout_helper: i32,
    id: KlassID,
    super_check_offset: u32,
    name: *const (), // Symbol*
    secondary_super_cache: &'static Klass,
    secondary_supers: *mut (), // Array<Klass*>*
    primary_supers: [&'static Klass; 8],
    java_mirror:  &'static &'static Oop, // OopHandle
    super_: &'static Klass,
    subklass: &'static Klass,
    next_sibling: &'static Klass,
    next_link: &'static Klass,
    class_loader_data: *const (), // ClassLoaderData*
    modifier_flags: i32,
    access_flags: i32, // AccessFlags
    trace_id: u64, // JFR_ONLY(traceid _trace_id;)
    last_biased_lock_bulk_revocation_time: i64,
    prototype_header: &'static Oop, // markOop,
    biased_lock_revocation_count: i32,
    vtable_len: i32,
    shared_class_path_index: i16,
}

impl Klass {
    unsafe fn cast<'a, T>(&self) -> &'a T {
        &*(self as *const _ as usize as *const T)
    }
}

#[repr(C)]
struct InstanceKlass {
    klass: Klass,
    annotations: *const (), // Annotations*
    package_entry: *const (), // PackageEntry*
    array_klasses: &'static Klass,
    constants: *const (), // ConstantPool*
    inner_classes: *const (), // Array<jushort>*
    nest_members: *const (), // Array<jushort>*
    nest_host_index: u16,
    nest_host: &'static InstanceKlass,
    source_debug_extension: *const (), // const char*
    array_name: *const (), // Symbol*
    nonstatic_field_size: i32,
    static_field_size: i32,
    generic_signature_index: u16,
    source_file_name_index: u16,
    static_oop_field_count: u16,
    java_fields_count: u16,
    nonstatic_oop_map_size: i32,
    itable_len: i32,
    is_marked_dependent: bool, // bool
    is_being_redefined: bool, // bool
    misc_flags: u16,
    minor_version: u16,
    major_version: u16,
    init_thread: *const (), // Thread*
    oop_map_cache: *const (), // OopMapCache*
    jni_ids: *const (), // JNIid*
    methods_jmethod_ids: *const (), // jmethodID*
    dep_context: usize, // intptr_t
    osr_nmethods_head: *const (), // nmethod*
// #if INCLUDE_JVMTI
    breakpoints: *const (), // BreakpointInfo*
    previous_versions: *const (), // InstanceKlass*
    cached_class_file: *const (), // JvmtiCachedClassFileData*
// #endif
    idnum_allocated_count: u16,
    init_state: u8,
    reference_type: u8,
    this_class_index: u16,
// #if INCLUDE_JVMTI
    jvmti_cached_class_field_map: *const (), // JvmtiCachedClassFieldMap*
// #endif
    #[cfg(debug_assertions)]
    verify_count: i32,
    methods: *const (), // Array<Method*>*
    default_methods: *const (), // Array<Method*>*
    local_interfaces: *const (), // Array<Klass*>*
    transitive_interfaces: *const (), // Array<Klass*>*
    method_ordering: *const (), // Array<int>*
    default_vtable_indices: *const (), // Array<int>*
    fields: *const (), // Array<u2>*
}

impl InstanceKlass {
    const HEADER_SIZE: usize = mem::size_of::<Self>() / BYTES_IN_WORD;
    const VTABLE_START_OFFSET: usize = Self::HEADER_SIZE * BYTES_IN_WORD;

    fn start_of_vtable(&self) -> *const usize {
        unsafe { (self as *const _ as *const u8).add(Self::VTABLE_START_OFFSET) as _ }
    }

    fn start_of_itable(&self) -> *const usize {
        unsafe { self.start_of_vtable().add(self.klass.vtable_len as _) }
    }

    fn nonstatic_oop_map_count(&self) -> usize {
        let oop_map_block_size = mem::size_of::<OopMapBlock>();
        let oop_map_block_size_up = mmtk::util::conversions::raw_align_up(oop_map_block_size, BYTES_IN_WORD);
        self.nonstatic_oop_map_size as usize / (oop_map_block_size_up >> LOG_BYTES_IN_WORD)
    }

    fn nonstatic_oop_maps(&self) -> &'static [OopMapBlock] {
        let start_of_itable = self.start_of_itable();
        let start = unsafe { start_of_itable.add(self.itable_len as _) as *const OopMapBlock };
        let count = self.nonstatic_oop_map_count();
        unsafe {
            std::slice::from_raw_parts(start, count)
        }
    }

    fn oop_iterate_map(&self, oop: &'static Oop, map: &OopMapBlock, closure: &mut impl TransitiveClosure) {
        let start = oop.get_field_address(map.offset);
        for i in 0..map.count as usize {
            let edge = start + (i << LOG_BYTES_IN_ADDRESS);
            closure.process_edge(edge);
        }
    }

    fn oop_iterate(&self, oop: &'static Oop, closure: &mut impl TransitiveClosure) {
        let oop_maps = self.nonstatic_oop_maps();
        for map in oop_maps {
            self.oop_iterate_map(oop, map, closure)
        }
    }
}



#[repr(C)]
struct InstanceMirrorKlass {
    instance_klass: InstanceKlass,
    // offset_of_static_fields: i32,
}

impl InstanceMirrorKlass {
    fn oop_iterate_static(&self, oop: &'static Oop, closure: &mut impl TransitiveClosure) {

        // println!("cls oop = {:?}", oop as *const _);
        // println!("offset_of_static_fields = {:?}", self.offset_of_static_fields);
        let start_of_static_fields = unsafe {
            ((*UPCALLS).start_of_static_fields)(mem::transmute(oop))
        };
        let start: *const &'static Oop = start_of_static_fields.to_ptr::<&'static Oop>();
        let len = unsafe {
            ((*UPCALLS).static_oop_field_count)(oop as *const _ as _)
        };
        // println!("len = {:?}", len);
        let slice = unsafe { std::slice::from_raw_parts(start, len as _) };

        for oop in slice {
            let slot = oop as &&Oop;
            // println!("slot = {:?}, oop = {:?}", slot as *const _, *slot as *const Oop);
            closure.process_edge(Address::from_ref(slot));
        }
    }
    fn oop_iterate(&self, oop: &'static Oop, closure: &mut impl TransitiveClosure) {
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
        self.oop_iterate_static(oop, closure)
    }
}

#[repr(C)]
struct ArrayKlass {
    klass: Klass,
    dimension: i32,
    higher_dimension: &'static Klass,
    lower_dimension: &'static Klass,
}

#[repr(C)]
struct ObjArrayKlass {
    array_klass: ArrayKlass,
    element_klass: &'static Klass,
    bottom_klass: &'static Klass,
}

impl ObjArrayKlass {
    fn oop_iterate(&self, oop: &'static Oop, closure: &mut impl TransitiveClosure) {
        let array = unsafe { oop.cast::<ArrayOop<&'static Oop>>() };
        for oop in array.data() {
            let slot = oop as &&Oop;
            closure.process_edge(Address::from_ref(slot));
        }
    }
}

#[repr(C)]
struct TypeArrayKlass {
    array_klass: ArrayKlass,
    max_length: i32,
}

impl TypeArrayKlass {
    fn oop_iterate(&self, oop: &'static Oop, closure: &mut impl TransitiveClosure) {
        // Performance tweak: We skip processing the klass pointer since all
        // TypeArrayKlasses are guaranteed processed via the null class loader.
    }
}

#[repr(C)]
struct InstanceClassLoaderKlass {
    instance_klass: InstanceKlass,
}

impl InstanceClassLoaderKlass {
    fn oop_iterate(&self, oop: &'static Oop, closure: &mut impl TransitiveClosure) {
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


// #[repr(C)]
// struct InstanceRefKlass {
//     instance_klass: InstanceKlass,
// }

// impl InstanceRefKlass {
//     fn oop_iterate(&self, oop: &'static Oop, closure: &mut impl TransitiveClosure) {
//         self.instance_klass.oop_iterate(oop, closure);
//         for map in oop_maps {
//             self.oop_iterate_map(oop, map, closure)
//         }
//     }
// }

#[repr(C)]
struct Oop {
    mark: usize,
    klass: &'static Klass,
}

impl Oop {
    unsafe fn cast<'a, T>(&self) -> &'a T {
        &*(self as *const _ as usize as *const T)
    }

    fn to_address(&self) -> Address {
        Address::from_ref(self)
    }

    fn get_field_address(&self, offset: i32) -> Address {
        Address::from_ref(self) + offset as isize
    }
}

#[repr(C)]
struct ArrayOop<T>(Oop, PhantomData<T>);

impl <T> ArrayOop<T> {
    const ELEMENT_TYPE_SHOULD_BE_ALIGNED: bool = type_equal::<T, f64>() || type_equal::<T, i64>();
    const LENGTH_OFFSET: usize = mem::size_of::<Self>();
    fn header_size() -> usize {
        let typesize_in_bytes = conversions::raw_align_up(Self::LENGTH_OFFSET + BYTES_IN_INT, BYTES_IN_LONG);
        if Self::ELEMENT_TYPE_SHOULD_BE_ALIGNED {
            conversions::raw_align_up(typesize_in_bytes / BYTES_IN_WORD, BYTES_IN_LONG)
        } else {
            typesize_in_bytes / BYTES_IN_WORD
        }
    }
    fn length(&self) -> i32 {
        unsafe { *((self as *const _ as *const u8).add(Self::LENGTH_OFFSET) as *const i32) }
    }
    fn base(&self) -> *const T {
        let base_offset_in_bytes = Self::header_size() * BYTES_IN_WORD;
        unsafe { (self as *const _ as *const u8).add(base_offset_in_bytes) as _ }
    }
    fn data(&self) -> &[T] {
        unsafe {
            std::slice::from_raw_parts(self.base(), self.length() as _)
        }
    }
}

#[repr(C)]
#[derive(Debug)]
struct OopMapBlock {
    offset: i32,
    count: u32,
}



pub struct OopScan<'a, Closure: TransitiveClosure> {
    oop: &'static Oop,
    closure: &'a mut Closure,
    tls: OpaquePointer,
}

impl <'a, Closure: TransitiveClosure> OopScan<'a, Closure> {
    fn new(oop: &'static Oop, closure: &'a mut Closure, tls: OpaquePointer) -> Self {
        OopScan {
            oop,
            closure,
            tls,
        }
    }

    fn scan(&mut self) {
        let klass_id = self.oop.klass.id;
        debug_assert!(klass_id as i32 >= 0 && (klass_id as i32) < 6, "Invalid klass-id: {:?}", klass_id as i32);
        match klass_id {
            KlassID::Instance => {
                let instance_klass = unsafe { self.oop.klass.cast::<InstanceKlass>() };
                instance_klass.oop_iterate(self.oop, self.closure);
            },
            KlassID::InstanceClassLoader => {
                let instance_klass = unsafe { self.oop.klass.cast::<InstanceClassLoaderKlass>() };
                instance_klass.oop_iterate(self.oop, self.closure);
            },
            KlassID::InstanceMirror => {
                let instance_klass = unsafe { self.oop.klass.cast::<InstanceMirrorKlass>() };
                instance_klass.oop_iterate(self.oop, self.closure);
            },
            KlassID::ObjArray => {
                let array_klass = unsafe { self.oop.klass.cast::<ObjArrayKlass>() };
                array_klass.oop_iterate(self.oop, self.closure);
            },
            KlassID::TypeArray => {
                let array_klass = unsafe { self.oop.klass.cast::<TypeArrayKlass>() };
                array_klass.oop_iterate(self.oop, self.closure);
            },
            _ => self.scan_slow(),
        }
    }

    fn scan_slow(&mut self) {
        unsafe {
            ((*UPCALLS).scan_object)(mem::transmute(self.closure as *mut _), mem::transmute(self.oop), self.tls);
        }
    }
}

pub fn scan_object(object: ObjectReference, closure: &mut impl TransitiveClosure, tls: OpaquePointer) {
    unsafe {
        OopScan::new(mem::transmute(object), closure, tls).scan()
    }
}

pub fn validate_memory_layouts() {
    unsafe {
        ((*UPCALLS).validate_klass_mem_layout)(
            mem::size_of::<Klass>(),
            mem::size_of::<InstanceKlass>()
        )
    }
}

