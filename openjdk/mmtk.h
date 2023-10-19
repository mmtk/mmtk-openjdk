#ifndef MMTK_OPENJDK_MMTK_H
#define MMTK_OPENJDK_MMTK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* MMTk_Mutator;
typedef void* MMTk_TraceLocal;

// This has the same layout as mmtk::util::alloc::AllocationError
typedef enum {
    HeapOutOfMemory,
    MmapOutOfMemory,
} MMTkAllocationError;

extern const uintptr_t GLOBAL_SIDE_METADATA_BASE_ADDRESS;
extern const uintptr_t GLOBAL_SIDE_METADATA_VM_BASE_ADDRESS;
extern const uintptr_t VO_BIT_ADDRESS;
extern const size_t MMTK_MARK_COMPACT_HEADER_RESERVED_IN_BYTES;
extern const uintptr_t FREE_LIST_ALLOCATOR_SIZE;

extern const char* get_mmtk_version();

/**
 * Allocation
 */
extern MMTk_Mutator bind_mutator(void *tls);
extern void destroy_mutator(MMTk_Mutator mutator);
extern void flush_mutator(MMTk_Mutator mutator);

extern void* alloc(MMTk_Mutator mutator, size_t size,
    size_t align, size_t offset, int allocator);

extern void* alloc_slow_bump_monotone_immortal(MMTk_Mutator mutator, size_t size,
    size_t align, size_t offset);
extern void* alloc_slow_bump_monotone_copy(MMTk_Mutator mutator, size_t size,
    size_t align, size_t offset);
extern void* alloc_slow_largeobject(MMTk_Mutator mutator, size_t size,
    size_t align, size_t offset);

extern void post_alloc(MMTk_Mutator mutator, void* refer,
    size_t bytes, int allocator);

/// Full pre-barrier
extern void mmtk_object_reference_write_pre(MMTk_Mutator mutator, void* src, void* slot, void* target);

/// Full post-barrier
extern void mmtk_object_reference_write_post(MMTk_Mutator mutator, void* src, void* slot, void* target);

/// Generic slow-path
extern void mmtk_object_reference_write_slow(MMTk_Mutator mutator, void* src, void* slot, void* target);

/// Full array-copy pre-barrier
extern void mmtk_array_copy_pre(MMTk_Mutator mutator, void* src, void* dst, size_t count);

/// Full array-copy post-barrier
extern void mmtk_array_copy_post(MMTk_Mutator mutator, void* src, void* dst, size_t count);

/// C2 slowpath allocation barrier
extern void mmtk_object_probable_write(MMTk_Mutator mutator, void* obj);

extern void release_buffer(void** buffer, size_t len, size_t cap);

extern bool is_in_mmtk_spaces(void* ref);
extern bool is_mapped_address(void* addr);

// This type declaration needs to match AllocatorSelector in mmtk-core
struct AllocatorSelector {
    uint8_t tag;
    uint8_t index;
};

#define TAG_BUMP_POINTER              0
#define TAG_LARGE_OBJECT              1
#define TAG_MALLOC                    2
#define TAG_IMMIX                     3
#define TAG_MARK_COMPACT              4
#define TAG_FREE_LIST                 5

extern AllocatorSelector get_allocator_mapping(int allocator);
extern size_t get_max_non_los_default_alloc_bytes();

/**
 * Finalization
 */
extern void add_finalizer(void* obj);
extern void* get_finalized_object();

/**
 * Misc
 */
extern char* mmtk_active_barrier();
extern void initialize_collection(void *tls);
extern void gc_init(size_t heap_size);
extern bool will_never_move(void* object);
extern bool process(char* name, char* value);
extern bool process_bulk(char* options);
extern void scan_region();
extern void handle_user_collection_request(void *tls);

extern void start_control_collector(void *tls, void *context);
extern void start_worker(void *tls, void* worker);

extern size_t mmtk_add_nmethod_oop(void* object);
extern size_t mmtk_register_nmethod(void* nm);
extern size_t mmtk_unregister_nmethod(void* nm);

/**
 * VM Accounting
 */
extern size_t free_bytes();
extern size_t total_bytes();

typedef struct {
    void** buf;
    size_t cap;
} NewBuffer;

struct MutatorClosure {
    void (*func)(MMTk_Mutator mutator, void* data);
    void* data;

    void invoke(MMTk_Mutator mutator) {
        func(mutator, data);
    }
};

struct EdgesClosure {
    NewBuffer (*func)(void** buf, size_t size, size_t capa, void* data);
    void* data;

    NewBuffer invoke(void** buf, size_t size, size_t capa) {
        return func(buf, size, capa, data);
    }
};

/**
 * OpenJDK-specific
 */
typedef struct {
    void (*stop_all_mutators) (void *tls, MutatorClosure closure);
    void (*resume_mutators) (void *tls);
    void (*spawn_gc_thread) (void *tls, int kind, void *ctx);
    void (*block_for_gc) ();
    void (*out_of_memory) (void *tls, MMTkAllocationError err_kind);
    void (*get_mutators) (MutatorClosure closure);
    void (*scan_object) (void* trace, void* object, void* tls);
    void (*dump_object) (void* object);
    size_t (*get_object_size) (void* object);
    void* (*get_mmtk_mutator) (void* tls);
    bool (*is_mutator) (void* tls);
    void (*harness_begin) ();
    void (*harness_end) ();
    size_t (*compute_klass_mem_layout_checksum) ();
    int (*offset_of_static_fields) ();
    int (*static_oop_field_count_offset) ();
    int (*referent_offset) ();
    int (*discovered_offset) ();
    char* (*dump_object_string) (void* object);
    void (*scan_roots_in_all_mutator_threads)(EdgesClosure closure);
    void (*scan_roots_in_mutator_thread)(EdgesClosure closure, void* tls);
    void (*scan_universe_roots) (EdgesClosure closure);
    void (*scan_jni_handle_roots) (EdgesClosure closure);
    void (*scan_object_synchronizer_roots) (EdgesClosure closure);
    void (*scan_management_roots) (EdgesClosure closure);
    void (*scan_jvmti_export_roots) (EdgesClosure closure);
    void (*scan_aot_loader_roots) (EdgesClosure closure);
    void (*scan_system_dictionary_roots) (EdgesClosure closure);
    void (*scan_code_cache_roots) (EdgesClosure closure);
    void (*scan_string_table_roots) (EdgesClosure closure);
    void (*scan_class_loader_data_graph_roots) (EdgesClosure closure);
    void (*scan_weak_processor_roots) (EdgesClosure closure);
    void (*scan_vm_thread_roots) (EdgesClosure closure);
    size_t (*number_of_mutators)();
    void (*schedule_finalizer)();
    void (*prepare_for_roots_re_scanning)();
    void (*enqueue_references)(void** objects, size_t len);
} OpenJDK_Upcalls;

extern void openjdk_gc_init(OpenJDK_Upcalls *calls);
extern bool openjdk_is_gc_initialized();

extern bool mmtk_set_heap_size(size_t min, size_t max);

extern bool mmtk_enable_compressed_oops();
extern void* mmtk_narrow_oop_base();
extern size_t mmtk_narrow_oop_shift();
extern size_t mmtk_set_compressed_klass_base_and_shift(void* base, size_t shift);

extern size_t used_bytes();
extern void* starting_heap_address();
extern void* last_heap_address();
extern void iterator(); // ???


// (It is the total_space - capacity_of_to_space in Semispace )
// PZ: It shouldn't be ...?
extern size_t openjdk_max_capacity();
extern size_t _noaccess_prefix();  // ???
extern size_t _alignment();        // ???
extern bool   executable();

/**
 * Reference Processing
 */
extern void add_weak_candidate(void* ref, void* referent);
extern void add_soft_candidate(void* ref, void* referent);
extern void add_phantom_candidate(void* ref, void* referent);

extern void mmtk_harness_begin_impl();
extern void mmtk_harness_end_impl();

extern void mmtk_builder_read_env_var_settings();
extern void mmtk_builder_set_threads(size_t value);
extern void mmtk_builder_set_transparent_hugepages(bool value);

#ifdef __cplusplus
}
#endif

#endif // MMTK_OPENJDK_MMTK_H
