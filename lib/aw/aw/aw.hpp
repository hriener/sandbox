/*

Copyright (c) 2019, NVIDIA Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

/*

This file introduces std::atomic_wait, atomic_notify_one, atomic_notify_all.

It has these strategies implemented:
 * Contention table. Used to optimize futex notify, or to hold CVs. Disable with __NO_TABLE.
 * Futex. Supported on Linux and Windows. For performance requires a table on Linux. Disable with __NO_FUTEX.
 * Condition variables. Supported on Linux and Mac. Requires table to function. Disable with __NO_CONDVAR.
 * Timed back-off. Supported on everything. Disable with __NO_SLEEP.
 * Spinlock. Supported on everything. Force with __NO_IDENT. Note: performance is too terrible to use.

You can also compare to pure spinning at algorithm level with __NO_WAIT.

The strategy is chosen this way, by platform:
 * Linux: default to futex (with table), fallback to futex (no table) -> CVs -> timed backoff -> spin.
 * Mac: default to CVs (table), fallback to timed backoff -> spin.
 * Windows: default to futex (no table), fallback to timed backoff -> spin.
 * CUDA: default to timed backoff, fallback to spin. (This is not all checked in in this tree.)
 * Unidentified platform: default to spin.

*/

//#define __NO_TABLE
//#define __NO_FUTEX
//#define __NO_CONDVAR
//#define __NO_SLEEP
//#define __NO_IDENT

// To benchmark against spinning
//#define __NO_SPIN
//#define __NO_WAIT

#ifndef __ATOMIC_WAIT_INCLUDED
#define __ATOMIC_WAIT_INCLUDED

#include <cstdint>
#include <climits>
#include <cassert>
#include <type_traits>

#if defined(__NO_IDENT)

    #include <thread>
    #include <chrono>

    #define __ABI
    #define __YIELD() std::this_thread::yield()
    #define __SLEEP(x) std::this_thread::sleep_for(std::chrono::microseconds(x))
    #define __YIELD_PROCESSOR()

#else

#if defined(__CUSTD__)
    #define __NO_FUTEX
    #define __NO_CONDVAR
    #ifndef __CUDACC__
        #define __host__ 
        #define __device__
    #endif
    #define __ABI __host__ __device__
#else
    #define __ABI
#endif

#if defined(__APPLE__) || defined(__linux__)

    #include <unistd.h>
    #include <sched.h>
    #define __YIELD() sched_yield()
    #define __SLEEP(x) usleep(x)

    #if defined(__aarch64__)
        #  define __YIELD_PROCESSOR() asm volatile ("yield" ::: "memory")
    #elif defined(__x86_64__)
        # define __YIELD_PROCESSOR() asm volatile ("pause" ::: "memory")
    #elif defined (__powerpc__)
        # define __YIELD_PROCESSOR() asm volatile ("or 27,27,27" ::: "memory")
    #endif
#endif

#if defined(__linux__) && !defined(__NO_FUTEX)

    #if !defined(__NO_TABLE)
        #define __TABLE
    #endif

    #include <time.h>
    #include <unistd.h>
    #include <linux/futex.h>
    #include <sys/syscall.h>
    
    #define __FUTEX
    #define __FUTEX_TIMED
    #define __type_used_directly(_T) (std::is_same<typename std::remove_const< \
            typename std::remove_volatile<_Tp>::type>::type, __futex_preferred_t>::value)
    using __futex_preferred_t = std::int32_t;
    template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
    void __do_direct_wait(_Tp const* ptr, _Tp val, void const* timeout) {
        syscall(SYS_futex, ptr, FUTEX_WAIT_PRIVATE, val, timeout, 0, 0);
    }
    template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
    void __do_direct_wake(_Tp const* ptr, bool all) {
        syscall(SYS_futex, ptr, FUTEX_WAKE_PRIVATE, all ? INT_MAX : 1, 0, 0, 0);
    }

#elif defined(_WIN32) && !defined(__CUSTD__)

    #define __NO_CONDVAR
    #define __NO_TABLE

    #include <Windows.h>
    #define __YIELD() Sleep(0)
    #define __SLEEP(x) Sleep(x)
    #define __YIELD_PROCESSOR() YieldProcessor()

    #include <intrin.h>
    template <class _Tp>
    auto __atomic_load_n(_Tp const* a, int) -> typename std::remove_reference<decltype(*a)>::type {
        auto const t = *a;
        _ReadWriteBarrier();
        return t;
    }
    #define __builtin_expect(e, v) (e)

    #if defined(_WIN32_WINNT) && (_WIN32_WINNT >= _WIN32_WINNT_WIN8) && !defined(__NO_FUTEX)

        #define __FUTEX
        #define __type_used_directly(_T) (sizeof(_T) <= 8)
        using __futex_preferred_t = std::int64_t;
        template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
        void __do_direct_wait(_Tp const* ptr, _Tp val, void const*) {
            WaitOnAddress((PVOID)ptr, (PVOID)&val, sizeof(_Tp), INFINITE);
        }
        template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
        void __do_direct_wake(_Tp const* ptr, bool all) {
            if (all)
                WakeByAddressAll((PVOID)ptr);
            else
                WakeByAddressSingle((PVOID)ptr);
        }

    #endif
#endif // _WIN32

#if !defined(__FUTEX) && !defined(__NO_CONDVAR)

    #if defined(__NO_TABLE)
        #warning "Condvars always generate a table (ignoring __NO_TABLE)."
    #endif
    #include <pthread.h>
    #define __CONDVAR
    #define __TABLE
#endif

#endif // __NO_IDENT

#ifdef __TABLE
    struct alignas(64) contended_t {
    #if defined(__FUTEX)
        int                     waiters = 0;
        __futex_preferred_t     version = 0;
    #elif defined(__CONDVAR)
        int                     credit = 0;
        pthread_mutex_t         mutex = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t          condvar = PTHREAD_COND_INITIALIZER;
    #else
        #error ""
    #endif
    };
    contended_t * __contention(volatile void const * p);
#else
    template <class _Tp>
    __ABI void __cxx_atomic_try_wait_slow_fallback(_Tp const* ptr, _Tp val, int order) {
    #ifndef __NO_SLEEP
        long history = 10;
        do {
            __SLEEP(history >> 2);
            history += history >> 2;
            if (history > (1 << 10))
                history = 1 << 10;
        } while (__atomic_load_n(ptr, order) == val);
    #else
        __YIELD();
    #endif
    }
#endif // __TABLE

#if defined(__CONDVAR)

    template <class _Tp>
    void __cxx_atomic_notify_all(volatile _Tp const* ptr) {
        auto * const c = __contention(ptr);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if(__builtin_expect(0 == __atomic_load_n(&c->credit, __ATOMIC_RELAXED), 1))
            return;
        if(0 != __atomic_exchange_n(&c->credit, 0, __ATOMIC_RELAXED)) {
            pthread_mutex_lock(&c->mutex);
            pthread_mutex_unlock(&c->mutex);
            pthread_cond_broadcast(&c->condvar);
        }
    }
    template <class _Tp>
    void __cxx_atomic_notify_one(volatile _Tp const* ptr) {
        __cxx_atomic_notify_all(ptr);
    }
    template <class _Tp>
    void __cxx_atomic_try_wait_slow(volatile _Tp const* ptr, _Tp const val, int order) {
        auto * const c = __contention(ptr);
        pthread_mutex_lock(&c->mutex);
        __atomic_store_n(&c->credit, 1, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (val == __atomic_load_n(ptr, order))
            pthread_cond_wait(&c->condvar, &c->mutex);
        pthread_mutex_unlock(&c->mutex);
    }

#elif defined(__FUTEX)

        template <class _Tp, typename std::enable_if<!__type_used_directly(_Tp), int>::type = 1>
        void __cxx_atomic_notify_all(_Tp const* ptr) {
    #if defined(__TABLE)
            auto * const c = __contention(ptr);
            __atomic_fetch_add(&c->version, 1, __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            if (0 != __atomic_exchange_n(&c->waiters, 0, __ATOMIC_RELAXED))
                __do_direct_wake(&c->version, true);
    #endif
        }
        template <class _Tp, typename std::enable_if<!__type_used_directly(_Tp), int>::type = 1>
        void __cxx_atomic_notify_one(_Tp const* ptr) {
            __cxx_atomic_notify_all(ptr);
        }
        template <class _Tp, typename std::enable_if<!__type_used_directly(_Tp), int>::type = 1>
        void __cxx_atomic_try_wait_slow(_Tp const* ptr, _Tp const val, int order) {
    #if defined(__TABLE)
            auto * const c = __contention(ptr);
            __atomic_store_n(&c->waiters, 1, __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            auto const version = __atomic_load_n(&c->version, __ATOMIC_RELAXED);
            if (__builtin_expect(val != __atomic_load_n(ptr, order), 1))
                return;
        #ifdef __FUTEX_TIMED
            constexpr timespec timeout = { 2, 0 }; // Hedge on rare 'int version' aliasing.
            __do_direct_wait(&c->version, version, &timeout);
        #else
            __do_direct_wait(&c->version, version, nullptr);
        #endif
    #else
        __cxx_atomic_try_wait_slow_fallback(ptr, val, order);
    #endif // __TABLE
        }

    template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
    void __cxx_atomic_try_wait_slow(_Tp const* ptr, _Tp val, int order) {
    #ifdef __TABLE
        auto * const c = __contention(ptr);
        __atomic_fetch_add(&c->waiters, 1, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
    #endif
        __do_direct_wait(ptr, val, nullptr);
    #ifdef __TABLE
        __atomic_fetch_sub(&c->waiters, 1, __ATOMIC_RELAXED);
    #endif
    }
    template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
    void __cxx_atomic_notify_all(_Tp const* ptr) {
    #ifdef __TABLE
        auto * const c = __contention(ptr);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (0 != __atomic_load_n(&c->waiters, __ATOMIC_RELAXED))
    #endif
            __do_direct_wake(ptr, true);
    }
    template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
    void __cxx_atomic_notify_one(_Tp const* ptr) {
    #ifdef __TABLE
        auto * const c = __contention(ptr);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (0 != __atomic_load_n(&c->waiters, __ATOMIC_RELAXED))
    #endif
            __do_direct_wake(ptr, false);
    }

#else // __FUTEX || __CONDVAR

    template <class _Tp>
    __ABI void __cxx_atomic_try_wait_slow(_Tp const* ptr, _Tp val, int order) {
        __cxx_atomic_try_wait_slow_fallback(ptr, val, order);
    }
    template <class _Tp>
    __ABI void __cxx_atomic_notify_one(_Tp const* ptr) { }
    template <class _Tp>
    __ABI void __cxx_atomic_notify_all(_Tp const* ptr) { }

#endif // __FUTEX || __CONDVAR

template <class _Tp>
__ABI void __cxx_atomic_wait(_Tp const* ptr, _Tp const val, int order) {
#ifndef __NO_SPIN
    if(__builtin_expect(__atomic_load_n(ptr, order) != val,1))
        return;
    for(int i = 0; i < 16; ++i) {
        if(__atomic_load_n(ptr, order) != val)
            return;
        if(i < 12)
            __YIELD_PROCESSOR();
        else
            __YIELD();
    }
#endif
    while(val == __atomic_load_n(ptr, order))
#ifndef __NO_WAIT
        __cxx_atomic_try_wait_slow(ptr, val, order)
#endif
        ;
}

#include <atomic>

namespace std {

    template <class _Tp, class _Tv>
    __ABI void atomic_wait_explicit(atomic<_Tp> const* a, _Tv val, std::memory_order order) {
        __cxx_atomic_wait((const _Tp*)a, (_Tp)val, (int)order);
    }
    template <class _Tp, class _Tv>
    __ABI void atomic_wait(atomic<_Tp> const* a, _Tv val) {
        atomic_wait_explicit(a, val, std::memory_order_seq_cst);
    }
    template <class _Tp>
    __ABI void atomic_notify_one(atomic<_Tp> const* a) {
        __cxx_atomic_notify_one((const _Tp*)a);
    }
    template <class _Tp>
    __ABI void atomic_notify_all(atomic<_Tp> const* a) {
        __cxx_atomic_notify_all((const _Tp*)a);
    }
}

#endif //__ATOMIC_WAIT_INCLUDED

/*

Copyright (c) 2019, NVIDIA Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <cstddef>
#include <algorithm>
#include <chrono>
#include <thread>

//#define __NO_SEM
//#define __NO_SEM_BACK
//#define __NO_SEM_FRONT
//#define __NO_SEM_POLL

#if defined(__APPLE__) || defined(__linux__)
	#define __semaphore_no_inline __attribute__ ((noinline))
#elif defined(_WIN32)
	#define __semaphore_no_inline __declspec(noinline)
    #include <Windows.h>
	#undef min
	#undef max
#else
    #define __semaphore_no_inline
    #define __NO_SEM
#endif

#ifndef __NO_SEM

#if defined(__APPLE__) 

    #include <dispatch/dispatch.h>

    #define __SEM_POST_ONE
    static constexpr ptrdiff_t __semaphore_max = std::numeric_limits<long>::max();
    using __semaphore_sem_t = dispatch_semaphore_t;

    inline bool __semaphore_sem_init(__semaphore_sem_t &sem, int init) {
        return (sem = dispatch_semaphore_create(init)) != NULL;
    }
    inline bool __semaphore_sem_destroy(__semaphore_sem_t &sem) {
        assert(sem != NULL);
        dispatch_release(sem);
        return true;
    }
    inline bool __semaphore_sem_post(__semaphore_sem_t &sem, int inc) {
        assert(inc == 1);
        dispatch_semaphore_signal(sem);
        return true;
    }
    inline bool __semaphore_sem_wait(__semaphore_sem_t &sem) {
        return dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER) == 0;
    }
    template <class Rep, class Period>
    inline bool __semaphore_sem_wait_timed(__semaphore_sem_t &sem, std::chrono::duration<Rep, Period> const &delta) {
        return dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count())) == 0;
    }

#endif //__APPLE__

#if defined(__linux__)

    #include <unistd.h>
    #include <sched.h>
    #include <semaphore.h>

    #ifndef __NO_SEM_POLL
        #define __NO_SEM_POLL
    #endif
    #define __SEM_POST_ONE
    static constexpr ptrdiff_t __semaphore_max = SEM_VALUE_MAX;
    using __semaphore_sem_t = sem_t;
    
    inline bool __semaphore_sem_init(__semaphore_sem_t &sem, int init) {
        return sem_init(&sem, 0, init) == 0;
    }
    inline bool __semaphore_sem_destroy(__semaphore_sem_t &sem) {
        return sem_destroy(&sem) == 0;
    }
    inline bool __semaphore_sem_post(__semaphore_sem_t &sem, int inc) {
        assert(inc == 1);
        return sem_post(&sem) == 0;
    }
    inline bool __semaphore_sem_wait(__semaphore_sem_t &sem) {
        return sem_wait(&sem) == 0;
    }
    template <class Rep, class Period>
    inline bool __semaphore_sem_wait_timed(__semaphore_sem_t &sem, std::chrono::duration<Rep, Period> const &delta) {
        struct timespec ts;
        ts.tv_sec = static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(delta).count());
        ts.tv_nsec = static_cast<long>(std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count());
        return sem_timedwait(&sem, &ts) == 0;
    }

#endif //__linux__

#if defined(_WIN32)

    #define __NO_SEM_BACK

    static constexpr ptrdiff_t __semaphore_max = std::numeric_limits<long>::max();
    using __semaphore_sem_t = HANDLE;

    inline bool __semaphore_sem_init(__semaphore_sem_t &sem, int init) {
        bool const ret = (sem = CreateSemaphore(NULL, init, INT_MAX, NULL)) != NULL;
        assert(ret);
        return ret;
    }
    inline bool __semaphore_sem_destroy(__semaphore_sem_t &sem) {
        assert(sem != NULL);
        return CloseHandle(sem) == TRUE;
    }
    inline bool __semaphore_sem_post(__semaphore_sem_t &sem, int inc) {
        assert(sem != NULL);
        assert(inc > 0);
        return ReleaseSemaphore(sem, inc, NULL) == TRUE;
    }
    inline bool __semaphore_sem_wait(__semaphore_sem_t &sem) {
        assert(sem != NULL);
        return WaitForSingleObject(sem, INFINITE) == WAIT_OBJECT_0;
    }
    template <class Rep, class Period>
    inline bool __semaphore_sem_wait_timed(__semaphore_sem_t &sem, std::chrono::duration<Rep, Period> const &delta) {
        assert(sem != NULL);
        return WaitForSingleObject(sem, (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(delta).count()) == WAIT_OBJECT_0;
    }

#endif // _WIN32

#endif

namespace std {

class __atomic_semaphore_base {

    __semaphore_no_inline inline bool __fetch_sub_if_slow(ptrdiff_t old) {
        while (old != 0) {
            if (count.compare_exchange_weak(old, old - 1, std::memory_order_acquire, std::memory_order_relaxed))
                return true; 
        }
        return false;
    }
    inline bool __fetch_sub_if() {

        ptrdiff_t old = count.load(std::memory_order_acquire);
        if (old == 0)
            return false;
        if(count.compare_exchange_weak(old, old - 1, std::memory_order_acquire, std::memory_order_relaxed))
            return true;
        return __fetch_sub_if_slow(old); // fail only if not available
    }
    __semaphore_no_inline inline void __wait_slow() {
        while (1) {
            ptrdiff_t const old = count.load(std::memory_order_acquire);
            if(old != 0)
                break;
            atomic_wait_explicit(&count, old, std::memory_order_relaxed);
        }
    }
    __semaphore_no_inline inline bool __acquire_slow_timed(std::chrono::nanoseconds const& rel_time) {

        using __clock = std::conditional<std::chrono::high_resolution_clock::is_steady, 
                                         std::chrono::high_resolution_clock,
                                         std::chrono::steady_clock>::type;

        auto const start = __clock::now();
        while (1) {
            ptrdiff_t const old = count.load(std::memory_order_acquire);
            if(old != 0 && __fetch_sub_if_slow(old))
                return true;
            auto const elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(__clock::now() - start);
            auto const delta = rel_time - elapsed;
            if(delta <= std::chrono::nanoseconds(0))
                return false;
            auto const sleep = std::min((elapsed.count() >> 2) + 100, delta.count());
            std::this_thread::sleep_for(std::chrono::nanoseconds(sleep));
        }
    }
    std::atomic<ptrdiff_t> count;

public:
    static constexpr ptrdiff_t max() noexcept { 
        return std::numeric_limits<ptrdiff_t>::max(); 
    }

    __atomic_semaphore_base(ptrdiff_t count) : count(count) { }

    ~__atomic_semaphore_base() = default;

    __atomic_semaphore_base(__atomic_semaphore_base const&) = delete;
    __atomic_semaphore_base& operator=(__atomic_semaphore_base const&) = delete;

    inline void release(ptrdiff_t update = 1) {
        count.fetch_add(update, std::memory_order_release);
        if(update > 1)
            atomic_notify_all(&count);
        else
            atomic_notify_one(&count);
    }
    inline void acquire() {
        while (!try_acquire())
            __wait_slow();
    }

    inline bool try_acquire() noexcept {
        return __fetch_sub_if();
    }
    template <class Clock, class Duration>
    inline bool try_acquire_until(std::chrono::time_point<Clock, Duration> const& abs_time) {

        if (try_acquire())
            return true;
        else
            return __acquire_slow_timed(abs_time - Clock::now());
    }
    template <class Rep, class Period>
    inline bool try_acquire_for(std::chrono::duration<Rep, Period> const& rel_time) {

        if (try_acquire())
            return true;
        else
            return __acquire_slow_timed(rel_time);
    }
};

#ifndef __NO_SEM

class __semaphore_base {

    inline bool __backfill(bool success) {
#ifndef __NO_SEM_BACK
        if(success) {
            auto const back_amount = __backbuffer.fetch_sub(2, std::memory_order_acquire);
            bool const post_one = back_amount > 0;
            bool const post_two = back_amount > 1;
            auto const success = (!post_one || __semaphore_sem_post(__semaphore, 1)) && 
                                 (!post_two || __semaphore_sem_post(__semaphore, 1));
            assert(success);
            if(!post_one || !post_two)
                __backbuffer.fetch_add(!post_one ? 2 : 1, std::memory_order_relaxed);
        }
#endif
        return success;
    }
    inline bool __try_acquire_fast() {
#ifndef __NO_SEM_FRONT
#ifndef __NO_SEM_POLL
        ptrdiff_t old = __frontbuffer.load(std::memory_order_relaxed);
        if(!(old >> 32)) {
            using __clock = std::conditional<std::chrono::high_resolution_clock::is_steady, 
                                             std::chrono::high_resolution_clock,
                                             std::chrono::steady_clock>::type;
            auto const start = __clock::now();
            old = __frontbuffer.load(std::memory_order_relaxed);
            while(!(old >> 32)) {
                auto const elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(__clock::now() - start);
                if(elapsed > std::chrono::microseconds(5))
                    break;
                std::this_thread::sleep_for((elapsed + std::chrono::nanoseconds(100)) / 4);
            }
        }
#else
        // boldly assume the semaphore is free with a count of 1, just because
        ptrdiff_t old = 1ll << 32;
#endif
        // always steal if you can
        while(old >> 32)
            if(__frontbuffer.compare_exchange_weak(old, old - (1ll << 32), std::memory_order_acquire))
                return true;
        // record we're waiting
        old = __frontbuffer.fetch_add(1ll, std::memory_order_release);
        // ALWAYS steal if you can!
        while(old >> 32)
            if(__frontbuffer.compare_exchange_weak(old, old - (1ll << 32), std::memory_order_acquire))
                break;
        // not going to wait after all
        if(old >> 32)
            return __try_done(true);
#endif
        // the wait has begun...
        return false;
    }
    inline bool __try_done(bool success) {
#ifndef __NO_SEM_FRONT
        // record we're NOT waiting
        __frontbuffer.fetch_sub(1ll, std::memory_order_release);
#endif
        return __backfill(success);
    }
    __semaphore_no_inline inline void __release_slow(ptrdiff_t post_amount) {
#ifdef __SEM_POST_ONE
    #ifndef __NO_SEM_BACK
        bool const post_one = post_amount > 0;
        bool const post_two = post_amount > 1;
        if(post_amount > 2)
            __backbuffer.fetch_add(post_amount - 2, std::memory_order_acq_rel);
        auto const success = (!post_one || __semaphore_sem_post(__semaphore, 1)) && 
                             (!post_two || __semaphore_sem_post(__semaphore, 1));
        assert(success);
    #else
        for(; post_amount; --post_amount) {
            auto const success = __semaphore_sem_post(__semaphore, 1);
            assert(success);
        }
    #endif
#else
        auto const success = __semaphore_sem_post(__semaphore, post_amount);
        assert(success);
#endif
    }
    __semaphore_sem_t __semaphore;
#ifndef __NO_SEM_FRONT
    std::atomic<ptrdiff_t> __frontbuffer;
#endif
#ifndef __NO_SEM_BACK
    std::atomic<ptrdiff_t> __backbuffer;
#endif

public:
    static constexpr ptrdiff_t max() noexcept {
        return __semaphore_max;
    }
    
    __semaphore_base(ptrdiff_t count = 0) : __semaphore()
#ifndef __NO_SEM_FRONT
    , __frontbuffer(count << 32)
#endif
#ifndef __NO_SEM_BACK
    , __backbuffer(0)
#endif
    { 
        assert(count <= max());
        auto const success = 
#ifndef __NO_SEM_FRONT
            __semaphore_sem_init(__semaphore, 0);
#else
            __semaphore_sem_init(__semaphore, count);
#endif
        assert(success);
    }
    ~__semaphore_base() {
#ifndef __NO_SEM_FRONT
        assert(0 == (__frontbuffer.load(std::memory_order_relaxed) & ~0u));
#endif
        auto const success = __semaphore_sem_destroy(__semaphore);
        assert(success);
    }

    __semaphore_base(const __semaphore_base&) = delete;
    __semaphore_base& operator=(const __semaphore_base&) = delete;

    inline void release(ptrdiff_t update = 1) {
#ifndef __NO_SEM_FRONT
        // boldly assume the semaphore is taken but uncontended
        ptrdiff_t old = 0;
        // try to fast-release as long as it's uncontended
        while(0 == (old & ~0ul))
            if(__frontbuffer.compare_exchange_weak(old, old + (update << 32), std::memory_order_acq_rel))
                return;
#endif
        // slow-release it is
        __release_slow(update);
    }
    inline void acquire() {
        if(!__try_acquire_fast())
            __try_done(__semaphore_sem_wait(__semaphore));
    }
    inline bool try_acquire() noexcept {
        return try_acquire_for(std::chrono::nanoseconds(0));
    }
    template <class Clock, class Duration>
    bool try_acquire_until(std::chrono::time_point<Clock, Duration> const& abs_time) {
        auto const current = std::max(Clock::now(), abs_time);
        return try_acquire_for(std::chrono::duration_cast<std::chrono::nanoseconds>(abs_time - current));
    }
    template <class Rep, class Period>
    bool try_acquire_for(std::chrono::duration<Rep, Period> const& rel_time) {
        return __try_acquire_fast() ||
               __try_done(__semaphore_sem_wait_timed(__semaphore, rel_time));
    }
};

#endif //__NO_SEM

template<ptrdiff_t least_max_value>
using semaphore_base = 
#ifndef __NO_SEM
    typename std::conditional<least_max_value <= __semaphore_base::max(), 
                              __semaphore_base, 
                              __atomic_semaphore_base>::type
#else
    __atomic_semaphore_base
#endif
    ;

template<ptrdiff_t least_max_value = INT_MAX>
class counting_semaphore : public semaphore_base<least_max_value> {
    static_assert(least_max_value <= semaphore_base<least_max_value>::max(), "");

public:
    counting_semaphore(ptrdiff_t count = 0) : semaphore_base<least_max_value>(count) { }
    ~counting_semaphore() = default;

    counting_semaphore(const counting_semaphore&) = delete;
    counting_semaphore& operator=(const counting_semaphore&) = delete;
};

#ifdef __NO_SEM

class __binary_semaphore_base {

    __semaphore_no_inline inline bool __acquire_slow_timed(std::chrono::nanoseconds const& rel_time) {

        using __clock = std::conditional<std::chrono::high_resolution_clock::is_steady, 
                                         std::chrono::high_resolution_clock,
                                         std::chrono::steady_clock>::type;

        auto const start = __clock::now();
        while (!try_acquire()) {
            auto const elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(__clock::now() - start);
            auto const delta = rel_time - elapsed;
            if(delta <= std::chrono::nanoseconds(0))
                return false;
            auto const sleep = std::min((elapsed.count() >> 2) + 100, delta.count());
            std::this_thread::sleep_for(std::chrono::nanoseconds(sleep));
        }
        return true;
    }
    std::atomic<int> available;

public:
    static constexpr ptrdiff_t max() noexcept { return 1; }

    __binary_semaphore_base(ptrdiff_t available) : available(available) { }

    ~__binary_semaphore_base() = default;

    __binary_semaphore_base(__binary_semaphore_base const&) = delete;
    __binary_semaphore_base& operator=(__binary_semaphore_base const&) = delete;

    inline void release(ptrdiff_t update = 1) {
        available.store(1, std::memory_order_release);
        atomic_notify_one(&available);
    }
    inline void acquire() {
        while (!__builtin_expect(try_acquire(), 1))
            atomic_wait_explicit(&available, 0, std::memory_order_relaxed);
    }

    inline bool try_acquire() noexcept {
        return 1 == available.exchange(0, std::memory_order_acquire);
    }
    template <class Clock, class Duration>
    bool try_acquire_until(std::chrono::time_point<Clock, Duration> const& abs_time) {

        if (__builtin_expect(try_acquire(), 1))
            return true;
        else
            return __acquire_slow_timed(abs_time - Clock::now());
    }
    template <class Rep, class Period>
    bool try_acquire_for(std::chrono::duration<Rep, Period> const& rel_time) {

        if (__builtin_expect(try_acquire(), 1))
            return true;
        else
            return __acquire_slow_timed(rel_time);
    }
};

template<>
class counting_semaphore<1> : public __binary_semaphore_base {
public:
    counting_semaphore(ptrdiff_t count = 0) : __binary_semaphore_base(count) { }
    ~counting_semaphore() = default;

    counting_semaphore(const counting_semaphore&) = delete;
    counting_semaphore& operator=(const counting_semaphore&) = delete;
};

#endif // __NO_SEM

using binary_semaphore = counting_semaphore<1>;

}

/*

Copyright (c) 2019, NVIDIA Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include "atomic_wait"

namespace std {

class latch {
public:
    constexpr explicit latch(ptrdiff_t expected) : counter(expected) { }
    ~latch() = default;

    latch(const latch&) = delete;
    latch& operator=(const latch&) = delete;

    inline void count_down(ptrdiff_t update = 1) {
        assert(update > 0);
        auto const old = counter.fetch_sub(update, std::memory_order_release);
        assert(old >= update);
#ifndef __NO_WAIT
        if(old == update)
            atomic_notify_all(&counter);
#endif
    }
    inline bool try_wait() const noexcept {
        return counter.load(std::memory_order_acquire) == 0;
    }
    inline void wait() const {
        while(1) {
            auto const current = counter.load(std::memory_order_acquire);
            if(current == 0) 
                return;
#ifndef __NO_WAIT
            atomic_wait_explicit(&counter, current, std::memory_order_relaxed)
#endif
            ;
        }
    }
    inline void arrive_and_wait(ptrdiff_t update = 1) {
        count_down(update);
        wait();
    }

private:
    std::atomic<ptrdiff_t> counter;
};

}

#ifdef __TABLE

contended_t contention[256];

contended_t * __contention(volatile void const * p) {
    return contention + ((uintptr_t)p & 255);
}

#endif //__TABLE

thread_local size_t __barrier_favorite_hash =
    std::hash<std::thread::id>()(std::this_thread::get_id());
