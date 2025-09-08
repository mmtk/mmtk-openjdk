#!/usr/bin/env python3

def enrich_meta_extra(log_processor, name, tid, ts, gc, wp, args):
    if wp is not None:
        match name:
            case "code_cache_roots":
                nursery, mature, num_nmethods = int(args[0]), int(args[1]), int(args[2])
                total = nursery + mature
                wp["args"] |= {
                    "nursery_slots": nursery,
                    "mature_slots": mature,
                    "total_slots": total,
                    "nmethods to fix relocations": num_nmethods,
                }

                # Replicate the statistics in the big "GC" block for convenience.
                gc["args"] |= {
                    "nmethods to fix relocaitons": num_nmethods,
                }

            case "fix_relocations":
                wp["args"] |= {
                    "num_nmethods": int(args[0]),
                }
