#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

int matrix_A[10000][10000];
int matrix_B[10000][10000];
int result_matrix[10000][10000];
int M, K, N;
int num_threads;
int pipe_fd[2];

typedef struct {
    int i;
    int j;
} Task;

void sendTask(const Task* task) {
    write(pipe_fd[1], task, sizeof(Task));
}

bool receiveTask(Task* task) {
    if (read(pipe_fd[0], task, sizeof(Task)) > 0) {
        return true;
    }
    return false;
}

void *threadFunction(void *arg) {
    Task task;
    while (receiveTask(&task)) {
        if (task.i == -1 && task.j == -1) { // Check for termination signal
            break;
        }

        int sum = 0;
        for (int k = 0; k < K; ++k) {
            sum += matrix_A[task.i][k] * matrix_B[k][task.j];
        }
        result_matrix[task.i][task.j] = sum;
    }
    return NULL;
}

void distributeTasks() {
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            Task task = {i, j};
            sendTask(&task);
        }
    }
    Task endTask = {-1, -1}; // Termination signal
    for (int i = 0; i < num_threads; ++i) {
        sendTask(&endTask);
    }
}

int main(int argc, char *argv[]) {
    // File reading logic to populate matrix_A, matrix_B, and set M, K, N, num_threads remains the same

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Create pipeline
    if (pipe(pipe_fd) == -1) {
        perror("Pipe creation failed");
        exit(EXIT_FAILURE);
    }

    pthread_t threads[num_threads];
    for (int i = 0; i < num_threads; ++i) {
        if (pthread_create(&threads[i], NULL, threadFunction, NULL) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    distributeTasks();

    for (int i = 0; i < num_threads; ++i) {
        pthread_join(threads[i], NULL);
    }

    close(pipe_fd[0]);
    close(pipe_fd[1]);

    // Print results and time elapsed, similar to the original code
    return 0;
}