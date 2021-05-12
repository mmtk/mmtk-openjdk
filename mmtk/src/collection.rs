use mmtk::scheduler::{ProcessEdgesWork, ScanStackRoot};
use mmtk::scheduler::{GCWorker, WorkBucketStage};
use mmtk::util::OpaquePointer;
use mmtk::vm::{Collection, Scanning, VMBinding};
use mmtk::{Mutator, MutatorContext};

use crate::OpenJDK;
use crate::{SINGLETON, UPCALLS};

pub struct VMCollection {}

extern "C" fn create_mutator_scan_work<E: ProcessEdgesWork<VM = OpenJDK>>(
    mutator: &'static mut Mutator<OpenJDK>,
) {
    mmtk::memory_manager::add_work_packet(&SINGLETON, WorkBucketStage::Prepare, ScanStackRoot::<E>(mutator));
}

impl Collection<OpenJDK> for VMCollection {
    fn stop_all_mutators<E: ProcessEdgesWork<VM = OpenJDK>>(tls: OpaquePointer) {
        let f = {
            if <OpenJDK as VMBinding>::VMScanning::SCAN_MUTATORS_IN_SAFEPOINT {
                0usize as _
            } else {
                create_mutator_scan_work::<E> as *const extern "C" fn(&'static mut Mutator<OpenJDK>)
            }
        };
        unsafe {
            ((*UPCALLS).stop_all_mutators)(tls, f);
        }
    }

    fn resume_mutators(tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).resume_mutators)(tls);
        }
    }

    fn block_for_gc(_tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).block_for_gc)();
        }
    }

    fn spawn_worker_thread(tls: OpaquePointer, ctx: Option<&GCWorker<OpenJDK>>) {
        let ctx_ptr = if let Some(r) = ctx {
            r as *const GCWorker<OpenJDK> as *mut GCWorker<OpenJDK>
        } else {
            std::ptr::null_mut()
        };
        unsafe {
            ((*UPCALLS).spawn_worker_thread)(tls, ctx_ptr as usize as _);
        }
    }

    fn prepare_mutator<T: MutatorContext<OpenJDK>>(
        _tls_w: OpaquePointer,
        _tls_m: OpaquePointer,
        _m: &T,
    ) {
        // unimplemented!()
    }

    fn schedule_finalization(_tls: OpaquePointer) {
        unsafe {
            ((*UPCALLS).schedule_finalizer)();
        }
    }
}
