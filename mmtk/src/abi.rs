use super::UPCALLS;
use crate::OpenJDKEdge;
use atomic::Atomic;
use atomic::Ordering;
use mmtk::util::constants::*;
use mmtk::util::conversions;
use mmtk::util::ObjectReference;
use mmtk::util::{Address, OpaquePointer};
use std::ffi::CStr;
use std::fmt;
use std::sync::atomic::AtomicUsize;
use std::{mem, slice};

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

#[repr(i32)]
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
#[allow(dead_code)]
#[allow(non_camel_case_types)]
pub enum BasicType {
    T_BOOLEAN = 4,
    T_CHAR = 5,
    T_FLOAT = 6,
    T_DOUBLE = 7,
    T_BYTE = 8,
    T_SHORT = 9,
    T_INT = 10,
    T_LONG = 11,
    T_OBJECT = 12,
    T_ARRAY = 13,
    T_VOID = 14,
    T_ADDRESS = 15,
    T_NARROWOOP = 16,
    T_METADATA = 17,
    T_NARROWKLASS = 18,
    T_CONFLICT = 19, // for stack value type with conflicting contents
    T_ILLEGAL = 99,
}

#[repr(C)]
pub struct Klass {
    vptr: OpaquePointer,
    #[cfg(debug_assertions)]
    valid: i32,
    pub layout_helper: i32,
    pub id: KlassID,
    pub super_check_offset: u32,
    pub name: OpaquePointer, // Symbol*
    pub secondary_super_cache: &'static Klass,
    pub secondary_supers: OpaquePointer, // Array<Klass*>*
    pub primary_supers: [&'static Klass; 8],
    pub java_mirror: &'static Oop, // OopHandle
    pub super_: &'static Klass,
    pub subklass: &'static Klass,
    pub next_sibling: &'static Klass,
    pub next_link: &'static Klass,
    pub class_loader_data: OpaquePointer, // ClassLoaderData*
    pub modifier_flags: i32,
    pub access_flags: i32, // AccessFlags
    pub trace_id: u64,     // JFR_ONLY(traceid _trace_id;)
    pub last_biased_lock_bulk_revocation_time: i64,
    pub prototype_header: Oop, // markOop,
    pub biased_lock_revocation_count: i32,
    pub vtable_len: i32,
    pub shared_class_path_index: i16,
}

impl Klass {
    pub const LH_NEUTRAL_VALUE: i32 = 0;
    pub const LH_INSTANCE_SLOW_PATH_BIT: i32 = 0x01;
    #[allow(clippy::erasing_op)]
    pub const LH_LOG2_ELEMENT_SIZE_SHIFT: i32 = BITS_IN_BYTE as i32 * 0;
    pub const LH_LOG2_ELEMENT_SIZE_MASK: i32 = BITS_IN_LONG as i32 - 1;
    pub const LH_HEADER_SIZE_SHIFT: i32 = BITS_IN_BYTE as i32 * 2;
    pub const LH_HEADER_SIZE_MASK: i32 = (1 << BITS_IN_BYTE) - 1;
    pub unsafe fn cast<'a, T>(&self) -> &'a T {
        &*(self as *const Self as *const T)
    }
    /// Force slow-path for instance size calculation?
    const fn layout_helper_needs_slow_path(lh: i32) -> bool {
        (lh & Self::LH_INSTANCE_SLOW_PATH_BIT) != 0
    }
    /// Get log2 array element size
    const fn layout_helper_log2_element_size(lh: i32) -> i32 {
        (lh >> Self::LH_LOG2_ELEMENT_SIZE_SHIFT) & Self::LH_LOG2_ELEMENT_SIZE_MASK
    }
    /// Get array header size
    const fn layout_helper_header_size(lh: i32) -> i32 {
        (lh >> Self::LH_HEADER_SIZE_SHIFT) & Self::LH_HEADER_SIZE_MASK
    }
}

#[repr(C)]
pub struct InstanceKlass {
    pub klass: Klass,
    pub annotations: OpaquePointer,   // Annotations*
    pub package_entry: OpaquePointer, // PackageEntry*
    pub array_klasses: &'static Klass,
    pub constants: OpaquePointer,     // ConstantPool*
    pub inner_classes: OpaquePointer, // Array<jushort>*
    pub nest_members: OpaquePointer,  // Array<jushort>*
    pub nest_host_index: u16,
    pub nest_host: &'static InstanceKlass,
    pub source_debug_extension: OpaquePointer, // const char*
    pub array_name: OpaquePointer,             // Symbol*
    pub nonstatic_field_size: i32,
    pub static_field_size: i32,
    pub generic_signature_index: u16,
    pub source_file_name_index: u16,
    pub static_oop_field_count: u16,
    pub java_fields_count: u16,
    pub nonstatic_oop_map_size: i32,
    pub itable_len: i32,
    pub is_marked_dependent: bool, // bool
    pub is_being_redefined: bool,  // bool
    pub misc_flags: u16,
    pub minor_version: u16,
    pub major_version: u16,
    pub init_thread: OpaquePointer,         // Thread*
    pub oop_map_cache: OpaquePointer,       // OopMapCache*
    pub jni_ids: OpaquePointer,             // JNIid*
    pub methods_jmethod_ids: OpaquePointer, // jmethodID*
    pub dep_context: usize,                 // intptr_t
    pub osr_nmethods_head: OpaquePointer,   // nmethod*
    // #if INCLUDE_JVMTI
    pub breakpoints: OpaquePointer,       // BreakpointInfo*
    pub previous_versions: OpaquePointer, // InstanceKlass*
    pub cached_class_file: OpaquePointer, // JvmtiCachedClassFileData*
    // #endif
    pub idnum_allocated_count: u16,
    pub init_state: u8,
    pub reference_type: ReferenceType,
    pub this_class_index: u16,
    // #if INCLUDE_JVMTI
    pub jvmti_cached_class_field_map: OpaquePointer, // JvmtiCachedClassFieldMap*
    // #endif
    #[cfg(debug_assertions)]
    verify_count: i32,
    pub methods: OpaquePointer,                // Array<Method*>*
    pub default_methods: OpaquePointer,        // Array<Method*>*
    pub local_interfaces: OpaquePointer,       // Array<Klass*>*
    pub transitive_interfaces: OpaquePointer,  // Array<Klass*>*
    pub method_ordering: OpaquePointer,        // Array<int>*
    pub default_vtable_indices: OpaquePointer, // Array<int>*
    pub fields: OpaquePointer,                 // Array<u2>*
}

#[repr(u8)]
#[derive(Copy, Clone, Debug)]
#[allow(dead_code)]
pub enum ReferenceType {
    None,    // Regular class
    Other,   // Subclass of java/lang/ref/Reference, but not subclass of one of the classes below
    Soft,    // Subclass of java/lang/ref/SoftReference
    Weak,    // Subclass of java/lang/ref/WeakReference
    Final,   // Subclass of java/lang/ref/FinalReference
    Phantom, // Subclass of java/lang/ref/PhantomReference
}

impl InstanceKlass {
    const HEADER_SIZE: usize = mem::size_of::<Self>() / BYTES_IN_WORD;
    const VTABLE_START_OFFSET: usize = Self::HEADER_SIZE * BYTES_IN_WORD;

    fn start_of_vtable(&self) -> *const usize {
        (Address::from_ref(self) + Self::VTABLE_START_OFFSET).to_ptr()
    }

    fn start_of_itable(&self) -> *const usize {
        unsafe { self.start_of_vtable().add(self.klass.vtable_len as _) }
    }

    fn nonstatic_oop_map_count(&self) -> usize {
        let oop_map_block_size = mem::size_of::<OopMapBlock>();
        let oop_map_block_size_up =
            mmtk::util::conversions::raw_align_up(oop_map_block_size, BYTES_IN_WORD);
        self.nonstatic_oop_map_size as usize / (oop_map_block_size_up >> LOG_BYTES_IN_WORD)
    }

    pub fn nonstatic_oop_maps(&self) -> &'static [OopMapBlock] {
        let start_of_itable = self.start_of_itable();
        let start = unsafe { start_of_itable.add(self.itable_len as _) as *const OopMapBlock };
        let count = self.nonstatic_oop_map_count();
        unsafe { slice::from_raw_parts(start, count) }
    }
}

#[repr(C)]
pub struct InstanceMirrorKlass {
    pub instance_klass: InstanceKlass,
}

impl InstanceMirrorKlass {
    fn offset_of_static_fields() -> usize {
        lazy_static! {
            pub static ref OFFSET_OF_STATIC_FIELDS: usize =
                unsafe { ((*UPCALLS).offset_of_static_fields)() as usize };
        }
        *OFFSET_OF_STATIC_FIELDS
    }
    fn static_oop_field_count_offset() -> i32 {
        lazy_static! {
            pub static ref STATIC_OOP_FIELD_COUNT_OFFSET: i32 =
                unsafe { ((*UPCALLS).static_oop_field_count_offset)() };
        }
        *STATIC_OOP_FIELD_COUNT_OFFSET
    }
    pub fn start_of_static_fields(oop: Oop) -> Address {
        Address::from_ref(oop) + Self::offset_of_static_fields()
    }
    pub fn static_oop_field_count(oop: Oop) -> usize {
        let offset = Self::static_oop_field_count_offset();
        unsafe { oop.get_field_address(offset).load::<i32>() as _ }
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

impl InstanceRefKlass {
    fn referent_offset() -> i32 {
        lazy_static! {
            pub static ref REFERENT_OFFSET: i32 = unsafe { ((*UPCALLS).referent_offset)() };
        }
        *REFERENT_OFFSET
    }
    fn discovered_offset() -> i32 {
        lazy_static! {
            pub static ref DISCOVERED_OFFSET: i32 = unsafe { ((*UPCALLS).discovered_offset)() };
        }
        *DISCOVERED_OFFSET
    }
    pub fn referent_address<const COMPRESSED: bool>(oop: Oop) -> OpenJDKEdge<COMPRESSED> {
        oop.get_field_address(Self::referent_offset()).into()
    }
    pub fn discovered_address<const COMPRESSED: bool>(oop: Oop) -> OpenJDKEdge<COMPRESSED> {
        oop.get_field_address(Self::discovered_offset()).into()
    }
}

#[repr(C)]
union KlassPointer {
    /// uncompressed Klass pointer
    klass: &'static Klass,
    /// compressed Klass pointer
    narrow_klass: u32,
}

#[repr(C)]
pub struct OopDesc {
    pub mark: usize,
    klass: KlassPointer,
}

static COMPRESSED_KLASS_BASE: Atomic<Address> = Atomic::new(Address::ZERO);
static COMPRESSED_KLASS_SHIFT: AtomicUsize = AtomicUsize::new(0);

/// When enabling compressed pointers, the class pointers are also compressed.
/// The c++ part of the binding should pass the compressed klass base and shift to rust binding, as object scanning will need it.
pub fn set_compressed_klass_base_and_shift(base: Address, shift: usize) {
    COMPRESSED_KLASS_BASE.store(base, Ordering::Relaxed);
    COMPRESSED_KLASS_SHIFT.store(shift, Ordering::Relaxed);
}

impl OopDesc {
    pub fn start(&self) -> Address {
        unsafe { mem::transmute(self) }
    }

    pub fn klass<const COMPRESSED: bool>(&self) -> &'static Klass {
        if COMPRESSED {
            let compressed = unsafe { self.klass.narrow_klass };
            let addr = COMPRESSED_KLASS_BASE.load(Ordering::Relaxed)
                + ((compressed as usize) << COMPRESSED_KLASS_SHIFT.load(Ordering::Relaxed));
            unsafe { &*addr.to_ptr::<Klass>() }
        } else {
            unsafe { self.klass.klass }
        }
    }
}

impl fmt::Debug for OopDesc {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let c_string = unsafe { ((*UPCALLS).dump_object_string)(mem::transmute(self)) };
        let c_str: &CStr = unsafe { CStr::from_ptr(c_string) };
        let s: &str = c_str.to_str().unwrap();
        write!(f, "{}", s)
    }
}

/// 32-bit compressed klass pointers
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct NarrowKlass(u32);

pub type Oop = &'static OopDesc;

/// 32-bit compressed reference pointers
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct NarrowOop(u32);

impl NarrowOop {
    pub fn slot(&self) -> Address {
        Address::from_ref(self)
    }
}

/// Convert ObjectReference to Oop
impl From<ObjectReference> for &OopDesc {
    fn from(o: ObjectReference) -> Self {
        unsafe { mem::transmute(o) }
    }
}

/// Convert Oop to ObjectReference
impl From<&OopDesc> for ObjectReference {
    fn from(o: &OopDesc) -> Self {
        unsafe { mem::transmute(o) }
    }
}

impl OopDesc {
    pub unsafe fn as_array_oop(&self) -> ArrayOop {
        &*(self as *const OopDesc as *const ArrayOopDesc)
    }

    pub fn get_field_address(&self, offset: i32) -> Address {
        Address::from_ref(self) + offset as isize
    }

    /// Slow-path for calculating object instance size
    unsafe fn size_slow(&self) -> usize {
        ((*UPCALLS).get_object_size)(self.into())
    }

    /// Calculate object instance size
    pub unsafe fn size<const COMPRESSED: bool>(&self) -> usize {
        let klass = self.klass::<COMPRESSED>();
        let lh = klass.layout_helper;
        // The (scalar) instance size is pre-recorded in the TIB?
        if lh > Klass::LH_NEUTRAL_VALUE {
            if !Klass::layout_helper_needs_slow_path(lh) {
                lh as _
            } else {
                self.size_slow()
            }
        } else if lh <= Klass::LH_NEUTRAL_VALUE {
            if lh < Klass::LH_NEUTRAL_VALUE {
                // Calculate array size
                let array_length = self.as_array_oop().length::<COMPRESSED>();
                let mut size_in_bytes: usize =
                    (array_length as usize) << Klass::layout_helper_log2_element_size(lh);
                size_in_bytes += Klass::layout_helper_header_size(lh) as usize;
                (size_in_bytes + 0b111) & !0b111
            } else {
                self.size_slow()
            }
        } else {
            unreachable!()
        }
    }
}

#[repr(C)]
pub struct ArrayOopDesc(OopDesc);

pub type ArrayOop = &'static ArrayOopDesc;

impl ArrayOopDesc {
    fn length_offset<const COMPRESSED: bool>() -> usize {
        let klass_offset_in_bytes = memoffset::offset_of!(OopDesc, klass);
        if COMPRESSED {
            klass_offset_in_bytes + mem::size_of::<NarrowKlass>()
        } else {
            klass_offset_in_bytes + mem::size_of::<KlassPointer>()
        }
    }

    fn element_type_should_be_aligned(ty: BasicType) -> bool {
        ty == BasicType::T_DOUBLE || ty == BasicType::T_LONG
    }

    fn header_size<const COMPRESSED: bool>(ty: BasicType) -> usize {
        let typesize_in_bytes = conversions::raw_align_up(
            Self::length_offset::<COMPRESSED>() + BYTES_IN_INT,
            BYTES_IN_LONG,
        );
        if Self::element_type_should_be_aligned(ty) {
            conversions::raw_align_up(typesize_in_bytes / BYTES_IN_WORD, BYTES_IN_LONG)
        } else {
            typesize_in_bytes / BYTES_IN_WORD
        }
    }
    fn length<const COMPRESSED: bool>(&self) -> i32 {
        unsafe { (Address::from_ref(self) + Self::length_offset::<COMPRESSED>()).load::<i32>() }
    }
    fn base<const COMPRESSED: bool>(&self, ty: BasicType) -> Address {
        let base_offset_in_bytes = Self::header_size::<COMPRESSED>(ty) * BYTES_IN_WORD;
        Address::from_ref(self) + base_offset_in_bytes
    }
    /// This provides an easy way to access the array data in Rust. However, the array data
    /// is Java types, so we have to map Java types to Rust types. The caller needs to guarantee:
    /// 1. `<T>` matches the actual Java type
    /// 2. `<T>` matches the argument, BasicType `ty`
    pub unsafe fn data<T, const COMPRESSED: bool>(&self, ty: BasicType) -> &[T] {
        slice::from_raw_parts(
            self.base::<COMPRESSED>(ty).to_ptr(),
            self.length::<COMPRESSED>() as _,
        )
    }

    pub unsafe fn slice<const COMPRESSED: bool>(
        &self,
        ty: BasicType,
    ) -> crate::OpenJDKEdgeRange<COMPRESSED> {
        let base = self.base::<COMPRESSED>(ty);
        let start = base;
        let lshift = OpenJDKEdge::<COMPRESSED>::LOG_BYTES_IN_EDGE;
        let end = base + ((self.length::<COMPRESSED>() as usize) << lshift);
        (start..end).into()
    }
}

#[repr(C)]
#[derive(Debug)]
pub struct OopMapBlock {
    pub offset: i32,
    pub count: u32,
}

pub fn validate_memory_layouts() {
    let vm_checksum = unsafe { ((*UPCALLS).compute_klass_mem_layout_checksum)() };
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
