/**
 * @file kernel/spinlock.c
 * @brief Interrupt-safe spinlocks (port backend: BASEPRI / ticket / SR).
 */
#include "internal.h"

void sai_spin_init(sai_spinlock_t *l)
{
    l->kobj.type = SAI_KOBJ_SPINLOCK;
    l->kobj.name = "spin";
    l->slock = 0u;
    l->saved = 0u;
    l->owner = NULL;
}

void sai_spin_lock(sai_spinlock_t *l)
{
    uint32_t key = port_spin_lock(&l->slock);
    l->saved = key;
    l->owner = _sai_current;
}

void sai_spin_unlock(sai_spinlock_t *l)
{
    (void)port_spin_unlock(&l->slock, l->saved);
    l->owner = NULL;
}

bool sai_spin_is_locked(const sai_spinlock_t *l)
{
    return l->slock != 0u;
}
