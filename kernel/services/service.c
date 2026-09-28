/**
 * @file kernel/services/service.c
 * @brief Named service endpoints: a static registry of threads with an
 *        attached message queue, plus a cooperative stop protocol.
 *
 * A service is a thread that owns a message queue.  Anything can post
 * 4-byte messages to it (requests, pokes, stop tokens).  Service loops
 * call sai_service_recv() which blocks on the queue and translates the
 * stop token into SAI_ERR_STOPPED, so the loop can `return`.
 */
#include <sai/services.h>
#include <sai/console.h>
#include <sai/log.h>
#include <string.h>

#define SAI_SERVICE_MAX    4
#define SAI_SERVICE_QLEN   8
#define SAI_SERVICE_STACK  3072

typedef struct sai_service {
    sai_kobj_t      kobj;
    const char     *name;
    sai_service_fn_t main_fn;
    sai_msgq_t      inbox;
    uint32_t        inbox_store[SAI_SERVICE_QLEN];
    sai_thread_t    thread;
    void           *stack;
    uint8_t         prio;
    volatile uint8_t state;     /* 0 = idle, 1 = running, 2 = stopped */
} sai_service_t;

static sai_service_t s_services[SAI_SERVICE_MAX];
static uint32_t s_service_count;

static void service_trampoline(void *arg)
{
    sai_service_t *svc = (sai_service_t *)arg;
    svc->main_fn(svc);
    /* Loop returned: mark stopped and release the thread context. */
    svc->state = 2;
    sai_thread_exit();
}

sai_status_t sai_service_start(const char *name, sai_service_fn_t main_fn,
                               void *stack, size_t stack_size, uint8_t prio)
{
    if (name == NULL || main_fn == NULL) {
        return SAI_ERR_INVAL;
    }
    if (sai_service_find(name) != NULL) {
        return SAI_ERR_NOENT;      /* name taken */
    }
    if (s_service_count >= SAI_SERVICE_MAX) {
        return SAI_ERR_NOMEM;
    }
    sai_service_t *svc = &s_services[s_service_count++];
    memset(svc, 0, sizeof(*svc));
    svc->name    = name;
    svc->main_fn = main_fn;
    svc->prio    = prio;

    sai_status_t rc = sai_msgq_init(&svc->inbox, name, svc->inbox_store,
                                    sizeof(uint32_t), SAI_SERVICE_QLEN);
    if (rc != SAI_OK) {
        s_service_count--;
        return rc;
    }
    rc = sai_thread_create(&svc->thread, name, service_trampoline, svc,
                           prio, stack, stack_size, 0);
    if (rc != SAI_OK) {
        (void)sai_msgq_destroy(&svc->inbox);
        s_service_count--;
        return rc;
    }
    svc->state = 1;
    return sai_thread_start(&svc->thread);
}

sai_service_t *sai_service_find(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < s_service_count; i++) {
        if (strcmp(s_services[i].name, name) == 0) {
            return &s_services[i];
        }
    }
    return NULL;
}

sai_status_t sai_service_post(sai_service_t *svc, uint32_t msg)
{
    if (svc == NULL || svc->state != 1u) {
        return SAI_ERR_INVAL;
    }
    return sai_msgq_put(&svc->inbox, &msg, SAI_NO_WAIT);
}

sai_status_t sai_service_post_named(const char *name, uint32_t msg)
{
    return sai_service_post(sai_service_find(name), msg);
}

sai_status_t sai_service_recv(sai_service_t *svc, uint32_t *msg, int32_t timeout_ms)
{
    if (svc == NULL || msg == NULL) {
        return SAI_ERR_INVAL;
    }
    for (;;) {
        sai_status_t rc = sai_msgq_get(&svc->inbox, msg, timeout_ms);
        if (rc != SAI_OK) {
            return rc;
        }
        if (*msg == SAI_SERVICE_MSG_STOP) {
            return SAI_ERR_STATE;  /* stop was requested */
        }
        return SAI_OK;
    }
}

sai_status_t sai_service_stop(sai_service_t *svc)
{
    if (svc == NULL || svc->state != 1u) {
        return SAI_ERR_INVAL;
    }
    /* Post the stop token; the loop drains its inbox and exits. */
    uint32_t stop = SAI_SERVICE_MSG_STOP;
    (void)sai_msgq_put_overwrite(&svc->inbox, &stop);
    /* Wait (bounded) for the trampoline to flip the state. */
    for (int i = 0; i < 2000 && svc->state == 1u; i++) {
        sai_sleep(1);
    }
    if (svc->state != 2u) {
        return SAI_ERR_TIMEOUT;
    }
    (void)sai_thread_join(&svc->thread, 100);
    (void)sai_msgq_destroy(&svc->inbox);
    svc->state = 0;
    return SAI_OK;
}

bool sai_service_is_stopped(const sai_service_t *svc)
{
    return svc == NULL || svc->state != 1u;
}

const char *sai_service_name(const sai_service_t *svc)
{
    return svc == NULL ? NULL : svc->name;
}

sai_thread_t *sai_service_thread(sai_service_t *svc)
{
    return svc == NULL ? NULL : &svc->thread;
}
