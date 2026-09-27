/**
 * @file arch/arm/m4/tcb_offset.h
 * @brief Layout constants shared by the Cortex-M assembly switch paths.
 *
 * sai_thread_t.sp sits at this offset in the ARM32 TCB (sai_thread_t is a
 * public, statically-declarable struct -- see include/sai/kernel.h).  Any
 * change to the TCB field order must be reflected here; arch/arm/m4/port.c
 * carries a _Static_assert() that pins the value against the real C layout.
 */
#ifndef SAI_TCB_OFFSET_H
#define SAI_TCB_OFFSET_H

/* kobj(12) + prio_next(4) + wq_next(4) + wq_wait(4) + wq_key(4) +
 * wq_result(4) + wq_flags_out(4) + wq_flags(1, pad 3) + join_next(4) +
 * entry(4) + arg(4) + stack_base(4) + stack_size(4) = 60 */
#define SAI_TCB_SP_OFFSET 60

#endif /* SAI_TCB_OFFSET_H */
