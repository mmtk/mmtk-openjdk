use std::ops::Range;

use mmtk::{
    util::{Address, ObjectReference},
    vm::edge_shape::{AddressRangeIterator, Edge, MemorySlice},
};

/// The type of edges in OpenJDK.
/// Currently it has the same layout as `Address`, but we override its load and store methods.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
#[repr(transparent)]
pub struct OpenJDKEdge {
    pub addr: Address,
}

impl From<Address> for OpenJDKEdge {
    fn from(value: Address) -> Self {
        Self { addr: value }
    }
}

impl Edge for OpenJDKEdge {
    fn load(&self) -> ObjectReference {
        if cfg!(any(target_arch = "x86", target_arch = "x86_64")) {
            // Workaround: On x86 (including x86_64), machine instructions may contain pointers as
            // immediates, and they may be unaligned.  It is an undefined behavior in Rust to
            // dereference unaligned pointers.  We have to explicitly use unaligned memory access
            // methods.  On x86, ordinary MOV instructions can load and store memory at unaligned
            // addresses, so we expect `ptr.read_unaligned()` to have no performance penalty over
            // `ptr.read()` if `ptr` is actually aligned.
            unsafe {
                let ptr = self.addr.to_ptr::<ObjectReference>();
                ptr.read_unaligned()
            }
        } else {
            unsafe { self.addr.load() }
        }
    }

    fn store(&self, object: ObjectReference) {
        if cfg!(any(target_arch = "x86", target_arch = "x86_64")) {
            unsafe {
                let ptr = self.addr.to_mut_ptr::<ObjectReference>();
                ptr.write_unaligned(object)
            }
        } else {
            unsafe { self.addr.store(object) }
        }
    }
}

/// A range of OpenJDKEdge, usually used for arrays.
#[derive(Clone, PartialEq, Eq, Hash, Debug)]
pub struct OpenJDKEdgeRange {
    range: Range<Address>,
}

impl From<Range<Address>> for OpenJDKEdgeRange {
    fn from(value: Range<Address>) -> Self {
        Self { range: value }
    }
}

pub struct OpenJDKEdgeRangeIterator {
    inner: AddressRangeIterator,
}

impl Iterator for OpenJDKEdgeRangeIterator {
    type Item = OpenJDKEdge;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next().map(|a| a.into())
    }
}

// Note that we cannot implement MemorySlice for `Range<OpenJDKEdgeRange>` because neither
// `MemorySlice` nor `Range<T>` are defined in the `mmtk-openjdk` crate. ("orphan rule")
impl MemorySlice for OpenJDKEdgeRange {
    type Edge = OpenJDKEdge;
    type EdgeIterator = OpenJDKEdgeRangeIterator;

    fn iter_edges(&self) -> Self::EdgeIterator {
        OpenJDKEdgeRangeIterator {
            inner: self.range.iter_edges(),
        }
    }

    fn object(&self) -> Option<ObjectReference> {
        self.range.object()
    }

    fn start(&self) -> Address {
        self.range.start()
    }

    fn bytes(&self) -> usize {
        self.range.bytes()
    }

    fn copy(src: &Self, tgt: &Self) {
        MemorySlice::copy(&src.range, &tgt.range)
    }
}
