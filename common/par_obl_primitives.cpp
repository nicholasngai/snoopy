#include "par_obl_primitives.h"
#include <condition_variable>
#include <list>
#include <mutex>

namespace pos {
    std::list<thread_work*> work;
    std::condition_variable has_work;
    std::mutex work_lock;
    int done_ctr;
}

std::pair<int, int> get_cutoffs_for_thread(int thread_id, int total, int n_threads) {
    int chunks = std::floor(total / n_threads);
    int start = chunks*thread_id;
    int end = start+chunks;
    if (thread_id + 1 == n_threads) {
        end = total;
    }
    // printf("[t %d] bounds: [%d, %d)\n", thread_id, start, end);
    return std::make_pair(start, end);
}
