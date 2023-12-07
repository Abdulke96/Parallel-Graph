// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

/* Create a task that would be executed by a thread. */
static int queue_is_empty(os_threadpool_t *tp);
os_task_t *create_task(void (*action)(void *), void *arg, void (*destroy_arg)(void *))
{
	os_task_t *t;

	t = malloc(sizeof(*t));
	DIE(t == NULL, "malloc");
	t->action = action;		// the function
	t->argument = arg;		// arguments for the function
	t->destroy_arg = destroy_arg;	// destroy argument function
	list_init(&t->list);
	return t;
}

/* Destroy task. */
void destroy_task(os_task_t *t)
{
	if (t->destroy_arg != NULL)
		t->destroy_arg(t->argument);
	free(t);
}

/* Put a new task to threadpool task queue. */
void enqueue_task(os_threadpool_t *tp, os_task_t *t)
{
	assert(tp != NULL);
	assert(t != NULL);
	pthread_mutex_lock(&tp->queue_mutex);
	list_add_tail(&t->list, &tp->head);
	tp->tasks_remaining++;
	pthread_cond_signal(&tp->queue_cond);
	pthread_mutex_unlock(&tp->queue_mutex);
}


static int queue_is_empty(os_threadpool_t *tp)
{
	return list_empty(&tp->head);
}

os_task_t *dequeue_task(os_threadpool_t *tp)
{
	os_task_t *t = NULL;

	pthread_mutex_lock(&tp->queue_mutex);
while (queue_is_empty(tp) && tp->tasks_remaining > 0)
	pthread_cond_wait(&tp->queue_cond, &tp->queue_mutex);

if (!queue_is_empty(tp)) {
	t = list_entry(tp->head.next, os_task_t, list);
	list_del(&t->list);
	tp->tasks_remaining--;
	}

	pthread_mutex_unlock(&tp->queue_mutex);

	return t;
}

/* Loop function for threads */
static void *thread_loop_function(void *arg)
{
	os_threadpool_t *tp = (os_threadpool_t *) arg;

	while (1) {
		os_task_t *t;

		t = dequeue_task(tp);
		if (t == NULL)
			break;
		t->action(t->argument);
		destroy_task(t);
	}
	return NULL;
}

/* Wait completion of all threads. This is to be called by the main thread. */
void wait_for_completion(os_threadpool_t *tp)
{
	pthread_mutex_lock(&tp->queue_mutex);

	while (tp->tasks_remaining > 0)
		pthread_cond_wait(&tp->queue_cond, &tp->queue_mutex);
	pthread_mutex_unlock(&tp->queue_mutex);
}
os_threadpool_t *create_threadpool(unsigned int num_threads)
{
	os_threadpool_t *tp = malloc(sizeof(*tp));

	DIE(tp == NULL, "malloc");

	list_init(&tp->head);
	pthread_mutex_init(&tp->queue_mutex, NULL);
	pthread_cond_init(&tp->queue_cond, NULL);
	tp->tasks_remaining = 0;
	tp->shutdown = 0; // TODO: Initialize shutdown flag.

	tp->num_threads = num_threads;
	tp->threads = malloc(num_threads * sizeof(*tp->threads));
	DIE(tp->threads == NULL, "malloc");
	for (unsigned int i = 0; i < num_threads; ++i) {
		int rc = pthread_create(&tp->threads[i], NULL, &thread_loop_function, (void *)tp);

		DIE(rc < 0, "pthread_create");
	}

	return tp;
}

void destroy_threadpool(os_threadpool_t *tp)
{
	os_list_node_t *n, *p;

	pthread_mutex_lock(&tp->queue_mutex);
	tp->shutdown = 1;
	pthread_cond_broadcast(&tp->queue_cond);
	pthread_mutex_unlock(&tp->queue_mutex);

	for (unsigned int i = 0; i < tp->num_threads; i++)
		pthread_join(tp->threads[i], NULL);
	pthread_mutex_destroy(&tp->queue_mutex);
	pthread_cond_destroy(&tp->queue_cond);

	list_for_each_safe(n, p, &tp->head) {
		list_del(n);
		destroy_task(list_entry(n, os_task_t, list));
	}
	if (tp->threads != NULL)
		free(tp->threads);
	if (tp != NULL)
		free(tp);
}
