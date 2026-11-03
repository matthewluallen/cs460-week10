"""Apply the bounded experimental deadline fence to the supported QEMU source."""
from pathlib import Path
import sys


def apply(root):
    root = Path(root)
    edits = []
    # Normalize only the exact predecessor experimental patch, never arbitrary code.
    rr = root/'accel/tcg/tcg-accel-ops-rr.c'
    original = rr.read_text()
    normalized = original.replace(LEGACY_WAIT+'\n', '')
    normalized = normalized.replace('\n        rr_wait_deadline_fence();\n', '\n')
    normalized = normalized.replace('\n            rr_wait_deadline_fence();\n', '\n')
    if normalized != original:
        edits.append((rr, normalized))
    def edit(name, before, after):
        path = root/name
        text = next((v for p,v in reversed(edits) if p == path), path.read_text())
        if after in text:
            return
        if text.count(before) != 1:
            raise ValueError('Unsupported or modified QEMU anchor: '+name)
        edits.append((path, text.replace(before, after, 1)))

    edit('include/sysemu/cpu-timers.h', '/* return the virtual CPU time',
         '/* Experimental whole-VM deadline stop; caller holds BQL. */\nbool icount_deadline_fenced(void);\nvoid icount_set_deadline_fence(bool pending);\n\n/* return the virtual CPU time')
    edit('accel/tcg/icount-common.c', 'static bool icount_sleep = true;',
         'static bool icount_sleep = true;\n'+FENCE)
    for name in ('icount_warp_rt', 'icount_start_warp_timer', 'icount_account_warp_timer'):
        prefix = ('static void ' if name == 'icount_warp_rt' else 'void ')+name+'(void)\n{'
        edit('accel/tcg/icount-common.c', prefix, prefix+'\n    if (icount_deadline_fenced()) return;')
    edit('accel/tcg/tcg-accel-ops-rr.c', 'static void rr_wait_io_event(void)',
         WAIT+'\nstatic void rr_wait_io_event(void)')
    edit('accel/tcg/tcg-accel-ops-rr.c', '        replay_mutex_unlock();\n\n        if (!cpu)',
         '        replay_mutex_unlock();\n\n        if (rr_wait_deadline_fence()) continue;\n        if (!cpu)')
    edit('accel/tcg/tcg-accel-ops-rr.c', '            /* Store rr_current_cpu before evaluating cpu_can_run().  */',
         '            if (rr_wait_deadline_fence()) break;\n            /* Store rr_current_cpu before evaluating cpu_can_run().  */')
    edit('accel/tcg/tcg-accel-ops-rr.c',
         '                } else if (r == EXCP_ATOMIC) {\n                    bql_unlock();',
         '                } else if (r == EXCP_ATOMIC) {\n                    if (rr_wait_deadline_fence() || !cpu_can_run(cpu)) break;\n                    bql_unlock();')
    # All anchors validated before any write; retain only each final file value.
    for path, text in dict(edits).items():
        path.write_text(text)


FENCE = r"""
static bool deadline_stop_fenced;

bool icount_deadline_fenced(void)
{
    return qatomic_read(&deadline_stop_fenced);
}

void icount_set_deadline_fence(bool pending)
{
    CPUState *cpu;
    g_assert(bql_locked());
    qatomic_set(&deadline_stop_fenced, pending);
    if (pending) {
        /* Discard uncommitted wall-time warp; never rewind virtual time. */
        if (timers_state.icount_warp_timer) timer_del(timers_state.icount_warp_timer);
        seqlock_write_lock(&timers_state.vm_clock_seqlock,
                          &timers_state.vm_clock_lock);
        timers_state.vm_clock_warp_start = -1;
        seqlock_write_unlock(&timers_state.vm_clock_seqlock,
                            &timers_state.vm_clock_lock);
    }
    /* Same BQL-protected predicate/condition as the RR fence wait. */
    CPU_FOREACH(cpu) {
        qemu_cond_broadcast(cpu->halt_cond);
    }
}
"""

LEGACY_WAIT = r"""
/* Never hold replay_mutex while waiting: the main thread must complete stop.
 * Service every CPU's stop flag on each wake, or pause_all_vcpus deadlocks. */
static void rr_wait_deadline_fence(void)
{
    CPUState *wait_cpu;
    CPUState *saved_cpu = current_cpu;
    while (icount_deadline_fenced()) {
        CPU_FOREACH(wait_cpu) {
            current_cpu = wait_cpu;
            qemu_wait_io_event_common(wait_cpu);
        }
        if (icount_deadline_fenced()) {
            qemu_cond_wait_bql(first_cpu->halt_cond);
        }
    }
    current_cpu = saved_cpu;
}
"""

WAIT = r"""
/* Never hold replay_mutex while waiting: the main thread must complete stop.
 * Service every CPU's stop flag on each wake, or pause_all_vcpus deadlocks. */
static bool rr_wait_deadline_fence(void)
{
    CPUState *wait_cpu;
    CPUState *saved_cpu = current_cpu;
    bool waited = false;
    while (icount_deadline_fenced()) {
        waited = true;
        CPU_FOREACH(wait_cpu) {
            current_cpu = wait_cpu;
            qemu_wait_io_event_common(wait_cpu);
        }
        if (icount_deadline_fenced()) {
            qemu_cond_wait_bql(first_cpu->halt_cond);
        }
    }
    current_cpu = saved_cpu;
    return waited;
}
"""

if __name__ == '__main__':
    apply(sys.argv[1])
