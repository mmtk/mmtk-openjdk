use std::{
    ops::Range,
    sync::atomic::{AtomicBool, AtomicUsize, Ordering},
};

use atomic::Atomic;
use mmtk::{
    util::{
        constants::{LOG_BYTES_IN_ADDRESS, LOG_BYTES_IN_INT},
        Address, ObjectReference,
    },
    vm::edge_shape::{Edge, MemorySlice},
};

static USE_COMPRESSED_OOPS: AtomicBool = AtomicBool::new(false);
pub static BASE: Atomic<Address> = Atomic::new(Address::ZERO);
pub static SHIFT: AtomicUsize = AtomicUsize::new(0);

pub fn enable_compressed_oops() {
    USE_COMPRESSED_OOPS.store(true, Ordering::Relaxed)
}

pub fn use_compressed_oops() -> bool {
    USE_COMPRESSED_OOPS.load(Ordering::Relaxed)
}

pub fn initialize_compressed_oops_base_and_shift() {
    let heap_end = mmtk::memory_manager::last_heap_address().as_usize();
    if heap_end <= (4usize << 30) {
        BASE.store(Address::ZERO, Ordering::Relaxed);
        SHIFT.store(0, Ordering::Relaxed);
    } else if heap_end <= (32usize << 30) {
        BASE.store(Address::ZERO, Ordering::Relaxed);
        SHIFT.store(3, Ordering::Relaxed);
    } else {
        // set heap base as HEAP_START - 4096, to make sure null pointer value is not conflict with HEAP_START
        BASE.store(
            mmtk::memory_manager::starting_heap_address() - 4096,
            Ordering::Relaxed,
        );
        SHIFT.store(3, Ordering::Relaxed);
    }
}

/// The type of edges in OpenJDK.
/// Currently it has the same layout as `Address`, but we override its load and store methods.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
#[repr(transparent)]
pub struct OpenJDKEdge<const COMPRESSED: bool> {
    pub addr: Address,
}

impl<const COMPRESSED: bool> From<Address> for OpenJDKEdge<COMPRESSED> {
    fn from(value: Address) -> Self {
        Self { addr: value }
    }
}

impl<const COMPRESSED: bool> OpenJDKEdge<COMPRESSED> {
    pub const LOG_BYTES_IN_EDGE: usize = if COMPRESSED { 2 } else { 3 };
    pub const BYTES_IN_EDGE: usize = 1 << Self::LOG_BYTES_IN_EDGE;

    const MASK: usize = 1usize << 63;

    const fn is_compressed(&self) -> bool {
        self.addr.as_usize() & Self::MASK == 0
    }

    const fn untagged_address(&self) -> Address {
        unsafe { Address::from_usize(self.addr.as_usize() << 1 >> 1) }
    }

    fn x86_read_unaligned<T, const UNTAG: bool>(&self) -> T {
        debug_assert!(cfg!(any(target_arch = "x86", target_arch = "x86_64")));
        // Workaround: On x86 (including x86_64), machine instructions may contain pointers as
        // immediates, and they may be unaligned.  It is an undefined behavior in Rust to
        // dereference unaligned pointers.  We have to explicitly use unaligned memory access
        // methods.  On x86, ordinary MOV instructions can load and store memory at unaligned
        // addresses, so we expect `ptr.read_unaligned()` to have no performance penalty over
        // `ptr.read()` if `ptr` is actually aligned.
        unsafe {
            let slot = if UNTAG {
                self.untagged_address()
            } else {
                self.addr
            };
            let ptr = slot.to_ptr::<T>();
            ptr.read_unaligned()
        }
    }

    fn x86_write_unaligned<T: Copy, const UNTAG: bool>(&self, v: T) {
        debug_assert!(cfg!(any(target_arch = "x86", target_arch = "x86_64")));
        unsafe {
            let slot = if UNTAG {
                self.untagged_address()
            } else {
                self.addr
            };
            let ptr = slot.to_mut_ptr::<T>();
            ptr.write_unaligned(v)
        }
    }

    fn compress(o: ObjectReference) -> u32 {
        if o.is_null() {
            0u32
        } else {
            ((o.to_raw_address() - BASE.load(Ordering::Relaxed)) >> SHIFT.load(Ordering::Relaxed))
                as u32
        }
    }

    fn decompress(v: u32) -> ObjectReference {
        if v == 0 {
            ObjectReference::NULL
        } else {
            ObjectReference::from_raw_address(
                BASE.load(Ordering::Relaxed) + ((v as usize) << SHIFT.load(Ordering::Relaxed)),
            )
        }
    }
}

impl<const COMPRESSED: bool> Edge for OpenJDKEdge<COMPRESSED> {
    fn load(&self) -> ObjectReference {
        if cfg!(any(target_arch = "x86", target_arch = "x86_64")) {
            if COMPRESSED {
                if self.is_compressed() {
                    Self::decompress(self.x86_read_unaligned::<u32, true>())
                } else {
                    self.x86_read_unaligned::<ObjectReference, true>()
                }
            } else {
                self.x86_read_unaligned::<ObjectReference, false>()
            }
        } else {
            debug_assert!(!COMPRESSED);
            unsafe { self.addr.load() }
        }
    }

    fn store(&self, object: ObjectReference) {
        if cfg!(any(target_arch = "x86", target_arch = "x86_64")) {
            if COMPRESSED {
                if self.is_compressed() {
                    self.x86_write_unaligned::<u32, true>(Self::compress(object))
                } else {
                    self.x86_write_unaligned::<ObjectReference, true>(object)
                }
            } else {
                self.x86_write_unaligned::<ObjectReference, false>(object)
            }
        } else {
            debug_assert!(!COMPRESSED);
            unsafe { self.addr.store(object) }
        }
    }
}

/// A range of OpenJDKEdge, usually used for arrays.
#[derive(Clone, PartialEq, Eq, Hash, Debug)]
pub struct OpenJDKEdgeRange<const COMPRESSED: bool> {
    pub range: Range<OpenJDKEdge<COMPRESSED>>,
}

impl<const COMPRESSED: bool> From<Range<Address>> for OpenJDKEdgeRange<COMPRESSED> {
    fn from(value: Range<Address>) -> Self {
        Self {
            range: Range {
                start: value.start.into(),
                end: value.end.into(),
            },
        }
    }
}

pub struct OpenJDKEdgeRangeIterator<const COMPRESSED: bool> {
    cursor: Address,
    limit: Address,
}

impl<const COMPRESSED: bool> Iterator for OpenJDKEdgeRangeIterator<COMPRESSED> {
    type Item = OpenJDKEdge<COMPRESSED>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.cursor >= self.limit {
            None
        } else {
            let edge = self.cursor;
            self.cursor += OpenJDKEdge::<COMPRESSED>::BYTES_IN_EDGE;
            Some(edge.into())
        }
    }
}

impl<const COMPRESSED: bool> Into<Range<Address>> for OpenJDKEdgeRange<COMPRESSED> {
    fn into(self) -> Range<Address> {
        self.range.start.addr..self.range.end.addr
    }
}

// Note that we cannot implement MemorySlice for `Range<OpenJDKEdgeRange>` because neither
// `MemorySlice` nor `Range<T>` are defined in the `mmtk-openjdk` crate. ("orphan rule")
impl<const COMPRESSED: bool> MemorySlice for OpenJDKEdgeRange<COMPRESSED> {
    type Edge = OpenJDKEdge<COMPRESSED>;
    type EdgeIterator = OpenJDKEdgeRangeIterator<COMPRESSED>;

    fn iter_edges(&self) -> Self::EdgeIterator {
        OpenJDKEdgeRangeIterator {
            cursor: self.range.start.addr,
            limit: self.range.end.addr,
        }
    }

    fn object(&self) -> Option<ObjectReference> {
        None
    }

    fn start(&self) -> Address {
        self.range.start.addr
    }

    fn bytes(&self) -> usize {
        self.range.end.addr - self.range.start.addr
    }

    fn copy(src: &Self, tgt: &Self) {
        debug_assert_eq!(src.bytes(), tgt.bytes());
        debug_assert_eq!(
            src.bytes() & ((1 << LOG_BYTES_IN_ADDRESS) - 1),
            0,
            "bytes are not a multiple of words"
        );
        // Raw memory copy
        if COMPRESSED {
            unsafe {
                let words = tgt.bytes() >> LOG_BYTES_IN_INT;
                let src = src.start().to_ptr::<u32>();
                let tgt = tgt.start().to_mut_ptr::<u32>();
                std::ptr::copy(src, tgt, words)
            }
        } else {
            Range::<Address>::copy(&src.clone().into(), &tgt.clone().into())
        }
    }
}
