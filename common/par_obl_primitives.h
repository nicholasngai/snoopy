#include "obl_primitives.h"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <list>
#include <mutex>

namespace pos {
    struct thread_work {
        void (*func)(void *arg);
        void *arg;
        bool is_done;
        std::condition_variable done;
        std::mutex done_lock;
    };

    extern std::list<thread_work*> work;
    extern std::condition_variable has_work;
    extern std::mutex work_lock;
    extern int done_ctr;
}

std::pair<int, int> get_cutoffs_for_thread(int thread_id, int total, int n_threads);

namespace detail {
inline void do_sort_thread_work() {
    int done_ctr;
    {
        std::unique_lock<std::mutex> guard(pos::work_lock);
        done_ctr = pos::done_ctr;
    }
    while (true) {
        pos::thread_work *work;
        {
            std::unique_lock<std::mutex> guard(pos::work_lock);
            while (pos::done_ctr == done_ctr && pos::work.empty()) {
                pos::has_work.wait(guard);
            }
            if (pos::done_ctr > done_ctr) {
                break;
            }
            work = pos::work.front();
            pos::work.pop_front();
        }
        work->func(work->arg);
        {
            std::unique_lock<std::mutex> guard(work->done_lock);
            work->is_done = true;
            work->done.notify_one();
        }
    }
}

template <typename T, typename Comparator>
static void swap_range(T *arr, size_t a, size_t b, size_t count,
        size_t real_length, Comparator cmp, bool crossover) {
    if (crossover) {
        for (size_t i = 0; i < count; i++) {
            if (b + count - 1 - i >= real_length) {
                continue;
            }
            bool cond = cmp(arr[a + i], arr[b + count - 1 - i]);
            o_memswap(&arr[a + i], &arr[b + count - 1 - i], sizeof(*arr), cond);
        }
    } else {
        for (size_t i = 0; i < count; i++) {
            if (b + i >= real_length) {
                continue;
            }
            bool cond = cmp(arr[a + i], arr[b + i]);
            o_memswap(&arr[a + i], &arr[b + i], sizeof(*arr), cond);
        }
    }
}

template <typename T, typename Comparator>
struct merge_args {
    T *arr;
    size_t start;
    size_t real_length;
    size_t length;
    Comparator cmp;
    bool crossover;
    size_t num_threads;
};
template <typename T, typename Comparator>
static void merge(void *args_) {
    merge_args<T, Comparator> *args = (merge_args<T, Comparator> *) args_;
    T *arr = args->arr;
    size_t start = args->start;
    size_t real_length = args->real_length;
    size_t length = args->length;
    Comparator cmp = args->cmp;
    bool crossover = args->crossover;
    size_t num_threads = args->num_threads;

    switch (length) {
        case 0:
        case 1:
            /* Do nothing. */
            break;
        case 2: {
            swap_range(arr, start, start + 1, 1, real_length, cmp, false);
            break;
        }
        default: {
            /* If the length is odd, bubble sort an element to the end of the
             * array and leave it there. */
            swap_range(arr, start, start + length / 2, length / 2, real_length,
                    cmp, crossover);

            /* Recursively merge. */
            merge_args<T, Comparator> left_args = {
                .arr = arr,
                .start = start,
                .real_length = length / 2,
                .length = length / 2,
                .cmp = cmp,
                .crossover = false,
            };
            merge_args<T, Comparator> right_args = {
                .arr = arr,
                .start = start + length / 2,
                .real_length = real_length - length / 2,
                .length = length / 2,
                .cmp = cmp,
                .crossover = false,
            };
            if (num_threads > 1) {
                /* Merge both with separate threads. */
                size_t right_threads = num_threads / 2;
                left_args.num_threads = num_threads - right_threads;
                right_args.num_threads = right_threads;
                struct pos::thread_work right_work = {
                    .func = merge<T, Comparator>,
                    .arg = &right_args,
                };
                {
                    std::unique_lock<std::mutex> guard(pos::work_lock);
                    pos::work.push_back(&right_work);
                    pos::has_work.notify_all();
                }
                merge<T, Comparator>(&left_args);
                {
                    std::unique_lock<std::mutex> guard(right_work.done_lock);
                    if (!right_work.is_done) {
                        right_work.done.wait(guard);
                    }
                }
            } else {
                /* Merge both in our own thread. */
                left_args.num_threads = 1;
                right_args.num_threads = 1;
                merge<T, Comparator>(&left_args);
                merge<T, Comparator>(&right_args);
            }
            break;
         }
    }
}

template <typename T, typename Comparator>
struct sort_args {
    T *arr;
    size_t start;
    size_t real_length;
    size_t length;
    Comparator cmp;
    size_t num_threads;
};
template <typename T, typename Comparator>
static void sort(void *args_) {
    sort_args<T, Comparator> *args = (sort_args<T, Comparator> *) args_;
    T *arr = args->arr;
    size_t start = args->start;
    size_t real_length = args->real_length;
    size_t length = args->length;
    Comparator cmp = args->cmp;
    size_t num_threads = args->num_threads;

    switch (length) {
        case 0:
        case 1:
            /* Do nothing. */
            break;
        case 2: {
            swap_range(arr, start, start + 1, 1, real_length, cmp, false);
            break;
        }
        default: {
            /* Recursively sort left half forwards and right half in reverse to
             * create a bitonic sequence. */
            sort_args<T, Comparator> left_args = {
                .arr = arr,
                .start = start,
                .real_length = length / 2,
                .length = length / 2,
                .cmp = cmp,
            };
            sort_args<T, Comparator> right_args = {
                .arr = arr,
                .start = start + length / 2,
                .real_length = real_length - length / 2,
                .length = length / 2,
                .cmp = cmp,
            };
            if (num_threads > 1) {
                /* Sort both with separate threads. */
                size_t right_threads = num_threads / 2;
                left_args.num_threads = num_threads - right_threads;
                right_args.num_threads = right_threads;
                struct pos::thread_work right_work = {
                    .func = sort<T, Comparator>,
                    .arg = &right_args,
                };
                {
                    std::unique_lock<std::mutex> guard(pos::work_lock);
                    pos::work.push_back(&right_work);
                    pos::has_work.notify_all();
                }
                sort<T, Comparator>(&left_args);
                {
                    std::unique_lock<std::mutex> guard(right_work.done_lock);
                    if (!right_work.is_done) {
                        right_work.done.wait(guard);
                    }
                }
            } else {
                /* Sort both in our own thread. */
                left_args.num_threads = 1;
                right_args.num_threads = 1;
                sort<T, Comparator>(&left_args);
                sort<T, Comparator>(&right_args);
            }

            /* Bitonic merge. */
            merge_args<T, Comparator> merge_args = {
                .arr = arr,
                .start = start,
                .real_length = real_length,
                .length = length,
                .cmp = cmp,
                .crossover = true,
                .num_threads = num_threads,
            };
            merge<T, Comparator>(&merge_args);
            break;
        }
    }
}
}

template <typename Iter, typename Comparator>
inline void ObliviousSortParallel(Iter begin, Iter end, Comparator cmp, int num_threads, int thread_id) {
    if (thread_id == 0) {
        using value_type = typename std::remove_reference<decltype(*begin)>::type;
        value_type *array = &(*begin);
        detail::sort_args<value_type, Comparator> args = {
            .arr = array,
            .start = 0,
            .real_length = (size_t) (end - begin),
            .length = detail::greatest_power_of_two_less_than(end - begin) * 2,
            .cmp = cmp,
            .num_threads = (size_t) num_threads,
        };
        detail::sort<value_type, Comparator>(&args);
        {
            std::unique_lock<std::mutex> guard(pos::work_lock);
            pos::done_ctr++;
            pos::has_work.notify_all();
        }
    } else {
        detail::do_sort_thread_work();
    }
}

template <typename Iter, typename Comparator>
inline void ObliviousSortParallelNonAdaptive(Iter begin, Iter end, Comparator cmp, int num_threads, int thread_id) {
    ObliviousSortParallel(begin, end, cmp, num_threads, thread_id);
}
