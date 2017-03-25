/* Copyright (C) 2005, 2008, 2009 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@redhat.com>.

   This file is part of the GNU OpenMP Library (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

/* This file contains routines for managing work-share iteration, both
   for loops and sections.  */

#include "libgomp.h"
#include <stdlib.h>


/* This function implements the STATIC scheduling method.  The caller should
   iterate *pstart <= x < *pend.  Return zero if there are more iterations
   to perform; nonzero if not.  Return less than 0 if this thread had
   received the absolutely last iteration.  */

int
gomp_iter_static_next (long *pstart, long *pend)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_team *team = thr->ts.team;
  struct gomp_work_share *ws = thr->ts.work_share;
  unsigned long nthreads = team ? team->nthreads : 1;

  if (thr->ts.static_trip == -1)
  {
    return -1;
  }


  /* Quick test for degenerate teams and orphaned constructs.  */
  if (nthreads == 1)
    {
      int retval;
      *pstart = ws->next;
      *pend = ws->end;
      thr->ts.static_trip = -1;
      retval = ws->next == ws->end;
      return retval;
    }

  /* We interpret chunk_size zero as "unspecified", which means that we
     should break up the iterations such that each thread makes only one
     trip through the outer loop.  */
  if (ws->chunk_size == 0)
    {
      unsigned long n, q, i;
      unsigned long s0, e0;
      long s, e;

      if (thr->ts.static_trip > 0)
	return 1;

      /* Compute the total number of iterations.  */
      s = ws->incr + (ws->incr > 0 ? -1 : 1);
      n = (ws->end - ws->next + s) / ws->incr;
      i = thr->ts.team_id;

      /* Compute the "zero-based" start and end points.  That is, as
         if the loop began at zero and incremented by one.  */
      q = n / nthreads;
      q += (q * nthreads != n);
      s0 = q * i;
      e0 = s0 + q;
      if (e0 > n)
        e0 = n;

      /* Notice when no iterations allocated for this thread.  */
      if (s0 >= e0)
	{
	  thr->ts.static_trip = 1;
	  return 1;
	}

      /* Transform these to the actual start and end numbers.  */
      s = (long)s0 * ws->incr + ws->next;
      e = (long)e0 * ws->incr + ws->next;

      *pstart = s;
      *pend = e;
      thr->ts.static_trip = (e0 == n ? -1 : 1);
      return 0;
    }
  else
    {
      unsigned long n, s0, e0, i, c;
      long s, e;

      /* Otherwise, each thread gets exactly chunk_size iterations
	 (if available) each time through the loop.  */

      s = ws->incr + (ws->incr > 0 ? -1 : 1);
      n = (ws->end - ws->next + s) / ws->incr;
      i = thr->ts.team_id;
      c = ws->chunk_size;

      /* Initial guess is a C sized chunk positioned nthreads iterations
	 in, offset by our thread number.  */
      s0 = (thr->ts.static_trip * nthreads + i) * c;
      e0 = s0 + c;

      /* Detect overflow.  */
      if (s0 >= n)
	return 1;
      if (e0 > n)
	e0 = n;

      /* Transform these to the actual start and end numbers.  */
      s = (long)s0 * ws->incr + ws->next;
      e = (long)e0 * ws->incr + ws->next;

      *pstart = s;
      *pend = e;

      if (e0 == n)
	thr->ts.static_trip = -1;
      else
	thr->ts.static_trip++;
      return 0;
    }
}


/* This function implements the DYNAMIC scheduling method.  Arguments are
   as for gomp_iter_static_next.  This function must be called with ws->lock
   held.  */

bool
gomp_iter_dynamic_next_locked (long *pstart, long *pend)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_work_share *ws = thr->ts.work_share;
  long start, end, chunk, left;

  start = ws->next;
  if (start == ws->end)
    return false;

  chunk = ws->chunk_size;
  left = ws->end - start;
  if (ws->incr < 0)
    {
      if (chunk < left)
	chunk = left;
    }
  else
    {
      if (chunk > left)
	chunk = left;
    }
  end = start + chunk;

  ws->next = end;
  *pstart = start;
  *pend = end;
  return true;
}


#ifdef HAVE_SYNC_BUILTINS
/* Similar, but doesn't require the lock held, and uses compare-and-swap
   instead.  Note that the only memory value that changes is ws->next.  */

bool
gomp_iter_dynamic_next (long *pstart, long *pend)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_work_share *ws = thr->ts.work_share;
  long start, end, nend, chunk, incr;

  end = ws->end;
  incr = ws->incr;
  chunk = ws->chunk_size;

  if (__builtin_expect (ws->mode, 1))
    {
      long tmp = __sync_fetch_and_add (&ws->next, chunk);
      if (incr > 0)
	{
	  if (tmp >= end)
	    goto label_return_false;
	  nend = tmp + chunk;
	  if (nend > end)
	    nend = end;
	  *pstart = tmp;
	  *pend = nend;
	  goto label_return_true;
	}
      else
	{
	  if (tmp <= end)
	    goto label_return_false;
	  nend = tmp + chunk;
	  if (nend < end)
	    nend = end;
	  *pstart = tmp;
	  *pend = nend;
	  goto label_return_true;
	}
    }

  start = ws->next;
  while (1)
    {
      long left = end - start;
      long tmp;

      if (start == end)
	goto label_return_false;

      if (incr < 0)
	{
	  if (chunk < left)
	    chunk = left;
	}
      else
	{
	  if (chunk > left)
	    chunk = left;
	}
      nend = start + chunk;

      tmp = __sync_val_compare_and_swap (&ws->next, start, nend);
      if (__builtin_expect (tmp == start, 1))
	break;

      start = tmp;
    }

  *pstart = start;
  *pend = nend;

label_return_true:
  return true;

label_return_false:
  return false;
}
#endif /* HAVE_SYNC_BUILTINS */


/* This function implements the GUIDED scheduling method.  Arguments are
   as for gomp_iter_static_next.  This function must be called with the
   work share lock held.  */

bool
gomp_iter_guided_next_locked (long *pstart, long *pend)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_work_share *ws = thr->ts.work_share;
  struct gomp_team *team = thr->ts.team;
  unsigned long nthreads = team ? team->nthreads : 1;
  unsigned long n, q;
  long start, end;

  if (ws->next == ws->end)
    return false;

  start = ws->next;
  n = (ws->end - start) / ws->incr;
  q = (n + nthreads - 1) / nthreads;

  if (q < ws->chunk_size)
    q = ws->chunk_size;
  if (q <= n)
    end = start + q * ws->incr;
  else
    end = ws->end;

  ws->next = end;
  *pstart = start;
  *pend = end;

  return true;
}

#ifdef HAVE_SYNC_BUILTINS
/* Similar, but doesn't require the lock held, and uses compare-and-swap
   instead.  Note that the only memory value that changes is ws->next.  */

#include <stdio.h>

#if defined(LIBGOMP_USE_ADAPTIVE)

#define gomp_loop_adaptive_is_finished(ws) (ws->nb_iterations_left == 0)

#if !defined(LIBGOMP_USE_PWS_STRICT)
static struct gomp_ws_adaptive_chunk *
gomp_loop_adaptive_random_pick_victim (struct gomp_thread* thr)
{
  long victim_id;
  long nthreads = thr->ts.team->nthreads;
  if (nthreads ==1)  /* means myself ! */
    return 0;
  do {
    victim_id = rand_r (&thr->seed) % nthreads;
  } while (victim_id == thr->ts.team_id);

  return &thr->ts.work_share->adaptive_chunks[victim_id];
}
#endif


#if defined(LIBGOMP_USE_NUMA)
static struct gomp_ws_adaptive_chunk *
gomp_loop_adaptive_numa_pick_victim (struct gomp_thread* thr) 
{
  struct gomp_thread_pool *pool = thr->thread_pool;

  long victim_id;
  long nthreads = pool->numa_info[thr->numaid].size;
  if (nthreads ==1)  /* means myself ! */
    return 0;
  do {
    victim_id = rand_r (&thr->seed) % nthreads;
  } while (victim_id == thr->index_numanode);

  /* convert local index in numanode to team index */
  victim_id = pool->numa_info[thr->numaid].team_ids[victim_id];
  return &thr->ts.work_share->adaptive_chunks[victim_id];
}
#endif


#define gomp_compiler_barrier() __sync_synchronize()

static inline bool
gomp_iter_adaptive_try_local_work (struct gomp_ws_adaptive_chunk *local_queue, 
				   long chunk_size, 
				   long *pstart, 
				   long *pend) 
{
  bool ret = false;

  long begin;

  begin = local_queue->begin;
  begin += chunk_size;
  local_queue->begin = begin;
  gomp_compiler_barrier();
  
  if (begin < local_queue->end) {
    *pstart = begin - chunk_size;
    *pend = begin;
    
    local_queue->nb_exec += chunk_size;
    
    ret = true;
  } else {
    long size;
    begin -= chunk_size;
    local_queue->begin = begin;
    kaapi_atomic_lock (&local_queue->lock);
    size = local_queue->end - begin;
    if (size > 0) {
      if (size > chunk_size)
	size = chunk_size;
      begin += size; 
      local_queue->begin = begin;
    }

    kaapi_atomic_unlock (&local_queue->lock);
    if (size > 0) {
      *pstart = begin - size;
      *pend = begin;
    
      local_queue->nb_exec += size;

      ret = true;
    }
  }

  return ret;
}

static inline bool
gomp_iter_adaptive_steal (struct gomp_thread *thr,
                          struct gomp_ws_adaptive_chunk *local_queue, 
			  long chunk_size,
			  long *pstart,
			  long *pend) 
{

  struct gomp_ws_adaptive_chunk* victim_queue;

#if defined(LIBGOMP_USE_NUMA)
  struct gomp_work_share* ws = thr->ts.work_share;
  struct gomp_thread_pool *pool = thr->thread_pool;
  long nthreads = pool->numa_info[thr->numaid].size;
  int i;
  for (i=0; i< (1+nthreads/2); ++i)
  {
    victim_queue = gomp_loop_adaptive_numa_pick_victim (thr);
    if ((victim_queue !=0) && (victim_queue->end > victim_queue->begin))
      goto do_steal;
  }
  if (gomp_loop_adaptive_is_finished (ws)) 
    return false;
#if defined(LIBGOMP_USE_PWS_STRICT)
  return false;
#else
  victim_queue = gomp_loop_adaptive_random_pick_victim (thr);
#endif

do_steal:
#else
  victim_queue = gomp_loop_adaptive_random_pick_victim (thr);
#endif
  if (victim_queue ==0) 
    return false;
  
  long end = victim_queue->end;
  long size = (victim_queue->end - victim_queue->begin) / 2;
  long local_size = chunk_size;
  
  /* return without locking... */
  if (size <= 0) 
    return false;

  kaapi_atomic_lock (&victim_queue->lock);
  end = victim_queue->end;
  //To not read again size... size = (victim_queue->end - victim_queue->begin) / 2;

  end -= size;
  victim_queue->end = end;
  gomp_compiler_barrier();
  if (end < victim_queue->begin) 
  {
    victim_queue->end = end + size;
    kaapi_atomic_unlock (&victim_queue->lock);
    return false;
  }
  
  *pstart = end;
  if (size <= local_size) {
    local_size = size;
  }
  *pend = end + local_size;
  
  kaapi_atomic_unlock (&victim_queue->lock);
  
  kaapi_atomic_lock (&local_queue->lock);
  
  local_queue->begin = *pend;
  local_queue->end = end + size;
  
  kaapi_atomic_unlock (&local_queue->lock);
  
  local_queue->nb_exec += (*pend - *pstart);
  
  return true;
}  



extern void
gomp_loop_adaptive_init_worker (
  struct gomp_work_share *ws,
  struct gomp_thread *thr,
  long start, long end, long chunk_size
);

bool
gomp_iter_adaptive_next (long *pstart, long *pend)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_work_share* ws = thr->ts.work_share;
  struct gomp_ws_adaptive_chunk *local_chunk = &ws->adaptive_chunks[thr->ts.team_id];

  if (!local_chunk->is_init)
    gomp_loop_adaptive_init_worker( ws, thr, ws->start_t0, ws->end,  ws->incr );

  long chunk_size = ws->chunk_size;
 
  /* Try local work first. */
  if (gomp_iter_adaptive_try_local_work (local_chunk, chunk_size, pstart, pend) == true)
  {
    //printf("Return: [%li,%li)\n", *pstart, *pend); 
    goto label_return_true;
  }

#if 0 // STEAL / NO STEAL for IWOMP 2013, EPCC

  /* Decr the iteration left counter, return false if == 0 */
  long retval = __sync_sub_and_fetch(&ws->nb_iterations_left, local_chunk->nb_exec);
  local_chunk->nb_exec = 0;
//printf("%i  nb_exec:%li  nb_left:%li\n", thr->ts.team_id, local_chunk->nb_exec, retval );
  if (retval ==0) 
    goto label_return_false;

  /* Steal work if idle. */
  while (!gomp_loop_adaptive_is_finished (ws)) 
  {
    int i;
    for (i=0; i<1; ++i)
    {
      if (gomp_iter_adaptive_steal (thr, local_chunk, chunk_size, pstart, pend) == true)
      {
#if defined(LIBGOMP_PROFILE_LOOP)
        ++PF(thr)->stealok;
#endif
        goto label_return_true;
      }
#if defined(LIBGOMP_PROFILE_LOOP)
      ++PF(thr)->stealbad;
#endif
    }
  } 

label_return_false:
#endif // to comment steal process

  return false;

label_return_true:
  return true;
}
#endif // #if defined(LIBGOMP_USE_ADAPTIVE)

bool
gomp_iter_guided_next (long *pstart, long *pend)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_work_share *ws = thr->ts.work_share;
  struct gomp_team *team = thr->ts.team;
  unsigned long nthreads = team ? team->nthreads : 1;
  long start, end, nend, incr;
  unsigned long chunk_size;

  start = ws->next;
  end = ws->end;
  incr = ws->incr;
  chunk_size = ws->chunk_size;

  while (1)
    {
      unsigned long n, q;
      long tmp;

      if (start == end)
      {
	return false;
      }

      n = (end - start) / incr;
      q = (n + nthreads - 1) / nthreads;

      if (q < chunk_size)
	q = chunk_size;
      if (__builtin_expect (q <= n, 1))
	nend = start + q * incr;
      else
	nend = end;

      tmp = __sync_val_compare_and_swap (&ws->next, start, nend);
      if (__builtin_expect (tmp == start, 1))
	break;

      start = tmp;
    }

  *pstart = start;
  *pend = nend;
  return true;
}

#endif /* HAVE_SYNC_BUILTINS */
