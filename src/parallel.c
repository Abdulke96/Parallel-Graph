// SPDX-License-Identifier: BSD-3-Clause


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "os_graph.h"
#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"


#define NUM_THREADS 4

static int sum;
static os_graph_t *graph;
static os_threadpool_t *tp;
static pthread_mutex_t graph_mutex;
static pthread_mutex_t sum_mutex;
static pthread_mutex_t visited_mutex;

typedef struct graph_task_arg {
	unsigned int idx;
} graph_task_arg;

void process_node(unsigned int idx);

void process_node_task(void *arg)
{
	graph_task_arg *task_arg = (graph_task_arg *)arg;

	process_node(task_arg->idx);
}

void process_node(unsigned int idx)
{
	os_node_t *node;

	pthread_mutex_lock(&graph_mutex);
	node = graph->nodes[idx];
	pthread_mutex_lock(&sum_mutex);
	if (graph->visited[idx] != DONE)
		sum += node->info;
	pthread_mutex_unlock(&sum_mutex);
	graph->visited[idx] = DONE;
	pthread_mutex_unlock(&graph_mutex);
	for (unsigned int i = 0; i < node->num_neighbours; i++) {
		unsigned int neighbour_idx = node->neighbours[i];

		pthread_mutex_lock(&graph_mutex);
		if (graph->visited[neighbour_idx] == NOT_VISITED) {
			graph->visited[neighbour_idx] = PROCESSING;
			pthread_mutex_unlock(&graph_mutex);
			graph_task_arg *task_arg = malloc(sizeof(graph_task_arg));

			task_arg->idx = neighbour_idx;
			os_task_t *task = create_task(&process_node_task, task_arg, &free);

			enqueue_task(tp, task);
			process_node(task_arg->idx);
		} else {
			pthread_mutex_unlock(&graph_mutex);
		}
	}


pthread_mutex_lock(&tp->queue_mutex);
tp->tasks_remaining = 0;
pthread_mutex_unlock(&tp->queue_mutex);
}
int main(int argc, char *argv[])
{
	FILE *input_file;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s input_file\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	input_file = fopen(argv[1], "r");
	DIE(input_file == NULL, "fopen");

	graph = create_graph_from_file(input_file);
	DIE(graph == NULL, "create_graph_from_file");

	fclose(input_file);

	pthread_mutex_init(&graph_mutex, NULL);
	pthread_mutex_init(&sum_mutex, NULL);
	pthread_mutex_init(&visited_mutex, NULL);

	tp = create_threadpool(NUM_THREADS);
	if (tp == NULL) {
		fprintf(stderr, "Failed to create thread pool\n");
		exit(EXIT_FAILURE);
	}
	process_node(0);

	wait_for_completion(tp);
	destroy_threadpool(tp);

	printf("%d", sum);
	pthread_mutex_destroy(&graph_mutex);
	pthread_mutex_destroy(&sum_mutex);
	pthread_mutex_destroy(&visited_mutex);
	return 0;
}
