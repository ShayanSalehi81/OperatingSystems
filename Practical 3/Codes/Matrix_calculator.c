#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

int first_matrix[5000][5000];
int second_matrix[5000][5000];
int result[5000][5000];
int M, K, N;
int number_of_threads;
int pipeline_fd[2];

typedef struct {
    int i;
    int j;
} Task;

void sendTask(const Task* task) {
    write(pipeline_fd[1], task, sizeof(Task));
}

bool receiveTask(Task* task) {
    if (read(pipeline_fd[0], task, sizeof(Task)) > 0) {
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
            sum += first_matrix[task.i][k] * second_matrix[k][task.j];
        }
        result[task.i][task.j] = sum;
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
    for (int i = 0; i < number_of_threads; ++i) {
        sendTask(&endTask);
    }
}

int main(int argc, char *argv[]) {
    FILE *file = fopen("input_matrix.txt", "r");

    fscanf(file, "%d", &number_of_threads);
    fscanf(file, "%d %d", &M, &K);
    printf("Number of threads used: %d\n", number_of_threads);
    printf("The dimension of the first matrix: %d %d\n", M, K);

    for (int i = 0; i < M; i++) {
        for (int j = 0; j < K; j++) {
            fscanf(file, "%d", &first_matrix[i][j]);
        }
    }

    fscanf(file, "%d %d", &K, &N);
    printf("The dimension of the second matrix: %d %d\n", K, N);

    for (int i = 0; i < K; i++) {
        for (int j = 0; j < N; j++) {
            fscanf(file, "%d", &second_matrix[i][j]);
        }
    }

    fclose(file);

    struct timespec starter, ender;
    clock_gettime(CLOCK_MONOTONIC, &starter);

    if (pipe(pipeline_fd) == -1) {
        perror("Pipe creation failed");
        exit(EXIT_FAILURE);
    }

    pthread_t threads[number_of_threads];
    for (int i = 0; i < number_of_threads; ++i) {
        if (pthread_create(&threads[i], NULL, threadFunction, NULL) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    distributeTasks();

    for (int i = 0; i < number_of_threads; ++i) {
        pthread_join(threads[i], NULL);
    }

    close(pipeline_fd[0]);
    close(pipeline_fd[1]);

    printf("The Result Matrix: \n");
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; j++) {
            printf("%d\t", result[i][j]);
        }
        printf("\n");
    }
    clock_gettime(CLOCK_MONOTONIC, &ender);

    double taken_time = (ender.tv_sec - starter.tv_sec) + (ender.tv_nsec - starter.tv_nsec) / 1e9;
    printf("Taken time for calculating the multplication of two given matrises: %.6f seconds\n", taken_time);

    return 0;
}