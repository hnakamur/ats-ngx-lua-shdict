#include "mps_queue.h"

/* this is just for compile check */
static void
mps_queue_compile_test(mps_slab_pool_t *pool)
{
    mps_queue_t *list_queue, *lq;
    
    list_queue = (mps_queue_t *) ((u_char *) pool + 4096);
    mps_queue_init(pool, list_queue);

    for (lq = mps_queue_head(pool, list_queue);
         lq != mps_queue_sentinel(pool, list_queue);
         lq = mps_queue_next(pool, lq))
    {
        mps_queue_remove(pool, lq);
        mps_queue_insert_head(pool, lq, lq);
        mps_queue_insert_tail(pool, lq, lq);
    }
}
