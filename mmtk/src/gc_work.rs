use std::sync::atomic::Ordering;

use super::{OpenJDK, UPCALLS};
use mmtk::scheduler::*;
use mmtk::vm::RootsWorkFactory;
use mmtk::MMTK;
use scanning::to_edges_closure;

macro_rules! scan_roots_work {
    ($struct_name: ident, $func_name: ident) => {
        pub struct $struct_name<F: RootsWorkFactory> {
            factory: F,
        }

        impl<F: RootsWorkFactory> $struct_name<F> {
            pub fn new(factory: F) -> Self {
                Self { factory }
            }
        }

        impl<F: RootsWorkFactory> GCWork<OpenJDK> for $struct_name<F> {
            fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
                unsafe {
                    ((*UPCALLS).$func_name)(to_edges_closure(&mut self.factory));
                }
            }
        }
    };
}

scan_roots_work!(ScanUniverseRoots, scan_universe_roots);
scan_roots_work!(ScanJNIHandlesRoots, scan_jni_handle_roots);
scan_roots_work!(ScanObjectSynchronizerRoots, scan_object_synchronizer_roots);
scan_roots_work!(ScanManagementRoots, scan_management_roots);
scan_roots_work!(ScanJvmtiExportRoots, scan_jvmti_export_roots);
scan_roots_work!(ScanAOTLoaderRoots, scan_aot_loader_roots);
scan_roots_work!(ScanSystemDictionaryRoots, scan_system_dictionary_roots);
scan_roots_work!(ScanStringTableRoots, scan_string_table_roots);
scan_roots_work!(
    ScanClassLoaderDataGraphRoots,
    scan_class_loader_data_graph_roots
);
scan_roots_work!(ScanWeakProcessorRoots, scan_weak_processor_roots);
scan_roots_work!(ScanVMThreadRoots, scan_vm_thread_roots);

pub struct ScanCodeCacheRoots<F: RootsWorkFactory> {
    factory: F,
}

impl<F: RootsWorkFactory> ScanCodeCacheRoots<F> {
    pub fn new(factory: F) -> Self {
        Self { factory }
    }
}

impl<F: RootsWorkFactory> GCWork<OpenJDK> for ScanCodeCacheRoots<F> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        // Collect all the cached roots
        let mut edges = Vec::with_capacity(crate::CODE_CACHE_ROOTS_SIZE.load(Ordering::Relaxed));
        for roots in (*crate::CODE_CACHE_ROOTS.lock().unwrap()).values() {
            for r in roots {
                edges.push(*r)
            }
        }
        // Create work packet
        self.factory.create_process_edge_roots_work(edges);
        // Use the following code to scan CodeCache directly, instead of scanning the "remembered set".
        // unsafe {
        //     ((*UPCALLS).scan_code_cache_roots)(create_process_edges_work::<E> as _);
        // }
    }
}
