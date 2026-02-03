# SptScript

[![Compile Check](https://github.com/Xarvie/spt/actions/workflows/compile.yml/badge.svg?event=push)](https://github.com/Xarvie/spt/actions/workflows/compile.yml)

``` cpp
/*
 * SPT Language Quickstart
 */

// ==========================================
// 1. Constants & Globals
// ==========================================
const string SYSTEM_NAME = "SPT-Core";
const float VERSION = 1.0;

// Enum simulation for task status
const int STATUS_PENDING = 0;
const int STATUS_RUNNING = 1;
const int STATUS_DONE    = 2;
const int STATUS_FAILED  = -1;

// Global statistics counter
global int g_total_tasks = 0;

// ==========================================
// 2. Functions & Multi-Return Values
// ==========================================

/**
 * Generates a unique task ID and an initial hash code.
 * Demonstrates: 'vars' for multiple return values, string concatenation (..)
 */
vars generate_id(string prefix, int index) {
    // String concatenation
    string id = prefix .. "-" .. index .. "-" .. g_ticks(); 
    int hash_code = (index * 1337) % 9999;
    
    // Returning multiple values
    return id, hash_code;
}

/**
 * Simulates getting system time ticks.
 */
int g_ticks() {
    return g_total_tasks * 10; 
}

// ==========================================
// 3. Object-Oriented Programming (Classes)
// ==========================================

/**
 * Represents a single unit of work.
 */
class Task {
    string id;
    int priority;
    int status;
    any payload; // 'any' type can hold data of any type

    // Constructor
    void __init(string id, int prio, any data) {
        this.id = id;
        this.priority = prio;
        this.status = STATUS_PENDING;
        this.payload = data;
    }

    void setStatus(int newStatus) {
        this.status = newStatus;
    }
    
    // Returns a string representation of the object
    string toString() {
        return "[Task " .. this.id .. "] Prio:" .. this.priority;
    }
}

/**
 * Manages a queue of tasks.
 * Demonstrates: List operations, Maps, and Error Handling.
 */
class Processor {
    list<Task> queue;
    map<string, int> stats;
    string name;

    void __init(string name) {
        this.name = name;
        this.queue = [];
        this.stats = {"success": 0, "fail": 0};
    }

    // Fluent Interface: Returns 'this' to allow method chaining
    Processor add(Task t) {
        this.queue.push(t);
        g_total_tasks += 1; 
        return this;
    }

    // Process all tasks in the queue
    void runAll() {
        print("--- Processor " .. this.name .. " Started ---");
        
        // 'defer': Ensures this block runs when the function exits,
        // even if an error occurs during execution.
        defer {
            print("--- Processor " .. this.name .. " Shutdown (Stats: " .. this.stats["success"] .. " ok) ---");
        }

        // Standard C-style loop
        for (int i = 0; i < this.queue.length; i += 1) {
            Task t = this.queue[i];
            
            print("Processing: " .. t.toString());
            
            // 'pcall' (Protected Call): Simulates try-catch.
            // Note: We must pass 'this' explicitly as the first argument to the method.
            vars ok, err = pcall(this.processSingle, this, t);
            
            if (ok) {
                t.setStatus(STATUS_DONE);
                this.stats["success"] += 1;
            } else {
                t.setStatus(STATUS_FAILED);
                this.stats["fail"] += 1;
                print("Error processing task: " .. err);
            }
        }
        
        this.queue.clear();
    }

    // Internal processing logic that might throw an error
    void processSingle(Task t) {
        if (t.priority < 0) {
            error("Invalid priority!"); // Throws a runtime error
        }
        
        // Check payload type
        if (t.payload == null) {
            print("  >> Warning: Empty payload");
        } else {
            print("  >> Payload type: " .. typeOf(t.payload));
        }
    }
}

// ==========================================
// 4. Coroutines (Fibers)
// ==========================================

/**
 * Creates a background monitoring coroutine.
 * Demonstrates: Fiber creation, yield, and Closures.
 */
auto create_monitor = function(int interval) -> any {
    // Fiber accepts a lambda function
    return Fiber.create(function(any _) -> void {
        int cycles = 0;
        while (true) {
            cycles += 1;
            print("[Monitor] Checking system health... Cycle: " .. cycles);
            
            // Suspend execution and return the current cycle count to the caller.
            // Execution resumes here when .call() is invoked again.
            Fiber.yield(cycles); 

            if (cycles >= interval) {
                print("[Monitor] Maintenance required!");
                break;
            }
        }
    });
};

// ==========================================
// 5. Main Execution Entry
// ==========================================

print("Initializing " .. SYSTEM_NAME .. " v" .. VERSION);

// 1. Setup Processor
Processor worker = new Processor("Worker-Alpha");

// 2. Batch create tasks using List and Loops
list<string> user_requests = ["Login", "Download", "Upload", "Logout"];

// 'pairs': A generic iterator for looping through collections
for (auto i, auto req : pairs(user_requests)) {
    // Unpack multiple return values
    vars tid, hash = generate_id("REQ", i);
    
    // Set high priority for even indices
    int prio = 1;
    if (i % 2 == 0) {
        prio = 10;
    }

    Task t = new Task(tid, prio, req);
    worker.add(t);
}

// 3. Add a task that is designed to fail (negative priority)
worker.add(new Task("ERR-001", -5, null)); 

// 4. Coroutine Scheduling Demo
print("\n[System] Starting Monitor Fiber...");
auto monitor_fiber = create_monitor(3); // Monitor runs for 3 cycles

// Start the fiber (Cycle 1)
monitor_fiber.call(null); 

print("\n[System] Running Task Processor...");
// Run the main workload (triggers defer and pcall logic)
worker.runAll();

// Resume the fiber (Cycle 2)
if (!monitor_fiber.isDone) {
    vars cycle = monitor_fiber.call(null);
    print("[System] Monitor yielded cycle: " .. cycle);
}

// 5. Higher-Order Functions & Lambdas
print("\n[System] Calculating Analysis...");

list<int> numbers = [1, 2, 3, 4, 5];

// Define a higher-order function that accepts a transformer function
auto map_list = function(list<int> src, function transformer) -> list<int> {
    list<int> result = [];
    for (int i = 0; i < src.length; i += 1) {
        result.push(transformer(src[i]));
    }
    return result;
};

// Use a Lambda expression to square numbers
list<int> squares = map_list(numbers, function(int x) -> int {
    return x * x;
});

print("Squares: " .. squares.join(", "));

print("\n[System] Shutdown complete.");

```
