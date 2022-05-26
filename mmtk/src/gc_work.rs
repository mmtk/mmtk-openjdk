use super::{OpenJDK, UPCALLS};
use crate::scanning::ScopedDynamicFactoryInvoker;
use mmtk::scheduler::*;
use mmtk::vm::RootsWorkFactory;
use mmtk::MMTK;

pub struct ScanUniverseRoots {
    factory: Box<dyn RootsWorkFactory>,
}

impl ScanUniverseRoots {
    pub fn new(factory: Box<dyn RootsWorkFactory>) -> Self {
        Self { factory }
    }
}

impl GCWork<OpenJDK> for ScanUniverseRoots {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        let invoker = ScopedDynamicFactoryInvoker::new(self.factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_universe_roots)(invoker.as_closure());
        }
    }
}

pub struct ScanJNIHandlesRoots {
    factory: Box<dyn RootsWorkFactory>,
}

impl ScanJNIHandlesRoots {
    pub fn new(factory: Box<dyn RootsWorkFactory>) -> Self {
        Self { factory }
    }
}

impl GCWork<OpenJDK> for ScanJNIHandlesRoots {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        let invoker = ScopedDynamicFactoryInvoker::new(self.factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_jni_handle_roots)(invoker.as_closure());
        }
    }
}

pub struct ScanObjectSynchronizerRoots {
    factory: Box<dyn RootsWorkFactory>,
}

impl ScanObjectSynchronizerRoots {
    pub fn new(factory: Box<dyn RootsWorkFactory>) -> Self {
        Self { factory }
    }
}

impl GCWork<OpenJDK> for ScanObjectSynchronizerRoots {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        let invoker = ScopedDynamicFactoryInvoker::new(self.factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_object_synchronizer_roots)(invoker.as_closure());
        }
    }
}

pub struct ScanManagementRoots {
    factory: Box<dyn RootsWorkFactory>,
}

impl ScanManagementRoots {
    pub fn new(factory: Box<dyn RootsWorkFactory>) -> Self {
        Self { factory }
    }
}

impl GCWork<OpenJDK> for ScanManagementRoots {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        let invoker = ScopedDynamicFactoryInvoker::new(self.factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_management_roots)(invoker.as_closure());
        }
    }
}

pub struct ScanJvmtiExportRoots {
    factory: Box<dyn RootsWorkFactory>,
}

impl ScanJvmtiExportRoots {
    pub fn new(factory: Box<dyn RootsWorkFactory>) -> Self {
        Self { factory }
    }
}

impl GCWork<OpenJDK> for ScanJvmtiExportRoots {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        let invoker = ScopedDynamicFactoryInvoker::new(self.factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_jvmti_export_roots)(invoker.as_closure());
        }
    }
}

pub struct ScanAOTLoaderRoots {
    factory: Box<dyn RootsWorkFactory>,
}

impl ScanAOTLoaderRoots {
    pub fn new(factory: Box<dyn RootsWorkFactory>) -> Self {
        Self { factory }
    }
}

impl GCWork<OpenJDK> for ScanAOTLoaderRoots {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        let invoker = ScopedDynamicFactoryInvoker::new(self.factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_aot_loader_roots)(invoker.as_closure());
        }
    }
}

pub struct ScanSystemDictionaryRoots {
    factory: Box<dyn RootsWorkFactory>,
}

impl ScanSystemDictionaryRoots {
    pub fn new(factory: Box<dyn RootsWorkFactory>) -> Self {
        Self { factory }
    }
}

impl GCWork<OpenJDK> for ScanSystemDictionaryRoots {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        let invoker = ScopedDynamicFactoryInvoker::new(self.factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_system_dictionary_roots)(invoker.as_closure());
        }
    }
}

pub struct ScanCodeCacheRoots {
    factory: Box<dyn RootsWorkFactory>,
}

impl ScanCodeCacheRoots {
    pub fn new(factory: Box<dyn RootsWorkFactory>) -> Self {
        Self { factory }
    }
}

impl GCWork<OpenJDK> for ScanCodeCacheRoots {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        let invoker = ScopedDynamicFactoryInvoker::new(self.factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_code_cache_roots)(invoker.as_closure());
        }
    }
}

pub struct ScanStringTableRoots {
    factory: Box<dyn RootsWorkFactory>,
}

impl ScanStringTableRoots {
    pub fn new(factory: Box<dyn RootsWorkFactory>) -> Self {
        Self { factory }
    }
}

impl GCWork<OpenJDK> for ScanStringTableRoots {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        let invoker = ScopedDynamicFactoryInvoker::new(self.factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_string_table_roots)(invoker.as_closure());
        }
    }
}

pub struct ScanClassLoaderDataGraphRoots {
    factory: Box<dyn RootsWorkFactory>,
}

impl ScanClassLoaderDataGraphRoots {
    pub fn new(factory: Box<dyn RootsWorkFactory>) -> Self {
        Self { factory }
    }
}

impl GCWork<OpenJDK> for ScanClassLoaderDataGraphRoots {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        let invoker = ScopedDynamicFactoryInvoker::new(self.factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_class_loader_data_graph_roots)(invoker.as_closure());
        }
    }
}

pub struct ScanWeakProcessorRoots {
    factory: Box<dyn RootsWorkFactory>,
}

impl ScanWeakProcessorRoots {
    pub fn new(factory: Box<dyn RootsWorkFactory>) -> Self {
        Self { factory }
    }
}

impl GCWork<OpenJDK> for ScanWeakProcessorRoots {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        let invoker = ScopedDynamicFactoryInvoker::new(self.factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_weak_processor_roots)(invoker.as_closure());
        }
    }
}

pub struct ScanVMThreadRoots {
    factory: Box<dyn RootsWorkFactory>,
}

impl ScanVMThreadRoots {
    pub fn new(factory: Box<dyn RootsWorkFactory>) -> Self {
        Self { factory }
    }
}

impl GCWork<OpenJDK> for ScanVMThreadRoots {
    fn do_work(&mut self, _worker: &mut GCWorker<OpenJDK>, _mmtk: &'static MMTK<OpenJDK>) {
        let invoker = ScopedDynamicFactoryInvoker::new(self.factory.as_ref());
        unsafe {
            ((*UPCALLS).scan_vm_thread_roots)(invoker.as_closure());
        }
    }
}
