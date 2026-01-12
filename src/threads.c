#include "threads.h"
#include "search.h"
#include "evaluation.h"
#include "tablebase.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#endif

// ===== GLOBAL THREAD POOL =====
ThreadPool thread_pool;

// ===== PLATFORM ABSTRACTION IMPLEMENTATION =====

#ifdef _WIN32

int thread_create(thread_t *thread, void *(*func)(void *), void *arg)
{
    *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    return (*thread == NULL) ? -1 : 0;
}

int thread_join(thread_t thread)
{
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}

void mutex_init(mutex_t *mutex)
{
    InitializeCriticalSection(mutex);
}

void mutex_destroy(mutex_t *mutex)
{
    DeleteCriticalSection(mutex);
}

void mutex_lock(mutex_t *mutex)
{
    EnterCriticalSection(mutex);
}

void mutex_unlock(mutex_t *mutex)
{
    LeaveCriticalSection(mutex);
}

void cond_init(cond_t *cond)
{
    InitializeConditionVariable(cond);
}

void cond_destroy(cond_t *cond)
{
    // Windows condition variables don't need explicit destruction
    (void)cond;
}

void cond_signal(cond_t *cond)
{
    WakeConditionVariable(cond);
}

void cond_broadcast(cond_t *cond)
{
    WakeAllConditionVariable(cond);
}

void cond_wait(cond_t *cond, mutex_t *mutex)
{
    SleepConditionVariableCS(cond, mutex, INFINITE);
}

int get_time_ms(void)
{
    return (int)GetTickCount64();
}

void atomic_add_nodes(volatile uint64_t *counter, uint64_t value)
{
    InterlockedExchangeAdd64((volatile LONG64 *)counter, (LONG64)value);
}

uint64_t atomic_load_nodes(volatile uint64_t *counter)
{
    return InterlockedCompareExchange64((volatile LONG64 *)counter, 0, 0);
}

#else // POSIX

int thread_create(thread_t *thread, void *(*func)(void *), void *arg)
{
    return pthread_create(thread, NULL, func, arg);
}

int thread_join(thread_t thread)
{
    return pthread_join(thread, NULL);
}

void mutex_init(mutex_t *mutex)
{
    pthread_mutex_init(mutex, NULL);
}

void mutex_destroy(mutex_t *mutex)
{
    pthread_mutex_destroy(mutex);
}

void mutex_lock(mutex_t *mutex)
{
    pthread_mutex_lock(mutex);
}

void mutex_unlock(mutex_t *mutex)
{
    pthread_mutex_unlock(mutex);
}

void cond_init(cond_t *cond)
{
    pthread_cond_init(cond, NULL);
}

void cond_destroy(cond_t *cond)
{
    pthread_cond_destroy(cond);
}

void cond_signal(cond_t *cond)
{
    pthread_cond_signal(cond);
}

void cond_broadcast(cond_t *cond)
{
    pthread_cond_broadcast(cond);
}

void cond_wait(cond_t *cond, mutex_t *mutex)
{
    pthread_cond_wait(cond, mutex);
}

int get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void atomic_add_nodes(volatile uint64_t *counter, uint64_t value)
{
    __sync_fetch_and_add(counter, value);
}

uint64_t atomic_load_nodes(volatile uint64_t *counter)
{
    return __sync_fetch_and_add(counter, 0);
}

#endif

// ===== FORWARD DECLARATIONS =====
static void *thread_idle_loop(void *arg);
static void thread_search(Thread *thread);

// ===== THREAD POOL IMPLEMENTATION =====

// Create a search state for a thread (without TT - uses shared TT)
static SearchState *create_thread_search_state(void)
{
    SearchState *state = (SearchState *)malloc(sizeof(SearchState));
    if (!state)
        return NULL;

    // Don't create TT - thread will use shared TT
    state->tt = NULL;
    state->pawn_tt = NULL;
    state->eval_tt = NULL;

    state->max_depth = 32;
    state->max_time_ms = 5000;
    state->nodes_limit = 0;
    state->nodes = 0;
    state->qnodes = 0;
    state->tt_hits = 0;
    state->pv_length = 0;
    state->ply = 0;

    // Initialize move ordering structures
    memset(state->killer_moves, 0, sizeof(state->killer_moves));
    memset(state->history, 0, sizeof(state->history));
    memset(state->counter_moves, 0, sizeof(state->counter_moves));
    memset(state->countermove_history, 0, sizeof(state->countermove_history));
    memset(state->capture_history, 0, sizeof(state->capture_history));
    memset(state->prev_piece, -1, sizeof(state->prev_piece));
    memset(state->prev_to, -1, sizeof(state->prev_to));
    memset(state->repetition_history, 0, sizeof(state->repetition_history));
    state->repetition_ply = 0;
    memset(state->last_move, 0, sizeof(state->last_move));

    state->previous_score = 0;
    state->aspiration_fail_high_count = 0;
    state->aspiration_fail_low_count = 0;
    state->contempt = 20;

    // Statistics
    state->lmr_reductions = 0;
    state->null_cutoffs = 0;
    state->futility_prunes = 0;
    state->rfp_prunes = 0;
    state->lmp_prunes = 0;
    state->see_prunes = 0;
    state->probcut_prunes = 0;
    state->extensions = 0;

    return state;
}

static void init_thread(Thread *thread, int id)
{
    thread->thread_id = id;
    thread->is_main = (id == 0);
    thread->searching = false;
    thread->exit = false;
    thread->stop = false;
    thread->best_move = MOVE_NONE;
    thread->best_score = -SCORE_INFINITE;
    thread->nodes = 0;
    thread->qnodes = 0;
    thread->depth = 0;
    thread->max_depth = MAX_DEPTH;
    thread->max_time_ms = 0;

    // Create search state for this thread
    thread->search = create_thread_search_state();

    // Initialize root position
    position_init(&thread->root_pos);
}

void threads_init(int num_threads, int tt_size_mb)
{
    if (num_threads < 1)
        num_threads = 1;
    if (num_threads > MAX_THREADS)
        num_threads = MAX_THREADS;

    // Initialize synchronization primitives
    mutex_init(&thread_pool.mutex);
    cond_init(&thread_pool.sleep_cond);
    cond_init(&thread_pool.stop_cond);

    // Create shared transposition tables
    thread_pool.shared_tt = tt_create(tt_size_mb);
    thread_pool.shared_pawn_tt = pawn_tt_create(1024); // 1MB pawn TT
    thread_pool.shared_eval_tt = eval_tt_create(2048); // 2MB eval TT

    thread_pool.stop_all = false;
    thread_pool.total_nodes = 0;
    thread_pool.total_tbhits = 0;
    thread_pool.num_threads = num_threads;
    thread_pool.start_time_ms = 0;
    thread_pool.allocated_time_ms = 0;
    thread_pool.max_time_ms = 0;

    // Initialize and create threads
    for (int i = 0; i < num_threads; i++)
    {
        init_thread(&thread_pool.threads[i], i);

        // Point thread's search state to shared TT
        if (thread_pool.threads[i].search)
        {
            thread_pool.threads[i].search->tt = thread_pool.shared_tt;
            thread_pool.threads[i].search->pawn_tt = thread_pool.shared_pawn_tt;
            thread_pool.threads[i].search->eval_tt = thread_pool.shared_eval_tt;
        }

        // Start worker threads (not main thread 0)
        if (i > 0)
        {
            thread_create(&thread_pool.threads[i].native_thread,
                          thread_idle_loop,
                          &thread_pool.threads[i]);
        }
    }
}

void threads_destroy(void)
{
    // Signal all threads to exit
    mutex_lock(&thread_pool.mutex);
    thread_pool.stop_all = true;
    for (int i = 0; i < thread_pool.num_threads; i++)
    {
        thread_pool.threads[i].exit = true;
        thread_pool.threads[i].stop = true;
    }
    cond_broadcast(&thread_pool.sleep_cond);
    mutex_unlock(&thread_pool.mutex);

    // Wait for worker threads to finish
    for (int i = 1; i < thread_pool.num_threads; i++)
    {
        thread_join(thread_pool.threads[i].native_thread);
    }

    // Free search states
    for (int i = 0; i < thread_pool.num_threads; i++)
    {
        if (thread_pool.threads[i].search)
        {
            // Don't free TT pointers - they're shared
            thread_pool.threads[i].search->tt = NULL;
            thread_pool.threads[i].search->pawn_tt = NULL;
            thread_pool.threads[i].search->eval_tt = NULL;
            free(thread_pool.threads[i].search);
        }
    }

    // Free shared resources
    if (thread_pool.shared_tt)
        tt_delete(thread_pool.shared_tt);
    if (thread_pool.shared_pawn_tt)
        pawn_tt_delete(thread_pool.shared_pawn_tt);
    if (thread_pool.shared_eval_tt)
        eval_tt_delete(thread_pool.shared_eval_tt);

    // Destroy synchronization primitives
    mutex_destroy(&thread_pool.mutex);
    cond_destroy(&thread_pool.sleep_cond);
    cond_destroy(&thread_pool.stop_cond);
}

void threads_set_count(int num_threads)
{
    if (num_threads < 1)
        num_threads = 1;
    if (num_threads > MAX_THREADS)
        num_threads = MAX_THREADS;

    // If increasing thread count, create new threads
    if (num_threads > thread_pool.num_threads)
    {
        for (int i = thread_pool.num_threads; i < num_threads; i++)
        {
            init_thread(&thread_pool.threads[i], i);

            if (thread_pool.threads[i].search)
            {
                thread_pool.threads[i].search->tt = thread_pool.shared_tt;
                thread_pool.threads[i].search->pawn_tt = thread_pool.shared_pawn_tt;
                thread_pool.threads[i].search->eval_tt = thread_pool.shared_eval_tt;
            }

            thread_create(&thread_pool.threads[i].native_thread,
                          thread_idle_loop,
                          &thread_pool.threads[i]);
        }
    }
    // If decreasing, signal excess threads to exit
    else if (num_threads < thread_pool.num_threads)
    {
        mutex_lock(&thread_pool.mutex);
        for (int i = num_threads; i < thread_pool.num_threads; i++)
        {
            thread_pool.threads[i].exit = true;
            thread_pool.threads[i].stop = true;
        }
        cond_broadcast(&thread_pool.sleep_cond);
        mutex_unlock(&thread_pool.mutex);

        // Wait for excess threads
        for (int i = num_threads; i < thread_pool.num_threads; i++)
        {
            thread_join(thread_pool.threads[i].native_thread);
            if (thread_pool.threads[i].search)
            {
                thread_pool.threads[i].search->tt = NULL;
                thread_pool.threads[i].search->pawn_tt = NULL;
                thread_pool.threads[i].search->eval_tt = NULL;
                free(thread_pool.threads[i].search);
                thread_pool.threads[i].search = NULL;
            }
        }
    }

    thread_pool.num_threads = num_threads;
}

int threads_get_count(void)
{
    return thread_pool.num_threads;
}

// ===== THREAD IDLE LOOP =====

static void *thread_idle_loop(void *arg)
{
    Thread *thread = (Thread *)arg;

    while (!thread->exit)
    {
        mutex_lock(&thread_pool.mutex);

        // Wait until we're told to search or exit
        while (!thread->searching && !thread->exit)
        {
            cond_wait(&thread_pool.sleep_cond, &thread_pool.mutex);
        }

        mutex_unlock(&thread_pool.mutex);

        if (thread->exit)
            break;

        if (thread->searching)
        {
            // Perform the search
            thread_search(thread);

            // Mark as done
            mutex_lock(&thread_pool.mutex);
            thread->searching = false;
            cond_signal(&thread_pool.stop_cond);
            mutex_unlock(&thread_pool.mutex);
        }
    }

    return NULL;
}

// ===== THREAD SEARCH FUNCTION =====

// Forward declaration of iterative deepening from search.c
extern Move iterative_deepening(SearchState *state, Position *pos, int max_time_ms);

static void thread_search(Thread *thread)
{
    if (!thread->search)
        return;

    // Reset thread state
    thread->stop = false;
    thread->nodes = 0;
    thread->qnodes = 0;
    thread->best_move = MOVE_NONE;
    thread->best_score = -SCORE_INFINITE;

    // Set up search state
    thread->search->max_depth = thread->max_depth;
    thread->search->max_time_ms = thread->max_time_ms;
    thread->search->nodes = 0;
    thread->search->qnodes = 0;

    // For Lazy SMP, helper threads search slightly different depths
    // to ensure work distribution
    int depth_offset = thread->is_main ? 0 : (thread->thread_id % 3);
    thread->search->max_depth = thread->max_depth + depth_offset;

    // Perform iterative deepening search
    // The search will check threads_should_stop() periodically
    Position pos_copy = thread->root_pos;

    Move best = iterative_deepening(thread->search, &pos_copy, thread->max_time_ms);

    // Store results
    thread->best_move = best;
    thread->best_score = thread->search->previous_score;
    thread->nodes = thread->search->nodes;
    thread->qnodes = thread->search->qnodes;

    // Add to global node count
    atomic_add_nodes(&thread_pool.total_nodes, thread->nodes);
}

// ===== SEARCH CONTROL =====

Move threads_start_search(const Position *pos, int max_depth, int max_time_ms)
{
    // Reset global state
    thread_pool.stop_all = false;
    thread_pool.total_nodes = 0;
    thread_pool.total_tbhits = 0;
    thread_pool.start_time_ms = get_time_ms();
    thread_pool.allocated_time_ms = max_time_ms;
    thread_pool.max_time_ms = max_time_ms * 2; // Allow some overrun for important moves

    // Increment TT generation
    tt_new_search(thread_pool.shared_tt);

    // Set up all threads with the position
    mutex_lock(&thread_pool.mutex);

    for (int i = 0; i < thread_pool.num_threads; i++)
    {
        Thread *thread = &thread_pool.threads[i];

        thread->root_pos = *pos;
        thread->max_depth = max_depth;
        thread->max_time_ms = max_time_ms;
        thread->stop = false;
        thread->best_move = MOVE_NONE;
        thread->best_score = -SCORE_INFINITE;
        thread->searching = true;
    }

    // Wake up helper threads
    cond_broadcast(&thread_pool.sleep_cond);
    mutex_unlock(&thread_pool.mutex);

    // Main thread (0) searches directly
    thread_search(&thread_pool.threads[0]);

    // Signal all threads to stop
    threads_stop();

    // Wait for helper threads to finish
    threads_wait();

    // Find best result among all threads
    // Prefer main thread's result, but use helper if it found a clearly better move
    Move best_move = thread_pool.threads[0].best_move;
    Score best_score = thread_pool.threads[0].best_score;

    for (int i = 1; i < thread_pool.num_threads; i++)
    {
        Thread *thread = &thread_pool.threads[i];

        // Use helper's result if it searched deeper and found a significantly better move
        if (thread->best_move != MOVE_NONE &&
            thread->best_score > best_score + 50)
        {
            best_move = thread->best_move;
            best_score = thread->best_score;
        }
    }

    return best_move;
}

void threads_stop(void)
{
    thread_pool.stop_all = true;
    for (int i = 0; i < thread_pool.num_threads; i++)
    {
        thread_pool.threads[i].stop = true;
    }
}

void threads_wait(void)
{
    mutex_lock(&thread_pool.mutex);

    // Wait for all threads to finish searching
    for (int i = 1; i < thread_pool.num_threads; i++)
    {
        while (thread_pool.threads[i].searching && !thread_pool.threads[i].exit)
        {
            cond_wait(&thread_pool.stop_cond, &thread_pool.mutex);
        }
    }

    mutex_unlock(&thread_pool.mutex);
}

bool threads_should_stop(void)
{
    // Check global stop flag
    if (thread_pool.stop_all)
        return true;

    // Check time limit (only main thread checks time to avoid overhead)
    if (thread_pool.allocated_time_ms > 0)
    {
        int elapsed = get_time_ms() - thread_pool.start_time_ms;
        if (elapsed >= thread_pool.max_time_ms)
        {
            thread_pool.stop_all = true;
            return true;
        }
    }

    return false;
}

// ===== UTILITY =====

uint64_t threads_get_nodes(void)
{
    uint64_t total = 0;
    for (int i = 0; i < thread_pool.num_threads; i++)
    {
        total += thread_pool.threads[i].nodes;
    }
    return total;
}

SearchState *threads_get_main_search(void)
{
    return thread_pool.threads[0].search;
}

void threads_report_best_move(int thread_id, Move move, Score score, int depth)
{
    // Only main thread reports via UCI
    if (thread_id != 0)
        return;

    // The actual reporting is handled in iterative_deepening
    (void)move;
    (void)score;
    (void)depth;
}
