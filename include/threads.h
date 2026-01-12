#ifndef THREADS_H
#define THREADS_H

#include "position.h"
#include "search.h"
#include "types.h"
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE thread_t;
typedef CRITICAL_SECTION mutex_t;
typedef CONDITION_VARIABLE cond_t;
#else
#include <pthread.h>
typedef pthread_t thread_t;
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t cond_t;
#endif

// ===== LAZY SMP CONFIGURATION =====

#define MAX_THREADS 64
#define DEFAULT_THREADS 1

// ===== THREAD DATA STRUCTURE =====

typedef struct Thread
{
    // Thread identification
    int thread_id;
    thread_t native_thread;

    // Search state (each thread has its own)
    SearchState *search;
    Position root_pos; // Copy of root position for this thread

    // Search parameters
    int depth;       // Current depth to search
    int max_depth;   // Maximum depth
    int max_time_ms; // Time limit

    // Search results
    Move best_move;
    Score best_score;
    int nodes;
    int qnodes;

    // Thread control
    volatile bool searching; // True when thread is actively searching
    volatile bool exit;      // True when thread should terminate
    volatile bool stop;      // True when search should stop early

    // For main thread synchronization
    bool is_main; // True if this is the main thread

} Thread;

// ===== THREAD POOL =====

typedef struct ThreadPool
{
    Thread threads[MAX_THREADS];
    int num_threads;

    // Shared resources
    TranspositionTable *shared_tt; // All threads share the TT
    PawnHashTable *shared_pawn_tt;
    EvalHashTable *shared_eval_tt;

    // Global stop flag (volatile for visibility across threads)
    volatile bool stop_all;

    // Synchronization
    mutex_t mutex;
    cond_t sleep_cond; // Condition for sleeping threads
    cond_t stop_cond;  // Condition for stopping search

    // Aggregated statistics
    volatile uint64_t total_nodes;
    volatile int total_tbhits;

    // Search timing
    int start_time_ms;
    int allocated_time_ms;
    int max_time_ms;

} ThreadPool;

// Global thread pool
extern ThreadPool thread_pool;

// ===== THREAD POOL FUNCTIONS =====

// Initialize thread pool with specified number of threads
// Creates shared TT and worker threads
void threads_init(int num_threads, int tt_size_mb);

// Cleanup thread pool
void threads_destroy(void);

// Set number of threads (can be called to resize)
void threads_set_count(int num_threads);

// Get current thread count
int threads_get_count(void);

// ===== SEARCH CONTROL =====

// Start parallel search from given position
// Returns best move found
Move threads_start_search(const Position *pos, int max_depth, int max_time_ms);

// Stop all threads immediately
void threads_stop(void);

// Wait for all threads to finish
void threads_wait(void);

// Check if search should stop (time limit, stop command, etc.)
bool threads_should_stop(void);

// ===== UTILITY =====

// Get total nodes searched across all threads
uint64_t threads_get_nodes(void);

// Get main thread's search state (for UCI output)
SearchState *threads_get_main_search(void);

// Signal that a thread found a new best move (for info output)
void threads_report_best_move(int thread_id, Move move, Score score, int depth);

// ===== PLATFORM ABSTRACTION =====

// Thread creation/joining
int thread_create(thread_t *thread, void *(*func)(void *), void *arg);
int thread_join(thread_t thread);

// Mutex operations
void mutex_init(mutex_t *mutex);
void mutex_destroy(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);

// Condition variable operations
void cond_init(cond_t *cond);
void cond_destroy(cond_t *cond);
void cond_signal(cond_t *cond);
void cond_broadcast(cond_t *cond);
void cond_wait(cond_t *cond, mutex_t *mutex);

// Atomic operations for statistics
void atomic_add_nodes(volatile uint64_t *counter, uint64_t value);
uint64_t atomic_load_nodes(volatile uint64_t *counter);

// Get current time in milliseconds
int get_time_ms(void);

#endif // THREADS_H
