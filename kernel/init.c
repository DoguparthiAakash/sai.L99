/**
 * @file kernel/init.c
 * @brief Kernel object registry (diagnostics) — tracks all live kobjs so the
 *        panic dump / shell can enumerate them.
 */
#include "internal.h"

#define SAI_KOBJ_REGISTRY_MAX 64

static sai_kobj_t *s_registry[SAI_KOBJ_REGISTRY_MAX];
static uint32_t s_registry_count;

void _sai_kobj_register_full(sai_kobj_t *k, uint16_t type, const char *name)
{
    uint32_t key = port_lock();
    k->type = type;
    k->name = name;
    if (s_registry_count < SAI_KOBJ_REGISTRY_MAX) {
        s_registry[s_registry_count++] = k;
    }
    port_unlock(key);
}

void _sai_kobj_unregister_full(sai_kobj_t *k)
{
    uint32_t key = port_lock();
    for (uint32_t i = 0; i < s_registry_count; i++) {
        if (s_registry[i] == k) {
            s_registry[i] = s_registry[--s_registry_count];
            break;
        }
    }
    k->type = SAI_KOBJ_NONE;
    port_unlock(key);
}

uint32_t _sai_kobj_count(uint16_t type)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < s_registry_count; i++) {
        if (s_registry[i]->type == type) {
            n++;
        }
    }
    return n;
}
