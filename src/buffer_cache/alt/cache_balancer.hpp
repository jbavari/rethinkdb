#ifndef BUFFER_CACHE_ALT_CACHE_BALANCER_HPP_
#define BUFFER_CACHE_ALT_CACHE_BALANCER_HPP_

#include <stdint.h>
#include <set>

#include "errors.hpp"

#include "threading.hpp"
#include "arch/timing.hpp"
#include "concurrency/coro_pool.hpp"
#include "concurrency/cross_thread_mutex.hpp"
#include "concurrency/queue/single_value_producer.hpp"
#include "containers/scoped.hpp"

namespace alt {
    class evicter_t;
}

// Base class so we can have a dummy implementation for tests
class cache_balancer_t {
public:
    cache_balancer_t() { }
    virtual ~cache_balancer_t() { }

    virtual uint64_t base_mem_per_store() const = 0;

protected:
    friend class alt::evicter_t;

    virtual void add_evicter(alt::evicter_t *evicter) = 0;
    virtual void remove_evicter(alt::evicter_t *evicter) = 0;

    DISABLE_COPYING(cache_balancer_t);
};

// Dummy balancer that does nothing but provide the initial size of a cache
class dummy_cache_balancer_t : public cache_balancer_t {
public:
    dummy_cache_balancer_t(uint64_t _base_mem_per_store) :
        base_mem_per_store_(_base_mem_per_store) { }
    ~dummy_cache_balancer_t() { }

    uint64_t base_mem_per_store() const {
        return base_mem_per_store_;
    }

private:
    void add_evicter(alt::evicter_t *) { }
    void remove_evicter(alt::evicter_t *) { }

    uint64_t base_mem_per_store_;

    DISABLE_COPYING(dummy_cache_balancer_t);
};

class alt_cache_balancer_dummy_value_t { };

class alt_cache_balancer_t :
    public cache_balancer_t,
    public home_thread_mixin_t,
    public coro_pool_callback_t<alt_cache_balancer_dummy_value_t>,
    public repeating_timer_callback_t
{
public:
    alt_cache_balancer_t(uint64_t _total_cache_size,
                         uint64_t interval_ms);
    ~alt_cache_balancer_t();

    uint64_t base_mem_per_store() const {
        return 0;
    }

private:
    friend class alt::evicter_t;

    void add_evicter(alt::evicter_t *evicter);
    void remove_evicter(alt::evicter_t *evicter);

    // Print a warning if we can't fit all the tables without using extra memory
    void warn_if_overcommitted(size_t num_shards);

    // Callback for repeating timer
    void on_ring();

    // Callback that handles rebalancing in a coro pool, so we don't block the
    // timer callback or have multiple rebalances happening at once
    void coro_pool_callback(alt_cache_balancer_dummy_value_t,
                            UNUSED signal_t *interruptor);

    // Used when calculating new cache sizes
    struct cache_data_t {
        cache_data_t(alt::evicter_t *_evicter);

        alt::evicter_t *evicter;
        uint64_t new_size;
        uint64_t old_size;
        uint64_t bytes_loaded;
    };

    // Helper function that rebalances all the shards on a given thread
    void apply_rebalance_to_thread(int index,
            scoped_array_t<std::vector<cache_data_t> > *new_sizes);

    struct thread_info_t {
        std::set<alt::evicter_t *> evicters;
        cross_thread_mutex_t mutex;
    };

    const uint64_t total_cache_size;
    repeating_timer_t rebalance_timer;

    // This contains the extant evicter pointers for each thread, and a mutex
    // to control access
    scoped_array_t<thread_info_t> thread_info;

    // Coroutine pool to make sure there is only one rebalance happening at a time
    // The single_value_producer_t makes sure we never build up a backlog
    struct dummy_value_t { };
    single_value_producer_t<alt_cache_balancer_dummy_value_t> pool_queue;
    coro_pool_t<alt_cache_balancer_dummy_value_t> rebalance_pool;

    DISABLE_COPYING(alt_cache_balancer_t);
};

#endif  // BUFFER_CACHE_ALT_CACHE_BALANCER_HPP_