#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>

#define MAX_PATH_SIZE 1024

typedef struct
{
    int source_fd;
    int destination_fd;
    char source_file[MAX_PATH_SIZE];
    char destination_file[MAX_PATH_SIZE];
} file_task_t;

typedef struct
{
    char source_folder[MAX_PATH_SIZE];
    char destination_folder[MAX_PATH_SIZE];
} directory_paths_t;

//----------------------------------------------------------------//

// Buffer management and synchronization
file_task_t *task_buffer;
int number_of_workers;
int buffer_size;
int buffer_front = 0, buffer_rear = 0, buffer_count = 0;
volatile sig_atomic_t done_flag = 0;

pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t buffer_not_full = PTHREAD_COND_INITIALIZER;

// Statistics management and synchronization
int num_regular_files = 0;
int num_fifo_files = 0;
int num_directories = 0;
int num_other_files = 0;
off_t total_bytes_copied = 0;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

// Stdout synchronization
pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;

// Calculate number of finished threads
int number_finish_threads = 0;
pthread_mutex_t finished_threads_mutex = PTHREAD_MUTEX_INITIALIZER;

// Barrier for synchronizing worker threads
pthread_barrier_t barrier;

//----------------------------------------------------------------//

void *worker_thread(void *arg);
void *manager_thread(void *arg);
void traverse_directory(const char *source_folder, const char *destination_folder);
void handle_directory(const char *source_folder, const char *destination_folder, const struct dirent *entry);
void handle_file(const char *source_folder, const char *destination_folder, const struct dirent *entry);
void copy_file(file_task_t *task);
void update_statistics(const struct dirent *entry);
void setup_signal_handler();
void handle_signal(int signal);
void destroy_mutexes_and_cond_vars();
void close_fd(int fd);

//----------------------------------------------------------------//

int main(int argc, char *argv[])
{
    //-------- Check arguments --------//
    if (argc != 5)
    {
        printf("Usage: %s <buffer_size> <number_of_workers> <source_folder> <destination_folder>\n", argv[0]);
        return 1;
    }

    buffer_size = atoi(argv[1]);
    if (buffer_size <= 0)
    {
        printf("Invalid buffer size. Please enter a positive integer.\n");
        return 1;
    }

    number_of_workers = atoi(argv[2]);
    if (number_of_workers <= 0)
    {
        printf("Invalid number of workers. Please enter a positive integer.\n");
        return 1;
    }

    directory_paths_t paths;
    strncpy(paths.source_folder, argv[3], MAX_PATH_SIZE - 1);
    paths.source_folder[MAX_PATH_SIZE - 1] = '\0';
    strncpy(paths.destination_folder, argv[4], MAX_PATH_SIZE - 1);
    paths.destination_folder[MAX_PATH_SIZE - 1] = '\0';

    //-------- Create task buffer in given size --------//
    task_buffer = malloc(buffer_size * sizeof(file_task_t));
    if (task_buffer == NULL)
    {
        perror("Failed to allocate memory for task buffer");
        return 1;
    }

    for (int i = 0; i < buffer_size; i++)
    {
        task_buffer[i].source_fd = -1;
        task_buffer[i].destination_fd = -1;
    }

    setup_signal_handler();

    //-------- Initialize barrier --------//
    if (pthread_barrier_init(&barrier, NULL, number_of_workers) != 0)
    {
        perror("Failed to initialize barrier");
        free(task_buffer);
        return 1;
    }

    //-------- Create manager and worker threads --------//
    pthread_t manager;
    if (pthread_create(&manager, NULL, manager_thread, &paths) != 0)
    {
        perror("Failed to create manager thread");
        free(task_buffer);
        pthread_barrier_destroy(&barrier);
        return 1;
    }

    pthread_t *workers = malloc(number_of_workers * sizeof(pthread_t));
    if (workers == NULL)
    {
        perror("Failed to allocate memory for worker threads");
        free(task_buffer);
        pthread_barrier_destroy(&barrier);
        return 1;
    }

    for (int i = 0; i < number_of_workers; i++)
    {
        if (pthread_create(&workers[i], NULL, worker_thread, NULL) != 0)
        {
            perror("Failed to create worker thread");
            for (int j = 0; j < i; j++)
            {
                pthread_cancel(workers[j]);
                pthread_join(workers[j], NULL);
            }
            free(task_buffer);
            free(workers);
            pthread_barrier_destroy(&barrier);
            return 1;
        }
    }

    //-------- Measure statistics of copying files --------//
    struct timeval start, end;
    gettimeofday(&start, NULL);

    pthread_join(manager, NULL);
    for (int i = 0; i < number_of_workers; i++)
        pthread_join(workers[i], NULL);

    gettimeofday(&end, NULL);
    long seconds = end.tv_sec - start.tv_sec;
    long micros = ((seconds * 1000000) + end.tv_usec) - (start.tv_usec);
    long minutes = seconds / 60;
    seconds = seconds % 60;
    long milliseconds = micros / 1000;

    pthread_mutex_lock(&stdout_mutex);
    printf("\n---------------STATISTICS--------------------\n");
    printf("Consumers: %d - Buffer Size: %d\n", number_of_workers, buffer_size);
    printf("Number of Regular Files: %d\n", num_regular_files);
    printf("Number of FIFO Files: %d\n", num_fifo_files);
    printf("Number of Directories: %d\n", num_directories);
    printf("Number of Other Files: %d\n", num_other_files);
    printf("TOTAL BYTES COPIED: %ld\n", total_bytes_copied);
    printf("TOTAL TIME: %02ld:%02ld.%03ld (min:sec.millis)\n", minutes, seconds, milliseconds);
    pthread_mutex_unlock(&stdout_mutex);

    //-------- Free resources --------//
    free(task_buffer);
    free(workers);
    destroy_mutexes_and_cond_vars();
    pthread_barrier_destroy(&barrier);

    return 0;
}

//----------------------------------------------------------------//

void *worker_thread(void *arg)
{
    while (1)
    {
        file_task_t task;

        pthread_mutex_lock(&buffer_mutex);
        while (buffer_count == 0 && !done_flag)
            pthread_cond_wait(&buffer_not_empty, &buffer_mutex);

        if (buffer_count == 0 && done_flag)
        {
            // Update number of finished threads
            pthread_mutex_lock(&finished_threads_mutex);
            number_finish_threads++;
            pthread_mutex_unlock(&finished_threads_mutex);
            pthread_mutex_unlock(&buffer_mutex);
            break;
        }

        task = task_buffer[buffer_front];
        buffer_front = (buffer_front + 1) % buffer_size;
        buffer_count--;

        pthread_cond_signal(&buffer_not_full);
        pthread_mutex_unlock(&buffer_mutex);

        copy_file(&task);

        // Check if there are enough remaining tasks for all threads to participate in the barrier
        pthread_mutex_lock(&finished_threads_mutex);
        if (number_finish_threads == 0 && buffer_count + (number_of_workers - number_finish_threads) <= buffer_size)
        {
            pthread_mutex_unlock(&finished_threads_mutex);
            pthread_barrier_wait(&barrier);
        }
        else
        {
            pthread_mutex_unlock(&finished_threads_mutex);
        }
    }
    return NULL;
}

//----------------------------------------------------------------//

void *manager_thread(void *arg)
{
    directory_paths_t *paths = (directory_paths_t *)arg;
    traverse_directory(paths->source_folder, paths->destination_folder);

    pthread_mutex_lock(&buffer_mutex);
    done_flag = 1;
    pthread_cond_broadcast(&buffer_not_empty);
    pthread_cond_broadcast(&buffer_not_full);
    pthread_mutex_unlock(&buffer_mutex);

    return NULL;
}

//----------------------------------------------------------------//

void traverse_directory(const char *source_folder, const char *destination_folder)
{
    DIR *dir;
    struct dirent *entry;
    if ((dir = opendir(source_folder)) == NULL)
    {
        pthread_mutex_lock(&stdout_mutex);
        perror("Failed to opendir source_folder");
        pthread_mutex_unlock(&stdout_mutex);
        return;
    }

    struct stat st = {0};
    if (stat(destination_folder, &st) == -1)
    {
        if (mkdir(destination_folder, 0755) == -1)
        {
            pthread_mutex_lock(&stdout_mutex);
            perror("Failed to mkdir destination_folder");
            pthread_mutex_unlock(&stdout_mutex);
            closedir(dir);
            return;
        }
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (done_flag)
        {
            closedir(dir);
            return;
        }

        if (entry->d_type == DT_DIR)
        {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                handle_directory(source_folder, destination_folder, entry);
        }
        else
            handle_file(source_folder, destination_folder, entry);
    }
    closedir(dir);
}

//----------------------------------------------------------------//

void handle_directory(const char *source_folder, const char *destination_folder, const struct dirent *entry)
{
    char new_source[MAX_PATH_SIZE];
    char new_dest[MAX_PATH_SIZE];

    snprintf(new_source, MAX_PATH_SIZE, "%s/%s", source_folder, entry->d_name);
    snprintf(new_dest, MAX_PATH_SIZE, "%s/%s", destination_folder, entry->d_name);

    traverse_directory(new_source, new_dest);

    pthread_mutex_lock(&stats_mutex);
    num_directories++;
    pthread_mutex_unlock(&stats_mutex);
}

//----------------------------------------------------------------//

void handle_file(const char *source_folder, const char *destination_folder, const struct dirent *entry)
{
    //-------- Open target file in source and create it in destination --------//
    char source_file[MAX_PATH_SIZE];
    char destination_file[MAX_PATH_SIZE];
    snprintf(source_file, MAX_PATH_SIZE, "%s/%s", source_folder, entry->d_name);
    snprintf(destination_file, MAX_PATH_SIZE, "%s/%s", destination_folder, entry->d_name);

    int source_fd = open(source_file, O_RDONLY);
    if (source_fd == -1)
    {
        pthread_mutex_lock(&stdout_mutex);
        perror("Failed to open source file for reading");
        pthread_mutex_unlock(&stdout_mutex);
        return;
    }

    int destination_fd = open(destination_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (destination_fd == -1)
    {
        pthread_mutex_lock(&stdout_mutex);
        perror("Failed to open destination file for writing");
        pthread_mutex_unlock(&stdout_mutex);
        close_fd(source_fd);
        return;
    }

    //-------- Add file copying task to buffer --------//
    file_task_t task;
    task.source_fd = source_fd;
    task.destination_fd = destination_fd;
    strcpy(task.source_file, source_file);
    strcpy(task.destination_file, destination_file);

    pthread_mutex_lock(&buffer_mutex);
    while (buffer_count == buffer_size && !done_flag)
        pthread_cond_wait(&buffer_not_full, &buffer_mutex);

    if (done_flag)
    {
        pthread_mutex_unlock(&buffer_mutex);
        close_fd(source_fd);
        close_fd(destination_fd);
        return;
    }

    task_buffer[buffer_rear] = task;
    buffer_rear = (buffer_rear + 1) % buffer_size;
    buffer_count++;

    pthread_cond_signal(&buffer_not_empty);
    pthread_mutex_unlock(&buffer_mutex);

    update_statistics(entry);
}

//----------------------------------------------------------------//

void copy_file(file_task_t *task)
{
    char buffer[1024];
    ssize_t read_bytes, written_bytes;
    off_t total_bytes = 0;

    while ((read_bytes = read(task->source_fd, buffer, sizeof(buffer))) > 0)
    {
        ssize_t total_written = 0;
        while (total_written < read_bytes)
        {
            written_bytes = write(task->destination_fd, buffer + total_written, read_bytes - total_written);
            if (written_bytes == -1)
            {
                pthread_mutex_lock(&stdout_mutex);
                perror("Error writing to destination file");
                pthread_mutex_unlock(&stdout_mutex);

                // Clean resources
                close_fd(task->source_fd);
                close_fd(task->destination_fd);
                pthread_mutex_lock(&buffer_mutex);
                done_flag = 1;
                pthread_cond_broadcast(&buffer_not_empty);
                pthread_cond_broadcast(&buffer_not_full);
                pthread_mutex_unlock(&buffer_mutex);
                return;
            }
            total_written += written_bytes;
        }
        total_bytes += total_written;
    }

    if (read_bytes == -1)
    {
        pthread_mutex_lock(&stdout_mutex);
        perror("Error reading from source file");
        pthread_mutex_unlock(&stdout_mutex);

        // Clean resources
        close_fd(task->source_fd);
        close_fd(task->destination_fd);
        pthread_mutex_lock(&buffer_mutex);
        done_flag = 1;
        pthread_cond_broadcast(&buffer_not_empty);
        pthread_cond_broadcast(&buffer_not_full);
        pthread_mutex_unlock(&buffer_mutex);
        return;
    }

    close_fd(task->source_fd);
    close_fd(task->destination_fd);

    pthread_mutex_lock(&stats_mutex);
    total_bytes_copied += total_bytes;
    pthread_mutex_unlock(&stats_mutex);

    pthread_mutex_lock(&stdout_mutex);
    printf("Copied %s to %s, %ld bytes\n", task->source_file, task->destination_file, total_bytes);
    pthread_mutex_unlock(&stdout_mutex);
}

//----------------------------------------------------------------//

void update_statistics(const struct dirent *entry)
{
    pthread_mutex_lock(&stats_mutex);
    if (entry->d_type == DT_REG)
        num_regular_files++;
    else if (entry->d_type == DT_FIFO)
        num_fifo_files++;
    else
        num_other_files++;
    pthread_mutex_unlock(&stats_mutex);
}

//----------------------------------------------------------------//

void setup_signal_handler()
{
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("Failed to set up SIGINT handler");
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1)
    {
        perror("Failed to set up SIGTERM handler");
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGQUIT, &sa, NULL) == -1)
    {
        perror("Failed to set up SIGQUIT handler");
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGHUP, &sa, NULL) == -1)
    {
        perror("Failed to set up SIGHUP handler");
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGUSR1, &sa, NULL) == -1)
    {
        perror("Failed to set up SIGUSR1 handler");
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGUSR2, &sa, NULL) == -1)
    {
        perror("Failed to set up SIGUSR2 handler");
        exit(EXIT_FAILURE);
    }
}

//----------------------------------------------------------------//

void handle_signal(int signal)
{
    done_flag = 1;
    pthread_cond_broadcast(&buffer_not_empty);
    pthread_cond_broadcast(&buffer_not_full);
}

//----------------------------------------------------------------//

void destroy_mutexes_and_cond_vars()
{
    pthread_mutex_destroy(&buffer_mutex);
    pthread_mutex_destroy(&stats_mutex);
    pthread_mutex_destroy(&stdout_mutex);
    pthread_mutex_destroy(&finished_threads_mutex);
    pthread_cond_destroy(&buffer_not_empty);
    pthread_cond_destroy(&buffer_not_full);
}

//----------------------------------------------------------------//

void close_fd(int fd)
{
    if (fd != -1 && close(fd) == -1)
    {
        perror("Failed to close file descriptor");
        exit(EXIT_FAILURE);
    }
}
