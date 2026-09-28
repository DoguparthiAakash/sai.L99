/**
 * @file kernel/services/events.c
 * @brief Events service: named pub/sub channels over kernel event flags.
 *
 * Channels live in a static pool; publish is ISR-safe (thin wrapper over
 * sai_isr_event_set).  A dedicated daemon is not required, but "eventsd"
 * exists so the service table lists the facility uniformly.
 */
#include <sai/services.h>
#include <sai/log.h>
#include <string.h>

typedef struct {
    sai_event_t ev;
    char        name[SAI_EVENTS_NAME_MAX];
    bool        used;
} event_channel_t;

static event_channel_t s_channels[SAI_EVENTS_MAX_CHANNELS];

sai_status_t sai_events_open(const char *name, sai_event_t **out)
{
    if (name == NULL || out == NULL ||
        strlen(name) >= SAI_EVENTS_NAME_MAX) {
        return SAI_ERR_INVAL;
    }
    event_channel_t *free_slot = NULL;
    for (uint32_t i = 0; i < SAI_EVENTS_MAX_CHANNELS; i++) {
        event_channel_t *ch = &s_channels[i];
        if (ch->used && strcmp(ch->name, name) == 0) {
            *out = &ch->ev;
            return SAI_OK;              /* existing channel */
        }
        if (!ch->used && free_slot == NULL) {
            free_slot = ch;
        }
    }
    if (free_slot == NULL) {
        return SAI_ERR_FULL;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    strncpy(free_slot->name, name, sizeof(free_slot->name) - 1u);
    sai_status_t rc = sai_event_init(&free_slot->ev, free_slot->name);
    if (rc != SAI_OK) {
        return rc;
    }
    free_slot->used = true;
    *out = &free_slot->ev;
    return SAI_OK;
}

sai_status_t sai_events_publish(const char *name, uint32_t flags)
{
    if (name == NULL) {
        return SAI_ERR_INVAL;
    }
    for (uint32_t i = 0; i < SAI_EVENTS_MAX_CHANNELS; i++) {
        event_channel_t *ch = &s_channels[i];
        if (ch->used && strcmp(ch->name, name) == 0) {
            return sai_isr_event_set(&ch->ev, flags);
        }
    }
    return SAI_ERR_NOENT;
}

sai_status_t sai_events_wait(const char *name, uint32_t flags, uint32_t opts,
                             uint32_t *set_flags, int32_t timeout_ms)
{
    if (name == NULL) {
        return SAI_ERR_INVAL;
    }
    for (uint32_t i = 0; i < SAI_EVENTS_MAX_CHANNELS; i++) {
        event_channel_t *ch = &s_channels[i];
        if (ch->used && strcmp(ch->name, name) == 0) {
            return sai_event_wait(&ch->ev, flags, opts, set_flags, timeout_ms);
        }
    }
    return SAI_ERR_NOENT;
}

void sai_events_reset(void)
{
    for (uint32_t i = 0; i < SAI_EVENTS_MAX_CHANNELS; i++) {
        event_channel_t *ch = &s_channels[i];
        if (ch->used) {
            (void)sai_event_destroy(&ch->ev);
            ch->used = false;
        }
    }
}
