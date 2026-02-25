use std::{
    ops::Range,
    sync::atomic::{AtomicBool, AtomicU32, AtomicUsize, Ordering},
};

use super::abi::LOG_BYTES_IN_INT;
use atomic::Atomic;
use mmtk::{
    util::{constants::LOG_BYTES_IN_WORD, Address, ObjectReference},
    vm::slot::{MemorySlice, Slot},
};

static USE_COMPRESSED_OOPS: AtomicBool = AtomicBool::new(false);
pub static BASE: Atomic<Address> = Atomic::new(Address::ZERO);
pub static SHIFT: AtomicUsize = AtomicUsize::new(0);

/// Enables compressed oops
///
/// This function can only be called once during MMTkHeap::initialize.
pub fn enable_compressed_oops() {
    static COMPRESSED_OOPS_INITIALIZED: AtomicBool = AtomicBool::new(false);
    assert!(
        !COMPRESSED_OOPS_INITIALIZED.fetch_or(true, Ordering::Relaxed),
        "cannot enable compressed pointers twice."
    );
    if cfg!(not(target_arch = "x86_64")) {
        panic!("Compressed pointer is only enable on x86_64 platforms.\
            For other RISC architectures, we need to find a way to process compressed embeded pointers in code objects first.");
    }
    USE_COMPRESSED_OOPS.store(true, Ordering::Relaxed)
}

/// Check if the compressed pointer is enabled
pub fn use_compressed_oops() -> bool {
    USE_COMPRESSED_OOPS.load(Ordering::Relaxed)
}

/// Set compressed pointer base and shift based on heap range
pub fn initialize_compressed_oops_base_and_shift() {
    let heap_start = mmtk::memory_manager::starting_heap_address().as_usize();
    let heap_end = mmtk::memory_manager::last_heap_address().as_usize();
    if cfg!(feature = "force_narrow_oop_mode") {
        println!("heap_start: 0x{:x}", heap_start);
        println!("heap_end: 0x{:x}", heap_end);
    }
    if heap_end <= (4usize << 30) {
        BASE.store(Address::ZERO, Ordering::Relaxed);
        SHIFT.store(0, Ordering::Relaxed);
    } else if heap_end <= (32usize << 30) {
        BASE.store(Address::ZERO, Ordering::Relaxed);
        SHIFT.store(3, Ordering::Relaxed);
    } else if cfg!(feature = "narrow_oop_mode_base") && (heap_end - heap_start) <= (4usize << 30) {
        // set heap base as HEAP_START - 4096, to make sure null pointer value is not conflict with HEAP_START
        BASE.store(
            mmtk::memory_manager::starting_heap_address() - 4096,
            Ordering::Relaxed,
        );
        SHIFT.store(0, Ordering::Relaxed);
    } else {
        // set heap base as HEAP_START - 4096, to make sure null pointer value does not conflict with HEAP_START
        BASE.store(
            mmtk::memory_manager::starting_heap_address() - 4096,
            Ordering::Relaxed,
        );
        SHIFT.store(3, Ordering::Relaxed);
    }
}

/// The type of slots in OpenJDK.
/// Currently it has the same layout as `Address`, but we override its load and store methods.
///
/// If `COMPRESSED = false`, every slot is uncompressed.
///
/// If `COMPRESSED = true`,
/// * If this is a field of an object, the slot is compressed.
/// * If this is a root pointer: The c++ part of the binding should pass all the root pointers to
///   rust as tagged pointers.
///   * If the 63rd bit of the pointer is set to 1, the value referenced by the pointer is a
///     32-bit compressed integer.
///   * Otherwise, it is a uncompressed root pointer.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
#[repr(transparent)]
pub struct OpenJDKSlot<const COMPRESSED: bool> {
    pub addr: Address,
}

impl<const COMPRESSED: bool> From<Address> for OpenJDKSlot<COMPRESSED> {
    fn from(value: Address) -> Self {
        Self { addr: value }
    }
}
impl<const COMPRESSED: bool> OpenJDKSlot<COMPRESSED> {
    pub const LOG_BYTES_IN_SLOT: usize = if COMPRESSED { 2 } else { 3 };
    pub const BYTES_IN_SLOT: usize = 1 << Self::LOG_BYTES_IN_SLOT;

    const MASK: usize = 1usize << 63;

    /// Check if the pointer is tagged as "compressed"
    const fn is_compressed(&self) -> bool {
        self.addr.as_usize() & Self::MASK == 0
    }

    /// Get the slot address with tags stripped
    const fn untagged_address(&self) -> Address {
        unsafe { Address::from_usize(self.addr.as_usize() << 1 >> 1) }
    }

    fn x86_read_unaligned<T, const UNTAG: bool>(&self) -> T {
        const {
            assert!(cfg!(any(target_arch = "x86", target_arch = "x86_64")));
        }
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
        const {
            assert!(cfg!(any(target_arch = "x86", target_arch = "x86_64")));
        }
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

    /// encode an object pointer to its u32 compressed form
    fn compress(o: Option<ObjectReference>) -> u32 {
        let Some(o) = o else {
            return 0;
        };
        ((o.to_raw_address() - BASE.load(Ordering::Relaxed)) >> SHIFT.load(Ordering::Relaxed))
            as u32
    }

    /// decode an object pointer from its u32 compressed form
    fn decompress(v: u32) -> Option<ObjectReference> {
        if v == 0 {
            None
        } else {
            // Note on `unsafe`: `v` must be positive here, so the result must be positive.
            let objref = unsafe {
                ObjectReference::from_raw_address_unchecked(
                    BASE.load(Ordering::Relaxed) + ((v as usize) << SHIFT.load(Ordering::Relaxed)),
                )
            };
            Some(objref)
        }
    }

    /// Store a null reference in the slot.
    pub fn store_null(&self) {
        if cfg!(any(target_arch = "x86", target_arch = "x86_64")) {
            if COMPRESSED {
                if self.is_compressed() {
                    self.x86_write_unaligned::<u32, true>(0)
                } else {
                    self.x86_write_unaligned::<Address, true>(Address::ZERO)
                }
            } else {
                self.x86_write_unaligned::<Address, false>(Address::ZERO)
            }
        } else {
            debug_assert!(!COMPRESSED);
            unsafe { self.addr.store(0) }
        }
    }
}

impl<const COMPRESSED: bool> Slot for OpenJDKSlot<COMPRESSED> {
    fn load(&self) -> Option<ObjectReference> {
        if cfg!(any(target_arch = "x86", target_arch = "x86_64")) {
            if COMPRESSED {
                if self.is_compressed() {
                    Self::decompress(self.x86_read_unaligned::<u32, true>())
                } else {
                    let addr = self.x86_read_unaligned::<Address, true>();
                    ObjectReference::from_raw_address(addr)
                }
            } else {
                let addr = self.x86_read_unaligned::<Address, false>();
                ObjectReference::from_raw_address(addr)
            }
        } else {
            debug_assert!(!COMPRESSED);
            unsafe { self.addr.load() }
        }
    }

    fn store(&self, object: Option<ObjectReference>) {
        if cfg!(any(target_arch = "x86", target_arch = "x86_64")) {
            if COMPRESSED {
                if self.is_compressed() {
                    self.x86_write_unaligned::<u32, true>(Self::compress(object))
                } else {
                    self.x86_write_unaligned::<Option<ObjectReference>, true>(object)
                }
            } else {
                self.x86_write_unaligned::<Option<ObjectReference>, false>(object)
            }
        } else {
            debug_assert!(!COMPRESSED);
            unsafe { self.addr.store(object) }
        }
    }

    fn to_address(&self) -> Address {
        self.untagged_address()
    }

    fn raw_address(&self) -> Address {
        self.addr
    }

    fn from_address(a: Address) -> Self {
        Self { addr: a }
    }

    fn compare_exchange(
        &self,
        old_object: Option<ObjectReference>,
        new_object: Option<ObjectReference>,
        success: Ordering,
        failure: Ordering,
    ) -> Result<Option<ObjectReference>, Option<ObjectReference>> {
        if COMPRESSED {
            if self.is_compressed() {
                let old_value = Self::compress(old_object);
                let new_value = Self::compress(new_object);
                let slot = self.untagged_address();
                unsafe {
                    match slot.compare_exchange::<AtomicU32>(old_value, new_value, success, failure)
                    {
                        Ok(v) => Ok(Self::decompress(v)),
                        Err(v) => Err(Self::decompress(v)),
                    }
                }
            } else {
                let slot = self.untagged_address();
                let old_value = old_object
                    .map(|o| o.to_raw_address().as_usize())
                    .unwrap_or(0);
                let new_value = new_object
                    .map(|o| o.to_raw_address().as_usize())
                    .unwrap_or(0);
                unsafe {
                    match slot
                        .compare_exchange::<AtomicUsize>(old_value, new_value, success, failure)
                    {
                        Ok(v) => Ok(ObjectReference::from_raw_address(Address::from_usize(v))),
                        Err(v) => Err(ObjectReference::from_raw_address(Address::from_usize(v))),
                    }
                }
            }
        } else {
            let old_value = old_object
                .map(|o| o.to_raw_address().as_usize())
                .unwrap_or(0);
            let new_value = new_object
                .map(|o| o.to_raw_address().as_usize())
                .unwrap_or(0);
            unsafe {
                match self
                    .addr
                    .compare_exchange::<AtomicUsize>(old_value, new_value, success, failure)
                {
                    Ok(v) => Ok(ObjectReference::from_raw_address(Address::from_usize(v))),
                    Err(v) => Err(ObjectReference::from_raw_address(Address::from_usize(v))),
                }
            }
        }
    }
}

/// A range of OpenJDKSlot, usually used for arrays.
#[derive(Clone, PartialEq, Eq, Hash, Debug)]
pub struct OpenJDKSlotRange<const COMPRESSED: bool> {
    range: Range<OpenJDKSlot<COMPRESSED>>,
}

impl<const COMPRESSED: bool> From<Range<Address>> for OpenJDKSlotRange<COMPRESSED> {
    fn from(value: Range<Address>) -> Self {
        Self {
            range: Range {
                start: value.start.into(),
                end: value.end.into(),
            },
        }
    }
}

pub struct ChunkIterator<const COMPRESSED: bool> {
    cursor: Address,
    limit: Address,
    step: usize,
}

impl<const COMPRESSED: bool> Iterator for ChunkIterator<COMPRESSED> {
    type Item = OpenJDKSlotRange<COMPRESSED>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.cursor >= self.limit {
            None
        } else {
            let start = self.cursor;
            let mut end = start + self.step;
            if end > self.limit {
                end = self.limit;
            }
            self.cursor = end;
            Some((start..end).into())
        }
    }
}

pub struct OpenJDKSlotRangeIterator<const COMPRESSED: bool> {
    cursor: Address,
    limit: Address,
}

impl<const COMPRESSED: bool> Iterator for OpenJDKSlotRangeIterator<COMPRESSED> {
    type Item = OpenJDKSlot<COMPRESSED>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.cursor >= self.limit {
            None
        } else {
            let slot = self.cursor;
            self.cursor += OpenJDKSlot::<COMPRESSED>::BYTES_IN_SLOT;
            Some(slot.into())
        }
    }
}

impl<const COMPRESSED: bool> From<OpenJDKSlotRange<COMPRESSED>> for Range<Address> {
    fn from(value: OpenJDKSlotRange<COMPRESSED>) -> Self {
        value.range.start.addr..value.range.end.addr
    }
}

// Note that we cannot implement MemorySlice for `Range<OpenJDKSlot>` because neither
// `MemorySlice` nor `Range<T>` are defined in the `mmtk-openjdk` crate. ("orphan rule")
impl<const COMPRESSED: bool> MemorySlice for OpenJDKSlotRange<COMPRESSED> {
    type SlotType = OpenJDKSlot<COMPRESSED>;
    type SlotIterator = OpenJDKSlotRangeIterator<COMPRESSED>;
    type ChunkIterator = ChunkIterator<COMPRESSED>;

    fn iter_slots(&self) -> Self::SlotIterator {
        OpenJDKSlotRangeIterator {
            cursor: self.range.start.addr,
            limit: self.range.end.addr,
        }
    }

    fn chunks(&self, chunk_size: usize) -> Self::ChunkIterator {
        ChunkIterator {
            cursor: self.range.start.addr,
            limit: self.range.end.addr,
            step: chunk_size << OpenJDKSlot::<COMPRESSED>::LOG_BYTES_IN_SLOT,
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

    fn len(&self) -> usize {
        self.bytes() >> OpenJDKSlot::<COMPRESSED>::LOG_BYTES_IN_SLOT
    }

    fn copy(src: &Self, tgt: &Self) {
        debug_assert_eq!(src.bytes(), tgt.bytes());
        // Raw memory copy
        if COMPRESSED {
            debug_assert_eq!(
                src.bytes() & ((1 << LOG_BYTES_IN_INT) - 1),
                0,
                "bytes are not a multiple of 32-bit integers"
            );
            unsafe {
                let words = tgt.bytes() >> LOG_BYTES_IN_INT;
                let src = src.start().to_ptr::<u32>();
                let tgt = tgt.start().to_mut_ptr::<u32>();
                std::ptr::copy(src, tgt, words)
            }
        } else {
            debug_assert_eq!(
                src.bytes() & ((1 << LOG_BYTES_IN_WORD) - 1),
                0,
                "bytes are not a multiple of words"
            );
            Range::<Address>::copy(&src.clone().into(), &tgt.clone().into())
        }
    }

    fn get(&self, index: usize) -> Self::SlotType {
        let addr = self.range.start.addr + (index << OpenJDKSlot::<COMPRESSED>::LOG_BYTES_IN_SLOT);
        addr.into()
    }
}
