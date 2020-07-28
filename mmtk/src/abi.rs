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
pub enum KlassID {
    Instance,
    InstanceRef,
    InstanceMirror,
    InstanceClassLoader,
    TypeArray,
    ObjArray,
}

#[repr(C)]
pub struct Klass {
    vptr: *const (),
    #[cfg(debug_assertions)]
    valid: i32,
    pub layout_helper: i32,
    pub id: KlassID,
    pub super_check_offset: u32,
    pub name: *const (), // Symbol*
    pub secondary_super_cache: &'static Klass,
    pub secondary_supers: *mut (), // Array<Klass*>*
    pub primary_supers: [&'static Klass; 8],
    pub java_mirror:  &'static &'static Oop, // OopHandle
    pub super_: &'static Klass,
    pub subklass: &'static Klass,
    pub next_sibling: &'static Klass,
    pub next_link: &'static Klass,
    pub class_loader_data: *const (), // ClassLoaderData*
    pub modifier_flags: i32,
    pub access_flags: i32, // AccessFlags
    pub trace_id: u64, // JFR_ONLY(traceid _trace_id;)
    pub last_biased_lock_bulk_revocation_time: i64,
    pub prototype_header: &'static Oop, // markOop,
    pub biased_lock_revocation_count: i32,
    pub vtable_len: i32,
    pub shared_class_path_index: i16,
}

impl Klass {
    pub unsafe fn cast<'a, T>(&self) -> &'a T {
        &*(self as *const _ as usize as *const T)
    }
}

#[repr(C)]
pub struct InstanceKlass {
    pub klass: Klass,
    pub annotations: *const (), // Annotations*
    pub package_entry: *const (), // PackageEntry*
    pub array_klasses: &'static Klass,
    pub constants: *const (), // ConstantPool*
    pub inner_classes: *const (), // Array<jushort>*
    pub nest_members: *const (), // Array<jushort>*
    pub nest_host_index: u16,
    pub nest_host: &'static InstanceKlass,
    pub source_debug_extension: *const (), // const char*
    pub array_name: *const (), // Symbol*
    pub nonstatic_field_size: i32,
    pub static_field_size: i32,
    pub generic_signature_index: u16,
    pub source_file_name_index: u16,
    pub static_oop_field_count: u16,
    pub java_fields_count: u16,
    pub nonstatic_oop_map_size: i32,
    pub itable_len: i32,
    pub is_marked_dependent: bool, // bool
    pub is_being_redefined: bool, // bool
    pub misc_flags: u16,
    pub minor_version: u16,
    pub major_version: u16,
    pub init_thread: *const (), // Thread*
    pub oop_map_cache: *const (), // OopMapCache*
    pub jni_ids: *const (), // JNIid*
    pub methods_jmethod_ids: *const (), // jmethodID*
    pub dep_context: usize, // intptr_t
    pub osr_nmethods_head: *const (), // nmethod*
// #if INCLUDE_JVMTI
    pub breakpoints: *const (), // BreakpointInfo*
    pub previous_versions: *const (), // InstanceKlass*
    pub cached_class_file: *const (), // JvmtiCachedClassFileData*
// #endif
    pub idnum_allocated_count: u16,
    pub init_state: u8,
    pub reference_type: u8,
    pub this_class_index: u16,
// #if INCLUDE_JVMTI
    pub jvmti_cached_class_field_map: *const (), // JvmtiCachedClassFieldMap*
// #endif
    #[cfg(debug_assertions)]
    verify_count: i32,
    pub methods: *const (), // Array<Method*>*
    pub default_methods: *const (), // Array<Method*>*
    pub local_interfaces: *const (), // Array<Klass*>*
    pub transitive_interfaces: *const (), // Array<Klass*>*
    pub method_ordering: *const (), // Array<int>*
    pub default_vtable_indices: *const (), // Array<int>*
    pub fields: *const (), // Array<u2>*
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

    pub fn nonstatic_oop_maps(&self) -> &'static [OopMapBlock] {
        let start_of_itable = self.start_of_itable();
        let start = unsafe { start_of_itable.add(self.itable_len as _) as *const OopMapBlock };
        let count = self.nonstatic_oop_map_count();
        unsafe {
            std::slice::from_raw_parts(start, count)
        }
    }
}

#[repr(C)]
pub struct InstanceMirrorKlass {
    pub instance_klass: InstanceKlass,
}

impl InstanceMirrorKlass {
    pub fn start_of_static_fields(oop: &'static Oop) -> Address {
        unsafe { ((*UPCALLS).start_of_static_fields)(oop as *const _ as _) }
    }
    pub fn static_oop_field_count(oop: &'static Oop) -> usize {
        unsafe { ((*UPCALLS).static_oop_field_count)(oop as *const _ as _) as _ }
    }
}

#[repr(C)]
pub struct ArrayKlass {
    pub klass: Klass,
    pub dimension: i32,
    pub higher_dimension: &'static Klass,
    pub lower_dimension: &'static Klass,
}

#[repr(C)]
pub struct ObjArrayKlass {
    pub array_klass: ArrayKlass,
    pub element_klass: &'static Klass,
    pub bottom_klass: &'static Klass,
}

#[repr(C)]
pub struct TypeArrayKlass {
    pub array_klass: ArrayKlass,
    pub max_length: i32,
}

#[repr(C)]
pub struct InstanceClassLoaderKlass {
    pub instance_klass: InstanceKlass,
}

#[repr(C)]
pub struct InstanceRefKlass {
    pub instance_klass: InstanceKlass,
}



#[repr(C)]
pub struct Oop {
    pub mark: usize,
    pub klass: &'static Klass,
}

impl Oop {
    pub unsafe fn cast<'a, T>(&self) -> &'a T {
        &*(self as *const _ as usize as *const T)
    }

    pub fn to_address(&self) -> Address {
        Address::from_ref(self)
    }

    pub fn get_field_address(&self, offset: i32) -> Address {
        Address::from_ref(self) + offset as isize
    }
}

#[repr(C)]
pub struct ArrayOop<T>(Oop, PhantomData<T>);

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
    pub fn data(&self) -> &[T] {
        unsafe {
            std::slice::from_raw_parts(self.base(), self.length() as _)
        }
    }
}

#[repr(C)]
#[derive(Debug)]
pub struct OopMapBlock {
    pub offset: i32,
    pub count: u32,
}

