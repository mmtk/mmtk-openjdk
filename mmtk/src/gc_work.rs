use super::{OpenJDK, UPCALLS};
use crate::scanning::create_process_edges_work;
use mmtk::scheduler::*;
use mmtk::MMTK;
use std::marker::PhantomData;

pub struct ScanUniverseRoots<E: ProcessEdgesWork<VM = OpenJDK>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = OpenJDK>> ScanUniverseRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = OpenJDK>> GCWork<OpenJDK> for ScanUniverseRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        unsafe {
            ((*UPCALLS).scan_universe_roots)(create_process_edges_work::<E> as _);
        }
    }
}

pub struct ScanJNIHandlesRoots<E: ProcessEdgesWork<VM = OpenJDK>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = OpenJDK>> ScanJNIHandlesRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = OpenJDK>> GCWork<OpenJDK> for ScanJNIHandlesRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        unsafe {
            ((*UPCALLS).scan_jni_handle_roots)(create_process_edges_work::<E> as _);
        }
    }
}

pub struct ScanObjectSynchronizerRoots<E: ProcessEdgesWork<VM = OpenJDK>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = OpenJDK>> ScanObjectSynchronizerRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = OpenJDK>> GCWork<OpenJDK> for ScanObjectSynchronizerRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        unsafe {
            ((*UPCALLS).scan_object_synchronizer_roots)(create_process_edges_work::<E> as _);
        }
    }
}

pub struct ScanManagementRoots<E: ProcessEdgesWork<VM = OpenJDK>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = OpenJDK>> ScanManagementRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = OpenJDK>> GCWork<OpenJDK> for ScanManagementRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        unsafe {
            ((*UPCALLS).scan_management_roots)(create_process_edges_work::<E> as _);
        }
    }
}

pub struct ScanJvmtiExportRoots<E: ProcessEdgesWork<VM = OpenJDK>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = OpenJDK>> ScanJvmtiExportRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = OpenJDK>> GCWork<OpenJDK> for ScanJvmtiExportRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        unsafe {
            ((*UPCALLS).scan_jvmti_export_roots)(create_process_edges_work::<E> as _);
        }
    }
}

pub struct ScanAOTLoaderRoots<E: ProcessEdgesWork<VM = OpenJDK>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = OpenJDK>> ScanAOTLoaderRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = OpenJDK>> GCWork<OpenJDK> for ScanAOTLoaderRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        unsafe {
            ((*UPCALLS).scan_aot_loader_roots)(create_process_edges_work::<E> as _);
        }
    }
}

pub struct ScanSystemDictionaryRoots<E: ProcessEdgesWork<VM = OpenJDK>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = OpenJDK>> ScanSystemDictionaryRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = OpenJDK>> GCWork<OpenJDK> for ScanSystemDictionaryRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        unsafe {
            ((*UPCALLS).scan_system_dictionary_roots)(create_process_edges_work::<E> as _);
        }
    }
}

pub struct ScanCodeCacheRoots<E: ProcessEdgesWork<VM = OpenJDK>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = OpenJDK>> ScanCodeCacheRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = OpenJDK>> GCWork<OpenJDK> for ScanCodeCacheRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        unsafe {
            ((*UPCALLS).scan_code_cache_roots)(create_process_edges_work::<E> as _);
        }
    }
}

pub struct ScanStringTableRoots<E: ProcessEdgesWork<VM = OpenJDK>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = OpenJDK>> ScanStringTableRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = OpenJDK>> GCWork<OpenJDK> for ScanStringTableRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        unsafe {
            ((*UPCALLS).scan_string_table_roots)(create_process_edges_work::<E> as _);
        }
    }
}

pub struct ScanClassLoaderDataGraphRoots<E: ProcessEdgesWork<VM = OpenJDK>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = OpenJDK>> ScanClassLoaderDataGraphRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = OpenJDK>> GCWork<OpenJDK> for ScanClassLoaderDataGraphRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        unsafe {
            ((*UPCALLS).scan_class_loader_data_graph_roots)(create_process_edges_work::<E> as _);
        }
    }
}

pub struct ScanWeakProcessorRoots<E: ProcessEdgesWork<VM = OpenJDK>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = OpenJDK>> ScanWeakProcessorRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = OpenJDK>> GCWork<OpenJDK> for ScanWeakProcessorRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        unsafe {
            ((*UPCALLS).scan_weak_processor_roots)(create_process_edges_work::<E> as _);
        }
    }
}

pub struct ScanVMThreadRoots<E: ProcessEdgesWork<VM = OpenJDK>>(PhantomData<E>);

impl<E: ProcessEdgesWork<VM = OpenJDK>> ScanVMThreadRoots<E> {
    pub fn new() -> Self {
        Self(PhantomData)
    }
}

impl<E: ProcessEdgesWork<VM = OpenJDK>> GCWork<OpenJDK> for ScanVMThreadRoots<E> {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        unsafe {
            ((*UPCALLS).scan_vm_thread_roots)(create_process_edges_work::<E> as _);
        }
    }
}
