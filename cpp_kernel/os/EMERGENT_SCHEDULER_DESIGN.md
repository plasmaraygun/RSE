# Emergent Scheduler Design

**Goal**: Eliminate the global scheduler bottleneck that makes old hardware feel slow.

**Key Insight**: In a braided system, scheduling doesn't need to be centralized. It can **emerge** from local decisions.

---

## The Problem with Traditional Schedulers

### **Traditional OS (Linux, Windows, macOS)**

```
Global Run Queue
    ├─ Process 1 (waiting)
    ├─ Process 2 (waiting)
    ├─ Process 3 (waiting)
    └─ Process N (waiting)
         ↓
   Global Scheduler
         ↓
    CPU cores (1, 2, 3, ...)
```

**Problems**:
1. **Single point of contention** - All processes compete for scheduler lock
2. **Cache thrashing** - Processes bounce between cores
3. **Overhead scales with N** - More processes = slower scheduling
4. **Doesn't scale** - Bottleneck gets worse with more cores

**On old hardware**: The scheduler overhead becomes a larger % of total CPU time → system feels sluggish

---

## The Braided Solution: Emergent Scheduling

### **No Global Scheduler**

Instead of one scheduler managing all processes, each **torus schedules its own processes**:

```
Torus A                 Torus B                 Torus C
├─ Process 1           ├─ Process 4           ├─ Process 7
├─ Process 2           ├─ Process 5           ├─ Process 8
└─ Process 3           └─ Process 6           └─ Process 9
     ↓                      ↓                      ↓
Local Scheduler        Local Scheduler        Local Scheduler
     ↓                      ↓                      ↓
   Core 1                 Core 2                 Core 3
```

**Benefits**:
1. **No global contention** - Each torus schedules independently
2. **Cache locality** - Processes stay on same core
3. **O(1) overhead** - Scheduling time doesn't grow with N
4. **Scales perfectly** - More cores = more independent schedulers

---

## How It Works

### **1. Process Placement**

When a process is created, it's assigned to a torus:

```cpp
// Simple round-robin placement
int target_torus = next_pid % 3;

// Or load-based placement
int target_torus = find_least_loaded_torus();
```

Once assigned, the process **stays on that torus** (unless migrated for load balancing).

### **2. Local Scheduling**

Each torus maintains its own **local run queue**:

```cpp
class TorusScheduler {
    FixedVector<OSProcess*, MAX_PROCESSES> ready_queue;
    OSProcess* current_process = nullptr;
    
    OSProcess* schedule() {
        if (ready_queue.empty()) {
            return nullptr;  // Idle
        }
        
        // Simple round-robin (can be more sophisticated)
        OSProcess* next = ready_queue[0];
        ready_queue.erase_at(0);
        ready_queue.push_back(next);
        
        return next;
    }
};
```

**No locks needed** - Each torus operates independently.

### **3. Context Switching**

When it's time to switch processes:

```cpp
void TorusScheduler::tick() {
    // Check if current process should be preempted
    if (current_process && current_process->time_slice_expired()) {
        // Save context
        current_process->save_context();
        
        // Put back in ready queue
        ready_queue.push_back(current_process);
        current_process = nullptr;
    }
    
    // Pick next process
    if (!current_process) {
        current_process = schedule();
        
        if (current_process) {
            // Restore context
            current_process->restore_context();
        }
    }
    
    // Run current process
    if (current_process) {
        current_process->execute();
    }
}
```

### **4. Load Balancing (Optional)**

If one torus gets overloaded, processes can migrate:

```cpp
void BraidCoordinator::balance_load() {
    // Check load across tori
    int load_a = torus_a->get_process_count();
    int load_b = torus_b->get_process_count();
    int load_c = torus_c->get_process_count();
    
    // If imbalance > threshold, migrate
    if (load_a > load_b + 5) {
        OSProcess* proc = torus_a->pick_migratable_process();
        torus_b->receive_process(proc);
    }
}
```

**Migration is rare** - Only happens when load is very imbalanced.

---

## Process States

Each process has a state:

```cpp
enum ProcessState {
    READY,      // Waiting to run
    RUNNING,    // Currently executing
    BLOCKED,    // Waiting for I/O or event
    ZOMBIE      // Terminated, waiting for parent
};
```

State transitions:

```
    READY ←→ RUNNING
      ↓         ↓
   BLOCKED   ZOMBIE
```

---

## Process Structure

```cpp
struct OSProcess {
    // Identity
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t torus_id;
    
    // State
    ProcessState state;
    uint64_t time_slice;
    uint64_t total_runtime;
    
    // Context (CPU registers)
    struct Context {
        uint64_t rip;  // Instruction pointer
        uint64_t rsp;  // Stack pointer
        uint64_t rbp;  // Base pointer
        uint64_t rax, rbx, rcx, rdx;  // General purpose
        // ... more registers
    } context;
    
    // Memory
    uint64_t* page_table;
    uint64_t heap_start;
    uint64_t heap_end;
    uint64_t stack_start;
    uint64_t stack_end;
    
    // I/O
    FileDescriptor* open_files[MAX_FDS];
    
    // Scheduling
    uint32_t priority;
    uint64_t last_scheduled;
    
    // Methods
    void save_context();
    void restore_context();
    void execute();
};
```

---

## Scheduling Algorithms

### **Phase 6.1: Simple Round-Robin**

```cpp
OSProcess* schedule() {
    // Just pick the next ready process
    return ready_queue.pop_front();
}
```

**Pros**: Simple, fair  
**Cons**: No priority, no I/O optimization

### **Phase 6.2: Priority-Based**

```cpp
OSProcess* schedule() {
    // Pick highest priority ready process
    OSProcess* best = nullptr;
    for (auto* proc : ready_queue) {
        if (!best || proc->priority > best->priority) {
            best = proc;
        }
    }
    return best;
}
```

**Pros**: Important processes get more CPU  
**Cons**: Low-priority processes can starve

### **Phase 6.3: Completely Fair Scheduler (CFS-like)**

```cpp
OSProcess* schedule() {
    // Pick process with least total runtime
    OSProcess* best = nullptr;
    for (auto* proc : ready_queue) {
        if (!best || proc->total_runtime < best->total_runtime) {
            best = proc;
        }
    }
    return best;
}
```

**Pros**: Fair, prevents starvation  
**Cons**: More complex

---

## Integration with Braided System

### **Each Torus = One Scheduler**

```cpp
class BraidedKernelV5 : public BraidedKernelV4 {
private:
    TorusScheduler scheduler_;
    
public:
    void tick() override {
        // 1. Run scheduler
        scheduler_.tick();
        
        // 2. Process events (existing RSE logic)
        BraidedKernelV4::tick();
        
        // 3. Update heartbeat
        updateHeartbeat();
    }
};
```

### **Braid Coordination**

At braid exchanges, tori can share load information:

```cpp
struct ProjectionV5 : public ProjectionV4 {
    // Existing fields...
    
    // New: Scheduler state
    uint32_t num_ready_processes;
    uint32_t num_blocked_processes;
    double avg_load;
};
```

This enables **load balancing** without a global coordinator.

---

## Why This Makes Old Hardware Fast

### **Traditional OS on Old Hardware**

```
1000 processes → Global scheduler → 1 old CPU
                      ↓
        Scheduler overhead = 10% of CPU time
                      ↓
              Only 90% for actual work
```

### **Braided OS on Old Hardware**

```
1000 processes → 3 local schedulers → 3 cores (even old ones)
                      ↓
        Scheduler overhead = 1% per torus
                      ↓
              99% for actual work
              + 3× parallelism
```

**Result**: Old hardware runs **3× faster** just from better scheduling!

---

## Implementation Plan

### **Phase 6.1: Basic Process Abstraction**
- Define OSProcess structure
- Implement process creation/destruction
- Basic context (no actual CPU context yet)

### **Phase 6.2: Simple Scheduler**
- Round-robin scheduler per torus
- Time slice management
- Process state transitions

### **Phase 6.3: Context Switching**
- Save/restore CPU registers
- Switch stack pointers
- Handle interrupts

### **Phase 6.4: Load Balancing**
- Detect imbalance across tori
- Migrate processes
- Update projections

### **Phase 6.5: Advanced Scheduling**
- Priority-based scheduling
- I/O-aware scheduling
- Real-time support (optional)

---

## Success Criteria

### **Functional**
- ✅ Processes can be created and destroyed
- ✅ Scheduler picks processes fairly
- ✅ Context switching works correctly
- ✅ Load balances across tori

### **Performance**
- ✅ Scheduling overhead < 1% per torus
- ✅ Context switch time < 1μs
- ✅ Scales linearly with cores
- ✅ Old hardware feels responsive

---

## Next Steps

1. **Implement OSProcess structure**
2. **Implement TorusScheduler**
3. **Integrate with BraidedKernelV5**
4. **Test with simple workloads**
5. **Benchmark against traditional schedulers**

---

## The Vision

**Traditional OS**: One slow scheduler managing everything  
**Braided OS**: Three fast schedulers working independently

**Result**: Old hardware runs like new hardware.

**This is how we unlock the potential of old machines.** 🚀
