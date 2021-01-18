#ifndef MMTK_H
#define MMTK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// #ifndef MMTK_GC_NOGC
// #define MMTK_GC_NOGC 0
// #endif

// #ifndef MMTK_GC_SEMISPACE
// #define MMTK_GC_SEMISPACE 0
// #endif

// #ifndef MMTK_GC_GENCOPY
// #define MMTK_GC_GENCOPY 0
// #endif

typedef void* MMTk_Mutator;
typedef void* MMTk_TraceLocal;

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
    int bytes, int allocator);

extern void record_modified_node(MMTk_Mutator mutator, void* obj);
extern void record_modified_edge(MMTk_Mutator mutator, void* slot);

extern void release_buffer(void** buffer, size_t len, size_t cap);

extern bool is_mapped_object(void* ref);
extern bool is_mapped_address(void* addr);
extern void modify_check(void* ref);

// This type declaration needs to match AllocatorSelector in mmtk-core
struct AllocatorSelector {
    uint8_t tag;
    uint8_t index;
};
#define TAG_BUMP_POINTER 0
#define TAG_LARGE_OBJECT 1

extern AllocatorSelector get_allocator_mapping(int allocator);

/**
 * Misc
 */
extern void enable_collection(void *tls);
extern void gc_init(size_t heap_size);
extern bool will_never_move(void* object);
extern bool process(char* name, char* value);
extern void scan_region();
extern void handle_user_collection_request(void *tls);

extern void start_control_collector(void *tls);
extern void start_worker(void *tls, void* worker);

/**
 * VM Accounting
 */
extern size_t free_bytes();
extern size_t total_bytes();

typedef struct {
    void** buf;
    size_t cap;
} NewBuffer;

typedef NewBuffer (*ProcessEdgesFn)(void** buf, size_t len, size_t cap);

/**
 * OpenJDK-specific
 */
typedef struct {
    void (*stop_all_mutators) (void *tls, void (*create_stack_scan_work)(void* mutator));
    void (*resume_mutators) (void *tls);
    void (*spawn_collector_thread) (void *tls, void *ctx);
    void (*block_for_gc) ();
    void* (*active_collector) (void* tls);
    void* (*get_next_mutator) ();
    void (*reset_mutator_iterator) ();
    void (*compute_static_roots) (void* trace, void* tls);
    void (*compute_global_roots) (void* trace, void* tls);
    void (*compute_thread_roots) (void* trace, void* tls);
    void (*scan_object) (void* trace, void* object, void* tls);
    void (*dump_object) (void* object);
    size_t (*get_object_size) (void* object);
    void* (*get_mmtk_mutator) (void* tls);
    bool (*is_mutator) (void* tls);
    int (*enter_vm) ();
    void (*leave_vm) (int st);
    size_t (*compute_klass_mem_layout_checksum) ();
    int (*offset_of_static_fields) ();
    int (*static_oop_field_count_offset) ();
    int (*referent_offset) ();
    int (*discovered_offset) ();
    char* (*dump_object_string) (void* object);
    void (*scan_thread_roots)(ProcessEdgesFn process_edges, void* tls);
    void (*scan_thread_root)(ProcessEdgesFn process_edges, void* tls);
    void (*scan_universe_roots) (ProcessEdgesFn process_edges);
    void (*scan_jni_handle_roots) (ProcessEdgesFn process_edges);
    void (*scan_object_synchronizer_roots) (ProcessEdgesFn process_edges);
    void (*scan_management_roots) (ProcessEdgesFn process_edges);
    void (*scan_jvmti_export_roots) (ProcessEdgesFn process_edges);
    void (*scan_aot_loader_roots) (ProcessEdgesFn process_edges);
    void (*scan_system_dictionary_roots) (ProcessEdgesFn process_edges);
    void (*scan_code_cache_roots) (ProcessEdgesFn process_edges);
    void (*scan_string_table_roots) (ProcessEdgesFn process_edges);
    void (*scan_class_loader_data_graph_roots) (ProcessEdgesFn process_edges);
    void (*scan_weak_processor_roots) (ProcessEdgesFn process_edges);
    void (*scan_vm_thread_roots) (ProcessEdgesFn process_edges);
    size_t (*number_of_mutators)();
} OpenJDK_Upcalls;

extern void openjdk_gc_init(OpenJDK_Upcalls *calls, size_t heap_size);
extern bool openjdk_needs_write_barrier();

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

extern void harness_begin(void *tls);
extern void harness_end();

#ifdef __cplusplus
}
#endif

#endif // MMTK_H