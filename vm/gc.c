/*
 * Garbage collector
 * Copyright (C) 2009 Tomasz Grabiec
 * Copyright (C) 2009 Pekka Enberg
 *
 * This file is released under the GPL version 2 with the following
 * clarification and special exception:
 *
 *     Linking this library statically or dynamically with other modules is
 *     making a combined work based on this library. Thus, the terms and
 *     conditions of the GNU General Public License cover the whole
 *     combination.
 *
 *     As a special exception, the copyright holders of this library give you
 *     permission to link this library with independent modules to produce an
 *     executable, regardless of the license terms of these independent
 *     modules, and to copy and distribute the resulting executable under terms
 *     of your choice, provided that you also meet, for each linked independent
 *     module, the terms and conditions of the license of that module. An
 *     independent module is a module which is not derived from or based on
 *     this library. If you modify this library, you may extend this exception
 *     to your version of the library, but you are not obligated to do so. If
 *     you do not wish to do so, delete this exception statement from your
 *     version.
 *
 * Please refer to the file LICENSE for details.
 *
 * The stop-the-world algorith is based on the following paper:
 *
 *   "GC Points in a Threaded Environment", Agesen.
 */

#include "arch/registers.h"
#include "arch/memory.h"
#include "arch/signal.h"

#include "jit/compilation-unit.h"
#include "jit/cu-mapping.h"

#include "lib/guard-page.h"

#include "vm/stdlib.h"
#include "vm/thread.h"
#include "vm/method.h"
#include "vm/class.h"
#include "vm/trace.h"
#include "vm/die.h"
#include "vm/gc.h"
#include "lib/string.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

#include "../boehmgc/include/gc.h"

void *gc_safepoint_page;

static pthread_mutex_t	gc_reclaim_mutex	= PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	gc_reclaim_cond		= PTHREAD_COND_INITIALIZER;
static bool		gc_reclaim_in_progress;

pthread_spinlock_t gc_spinlock;

/* protected by gc_spinlock */
static int nr_in_safepoint;
static int nr_threads;

static __thread sig_atomic_t in_safepoint;

static pthread_t gc_thread_id;

unsigned long max_heap_size	= 128 * 1024 * 1024;	/* 128 MB */
bool verbose_gc;
bool gc_enabled;

static void hide_safepoint_guard_page(void)
{
	hide_guard_page(gc_safepoint_page);
}

static void unhide_safepoint_guard_page(void)
{
	unhide_guard_page(gc_safepoint_page);
}

static void suspend_self(void)
{
	sigset_t mask;
	int sig;

	if (sigemptyset(&mask) != 0)
		die("sigemptyset");

	if (sigaddset(&mask, SIGUSR2) != 0)
		die("sigaddset");

	if (sigwait(&mask, &sig) != 0)
		die("sigwait");

	if (sig != SIGUSR2)
		die("wrong signal");
}

static void suspend_thread(pthread_t thread_id)
{
	if (pthread_kill(thread_id, SIGUSR1) != 0)
		die("pthread_kill");
}

static void resume_thread(pthread_t thread_id)
{
	if (pthread_kill(thread_id, SIGUSR2) != 0)
		die("pthread_kill");
}

static bool do_exit_safepoint(void)
{
	bool ret = false;

	assert(in_safepoint);

	in_safepoint = false;

	if (pthread_spin_lock(&gc_spinlock) != 0)
		die("pthread_spin_lock");

	if (--nr_in_safepoint == 0)
		ret = true;

	if (pthread_spin_unlock(&gc_spinlock) != 0)
		die("pthread_spin_unlock");

	return ret;
}

static void exit_safepoint(void)
{
	if (do_exit_safepoint())
		resume_thread(gc_thread_id);
}

static void enter_safepoint(void)
{
	assert(!in_safepoint);

	in_safepoint = true;

	if (pthread_spin_lock(&gc_spinlock) != 0)
		die("pthread_spin_lock");

	if (++nr_in_safepoint == nr_threads)
		resume_thread(gc_thread_id);

	if (pthread_spin_unlock(&gc_spinlock) != 0)
		die("pthread_spin_unlock");
}

static void do_gc_reclaim(void)
{
	if (verbose_gc)
		fprintf(stderr, "[GC]\n");

	/* TODO: Do main GC work here. */
}

static void gc_scan_rootset(struct register_state *regs)
{
	struct compilation_unit *cu;

	cu = jit_lookup_cu(regs->ip);
	if (!cu) {
		warn("safepoint at unknown IP %" PRIx64, regs->ip);
		return;
	}

	if (verbose_gc)
		fprintf(stderr, "[GC at %s.%s]\n", cu->method->class->name, cu->method->name);

	/* TODO: get live references from this thread. */
}

void gc_safepoint(struct register_state *regs)
{
	gc_scan_rootset(regs);

	enter_safepoint();

	suspend_self();

	exit_safepoint();
}

void suspend_handler(int sig, siginfo_t *si, void *ctx)
{
	struct vm_thread *self = vm_thread_self();

	if (signal_from_native(ctx)) {
		struct register_state thread_register_state;
		ucontext_t *uc = ctx;

		/*
		 * Fresh threads might return NULL from vm_thread_self().
		 */
		if (self)
			self->thread_state = THREAD_STATE_CONSISTENT;

		save_signal_registers(&thread_register_state, uc->uc_mcontext.gregs);
		gc_safepoint(&thread_register_state);
	} else {
		self->thread_state = THREAD_STATE_INCONSISTENT;

		enter_safepoint();

		suspend_self();

		do_exit_safepoint();	/* don't wake up GC */
	}
}

void wakeup_handler(int sig, siginfo_t *si, void *ctx)
{
}

static void gc_resume_rest(void)
{
	struct vm_thread *thread;

	if (pthread_spin_lock(&gc_spinlock) != 0)
		die("pthread_spin_lock");

	assert(nr_in_safepoint == nr_threads);

	if (pthread_spin_unlock(&gc_spinlock) != 0)
		die("pthread_spin_unlock");

	vm_thread_for_each(thread) {
		assert(thread->posix_id != pthread_self());

		resume_thread(thread->posix_id);
	}

	/* Wait for all threads to leave a safepoint. */
	suspend_self();

	if (pthread_spin_lock(&gc_spinlock) != 0)
		die("pthread_spin_lock");

	assert(nr_in_safepoint == 0);

	if (pthread_spin_unlock(&gc_spinlock) != 0)
		die("pthread_spin_unlock");
}

static void gc_suspend_rest(void)
{
	unsigned long nr_restarted = 0;
	struct vm_thread *thread;

	if (pthread_spin_lock(&gc_spinlock) != 0)
		die("pthread_spin_lock");

	assert(nr_in_safepoint == 0);
	assert(nr_threads > 0);

	if (pthread_spin_unlock(&gc_spinlock) != 0)
		die("pthread_spin_unlock");

	vm_thread_for_each(thread) {
		assert(thread->posix_id != pthread_self());

		suspend_thread(thread->posix_id);
	}

	/* Wait for all threads to enter a safepoint.  */
	suspend_self();

	/*
	 * Restart inconsistent threads and let them run until they enter a
	 * safepoint through guard page polling.
	 */
	hide_safepoint_guard_page();

	vm_thread_for_each(thread) {
		assert(thread->posix_id != pthread_self());

		if (thread->thread_state == THREAD_STATE_INCONSISTENT) {
			resume_thread(thread->posix_id);
			nr_restarted++;
		}
	}

	/* Wait for restarted threads to enter a safepoint.  */
	if (nr_restarted)
		suspend_self();

	if (pthread_spin_lock(&gc_spinlock) != 0)
		die("pthread_spin_lock");

	assert(nr_in_safepoint == nr_threads);

	if (pthread_spin_unlock(&gc_spinlock) != 0)
		die("pthread_spin_unlock");

	unhide_safepoint_guard_page();
}

static void do_gc(void)
{
	vm_lock_thread_count();

	if (pthread_spin_lock(&gc_spinlock) != 0)
		die("pthread_spin_lock");

	nr_threads = vm_nr_threads();

	/* Don't deadlock during early boostrap. */
	if (nr_threads == 0) {
		if (pthread_spin_unlock(&gc_spinlock) != 0)
			die("pthread_spin_unlock");
		goto out;
	}

	if (pthread_spin_unlock(&gc_spinlock) != 0)
		die("pthread_spin_unlock");

	gc_suspend_rest();
	do_gc_reclaim();
	gc_resume_rest();
out:
	if (pthread_spin_lock(&gc_spinlock) != 0)
		die("pthread_spin_lock");

	nr_threads = -1;

	if (pthread_spin_unlock(&gc_spinlock) != 0)
		die("pthread_spin_unlock");

	vm_unlock_thread_count();

	if (pthread_mutex_lock(&gc_reclaim_mutex) != 0)
		die("pthread_mutex_lock");

	gc_reclaim_in_progress = false;
	pthread_cond_broadcast(&gc_reclaim_cond);

	if (pthread_mutex_unlock(&gc_reclaim_mutex) != 0)
		die("pthread_mutex_unlock");
}

static void *gc_thread(void *arg)
{
	struct sigaction sa;
	sigset_t sigset;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags	= SA_RESTART | SA_SIGINFO;

	sa.sa_sigaction	= wakeup_handler;
	sigaction(SIGUSR2, &sa, NULL);

	/*
	 * SIGUSR2 is used to resume threads. Make sure the signal is blocked
	 * by default to avoid races with sigwait().
	 */
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR2);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);

	for (;;) {
		suspend_self();
		do_gc();
	}
	return NULL;
}

/*
 * This wakes up the GC thread and suspends until garbage collection is done.
 */
static void gc_start(void)
{
	if (pthread_mutex_lock(&gc_reclaim_mutex) != 0)
		die("pthread_mutex_lock");

	if (gc_reclaim_in_progress)
		goto wait_for_reclaim;

	gc_reclaim_in_progress = true;

	if (pthread_mutex_unlock(&gc_reclaim_mutex) != 0)
		die("pthread_mutex_unlock");

	resume_thread(gc_thread_id);

	if (pthread_mutex_lock(&gc_reclaim_mutex) != 0)
		die("pthread_mutex_lock");

wait_for_reclaim:
	while (gc_reclaim_in_progress) {
		if (pthread_cond_wait(&gc_reclaim_cond, &gc_reclaim_mutex) != 0)
			die("pthread_cond_wait");
	}

	if (pthread_mutex_unlock(&gc_reclaim_mutex) != 0)
		die("pthread_mutex_unlock");
}

static void *gc_out_of_memory(size_t nr)
{
	if (verbose_gc)
		fprintf(stderr, "[GC]\n");

	GC_gcollect();

	return NULL;	/* is this ok? */
}

static void gc_ignore_warnings(char *msg, GC_word arg)
{
}

static void setup_boehm_gc(void)
{
	GC_set_warn_proc(gc_ignore_warnings);

	GC_oom_fn	= gc_out_of_memory;

	GC_quiet	= 1;

	GC_dont_gc	= !gc_enabled;

	GC_INIT();

        GC_set_max_heap_size(max_heap_size);
}

void gc_init(void)
{
	setup_boehm_gc();

	gc_safepoint_page = alloc_guard_page(false);
	if (!gc_safepoint_page)
		die("Couldn't allocate GC safepoint guard page");

	if (pthread_spin_init(&gc_spinlock, PTHREAD_PROCESS_SHARED) != 0)
		die("pthread_spin_init");

	if (pthread_create(&gc_thread_id, NULL, &gc_thread, NULL))
		die("Couldn't create GC thread");
}

void *gc_alloc(size_t size)
{
	return GC_MALLOC(size);
}

void *vm_alloc(size_t size)
{
	return GC_malloc_uncollectable(size);
}

void *vm_zalloc(size_t size)
{
	void *result;

	result = vm_alloc(size);
	memset(result, 0, size);
	return result;
}

void vm_free(void *ptr)
{
	GC_free(ptr);
}

void gc_register_finalizer(struct vm_object *object, finalizer_fn finalizer,
			   void *param)
{
	GC_register_finalizer(object, (GC_finalization_proc) finalizer,
			      param, NULL, NULL);
}
