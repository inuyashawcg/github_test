#define TAILQ_HEAD(name, type)  \
struct name {   \
    struct type *tqh_first; \
    struct type **tqh_last; \
}   \

#define TAILQ_ENTRY(type) \
struct {    \
    struct type *tqe_next;  \
    struct type **tqe_prev; \
}   \

struct QUEUE_ITEM {
    int value;
    TAILQ_ENTRY(QUEUE_ITEM) entries;
};

TAILQ_HEAD(headname, QUEUE_ITEM) queue_head;