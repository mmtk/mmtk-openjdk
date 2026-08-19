## Progress Summary:

### Introduced a runtime argument -XX:+UseMMTk :  
We are on the way to complete building an interface between the JVM and the rust-mmtk. We are calling it `MMTkHeap`. It is enabled by a command line argument `-XX:+UseMMTk`. make, javac, and java (when this argument is not given) work the same way as before. Only difference is that we have to run an `export` command for every new terminal that intends to execute `make`, `javac`, or `java`.

### Partially implemented BarrierSet and CollectedHeap for MMTk:
Currently `MMTkHeap` tries to `allocate` from rust-mmtk, but there are some unimplemented methods from its superclass, so core dump occurs. We created a `BarrierSet` named `NoBarrier`. We modified several CPU level methods to introduce `NoBarrier` there. There are several classes which require support from rust-mmtk. After the support is added, the implementations of those classes can be completed.

### Testing our work:
The latest commit (**Commit 1b48c800** in mmtk branch) is a buildable and runnable version. We are attaching a [document](./Testing_Openjdk_MMTk_Branch.md) that has instructions to test our code.


## In Details:

### Warm-up and environment setup:
The first several weeks we had to spend some time to explore the *openjdk-10* codebase. We had explored openjdk-8 previously, but back then we didn't have any intention to make such big changes. However, we found a build procedure with rust-mmtk integrated very soon, thanks to Pavel and Isaac.

### First attempt to allocate with rust-mmtk:
At first we tested injecting the allocation codes into the existing system. It was able to allocate using rust-mmtk allocator. But `make` and `javac` also need memory allocation. As it replaced the default allocator, it created some conflicts with the build process of openjdk. We knew that we would need a runtime parameter to select mmtk allocator.

### Runtime argument for enabling MMTk:
Earlier in January, we introduced a runtime parameter to enable rust-mmtk allocator. At first it was running the built-in Parallel GC behind the scene. The best part of this parameter is that a user will not feel existence of our work unless they add this argument from command line. `make` and `javac` work as they did before. So does `java` unless we add `-XX:+UseMMTk` argument from command line.

### Implementing MMTkHeap as a subclass of ParallelScavengeHeap:
JVM needs a data structure named `CollectedHeap` to work properly. Every garbage collector has an instance of this data structure. `CollectedHeap` has some pure abstract methods. So its subclasses need to implement some methods. For example, Parallel GC uses `ParallelScavengeHeap` that extends this class.

So we wanted to create an abstraction of a Heap that would communicate with rust-mmtk. We named it `MMTkHeap`. For the beginning phase we created a class named `MMTkHeap` that extended `ParallelScavengeHeap`. We intended to override the necessary components.
As it had some components of `ParallelScavengeHeap`, it created some conflicts when the VM didn't find what it expected. For example, there are methods like *`block_size`*, *`amount_of_space_used`* etc. When we replaced the allocator with rust-mmtk allocator, some of them were not functioning properly. We discussed it with **Rifat**, our advisor in Bangladesh. He suggested to create `MMTkHeap` without any dependence on any other GC.

### Partial implementation of independent MMTkHeap and NoBarrier:
Therefore, we have made `MMTkHeap` a direct subclass of `CollectedHeap`. As a result we are left with a bunch of *pure abstract methods*. There were also some issue related to barriers. We created a class `NoBarrier` and introduced it to the CPU level methods which perform barrier related tasks before and after memory accesses. However, currently the `NoBarrier` is supported on *JVM for x86 processors only*. Later we will need to add this support for other processors as well.

### Why MMTkHeap, NoBarrier, and MMTkMemoryPool?:
Directly quoted form [http://openjdk.java.net/jeps/304](http://openjdk.java.net/jeps/304)
>More specifically, a garbage collector implementation will have to provide:  
■	The heap, a subclass of CollectedHeap  
■	The barrier set, a subclass of BarrierSet, which implements the various barriers for the runtime  
■	An implementation of CollectorPolicy  
■	An implementation of GCInterpreterSupport, which implements the various barriers for a GC for the interpreter (using assembler instructions)  
■	An implementation of GCC1Support, which implements the various barriers for a GC for the C1 compiler  
■	An implementation of GCC2Support, which implements the various barriers for a GC for the C2 compiler  
■	Initialization of eventual GC specific arguments  
■	Setup of a MemoryService, the related memory pools, memory managers, etc. ”

In simple words, the VM has significant dependence on several GC related classes. We will need to implement abstractions of a `CollectedHeap`, `MemoryPool`, `BarrierSet`, and some other classes to connect rust-mmtk with the JVM. But the actual GC related tasks will be handled by rust-mmtk.

## Issues Ahead:
We think the following tasks will be necessary:
- Complete implementation of `MMTkHeap`
- Complete implementation of `NoBarrier`
- Complete implementation of `MMTkMemoryPool`
- `NoBarrier` support for all processors


We need some support from Rust-MMTK.  Pavel told us that he would look into it. Our vm requires some attributes of the heap at runtime through some abstract methods specified in `ColectedHeap`.  These methods are called by JVM and Universe.  Currently we believe our vm crashes due to these lackings.
 

__Allocation Requirements:__  

    char* _base;  
    size_t _size;  
    size_t Used_bytes  
    size_t max_capacity (It is the total_space - capacity_of_to_space in Semispace )  
    size_t _noaccess_prefix;  
    size_t _alignment;  
    bool   executable;  
    int    _fd_for_heap;  //File descriptor for the heap.We will provide this to Rust-MMTK.

__GC Requirements:__  (when GC is completed in Rust)  

    Last_gc_time  

Some other things may be required.  We will know for sure when we start incorporating it.  
When Pavel gives us methods to access these attributes we can proceed to the garbage collection part. 

__Making facilities for GC:__  
Pavel told us we need to provide the following two things for GC.

__1.Finding Roots :__  
For roots, we have made a plan with Rifat.  We will follow the way psMarkSweep does it. We will incorporate it after allocation gets bug free.  We have gone through the codes extensively to understand how this works.  We  will simply recreate what it does.

What it does is it pushes all its roots in a stack called `marking_stack`.  We will do something similar and pass the data structure. 


__2.Specifying which class members are pointers:__  
We will go through the class OOP for solving this.  This class is for handling objects and holds object attributes.  We have not started working for it yet.  Rifat told us to look into it after allocation is successful.

---
*Thank You*
>*--Abdullah Al Mamun  
 and --Tanveer Hannan*