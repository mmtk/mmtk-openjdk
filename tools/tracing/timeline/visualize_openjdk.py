#!/usr/bin/env python3

def enrich_meta_extra(log_processor, name, tid, ts, gc, wp, args):
    if wp is not None:
        match name:
            case "code_cache_roots":
                nursery, mature = int(args[0]), int(args[1])
                total = nursery + mature
                wp["args"] |= {
                    "nursery_slots": nursery,
                    "mature_slots": mature,
                    "total_slots": total,
                }
