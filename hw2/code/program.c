#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>

volatile sig_atomic_t child_count = 2;
volatile sig_atomic_t child_finished = 0;

#define SERVER_FIFO_PATH "/tmp/server_fifo"
#define FIFO1_PATH "/tmp/fifo1"
#define FIFO2_PATH "/tmp/fifo2"

#define COMMAND "multiply"
#define REQUEST "REQUEST_MESSAGE"
#define REQUEST_SIZE 20

void handle_child1(int arr_size);
void handle_child2(int arr_size);
void sigchld_handler(int signo);
void sigusr1_handler(int signo);
void create_fifo(const char *fifo_path);
int open_fifo_with_retry(const char *fifo_path, int flags);
ssize_t read_fifo_with_retry(int fd, void *buf, size_t count);
ssize_t write_fifo_with_retry(int fd, const void *buf, size_t count);
void remove_fifo(const char *fifo_path);
void close_fd(int fd);
void print(const char *message);

int main(int argc, char *argv[])
{
    //--- Check validity of arguments --------------------------------------------------
    if (argc != 2)
    {
        printf("Usage: %s <array_size>\n", argv[0]);
        return 1;
    }
    int arr_size = atoi(argv[1]);
    if (arr_size <= 0)
    {
        printf("Invalid array size. Please enter a positive integer.\n");
        return 1;
    }

    //--- Create FIFOs -----------------------------------------------------------------
    create_fifo(SERVER_FIFO_PATH);
    printf("Parent process: SERVER_FIFO in '%s' is created!\n", SERVER_FIFO_PATH);
    create_fifo(FIFO1_PATH);
    printf("Parent process: FIFO1 in '%s' is created!\n", FIFO1_PATH);
    create_fifo(FIFO2_PATH);
    printf("Parent process: FIFO2 in '%s' is created!\n", FIFO2_PATH);

    //--- Create random numbers -------------------------------------------------------
    int *numbers = (int *)malloc(arr_size * sizeof(int));
    if (numbers == NULL)
    {
        perror("Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    srand(time(NULL));
    for (int i = 0; i < arr_size; i++)
        numbers[i] = rand() % arr_size;

    printf("Numbers: ");
    for (int i = 0; i < arr_size; i++)
        printf("%d ", numbers[i]);
    printf("\n");

    //--- Set signal handlers --------------------------------------------------------------
    // Signal handler for SIGCHLD
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("Failed to set SIGCHLD handler");
        exit(EXIT_FAILURE);
    }
    // Signal handler for SIGUSR1
    struct sigaction sa2;
    memset(&sa2, 0, sizeof(sa2));
    sa2.sa_handler = sigusr1_handler;
    if (sigaction(SIGUSR1, &sa2, NULL) == -1)
    {
        perror("Failed to set SIGUSR1 handler");
        exit(EXIT_FAILURE);
    }

    //--- Create child processes -----------------------------------------------------------
    for (int i = 0; i < 2; i++)
    {
        int pid = fork();
        if (pid < 0)
        {
            perror("Failed to fork");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
        {
            sleep(10);
            if (kill(getppid(), SIGUSR1) == -1)
            {
                perror("Failed to send signal with kill");
                exit(EXIT_FAILURE);
            }
            if (i == 0)
                handle_child1(arr_size);
            else
                handle_child2(arr_size);
            exit(EXIT_SUCCESS);
        }
    }

    //--- Display proceeding message ---------------------------------------------------------
    while (!child_finished)
    {
        print("Parent process: Proceeding...\n");
        sleep(2);
    }

    //--- Open SERVER_FIFO to read a request from Child 1 ----------------------------------
    char request[REQUEST_SIZE];
    int server_fifo_fd = open_fifo_with_retry(SERVER_FIFO_PATH, O_RDONLY);
    if (server_fifo_fd == -1)
    {
        perror("Failed to open SERVER_FIFO for reading");
        close(server_fifo_fd);
        free(numbers);
        exit(EXIT_FAILURE);
    }

    //--- Read request from Child 1 --------------------------------------------------------
    if (read_fifo_with_retry(server_fifo_fd, &request, strlen(REQUEST)) == -1)
    {
        perror("Failed to read from SERVER_FIFO");
        close_fd(server_fifo_fd);
        free(numbers);
        exit(EXIT_FAILURE);
    }

    //--- Open FIFO1 to write --------------------------------------------------------------
    int fifo1_fd = open_fifo_with_retry(FIFO1_PATH, O_WRONLY);
    if (fifo1_fd == -1)
    {
        perror("Failed to open FIFO1 for writing");
        close_fd(server_fifo_fd);
        close_fd(fifo1_fd);
        free(numbers);
        exit(EXIT_FAILURE);
    }

    //--- Write array to FIFO1 -------------------------------------------------------------
    if (write_fifo_with_retry(fifo1_fd, numbers, arr_size * sizeof(int)) == -1)
    {
        perror("Failed to write numbers to FIFO1");
        close_fd(server_fifo_fd);
        close_fd(fifo1_fd);
        free(numbers);
        exit(EXIT_FAILURE);
    }
    print("Parent process: numbers array are written to FIFO1\n");
    close_fd(server_fifo_fd);

    //--- Open FIFO2 to write --------------------------------------------------------------
    int fifo2_fd = open_fifo_with_retry(FIFO2_PATH, O_WRONLY);
    if (fifo2_fd == -1)
    {
        perror("Failed to open FIFO2 for writing");
        close_fd(fifo1_fd);
        close_fd(fifo2_fd);
        free(numbers);
        exit(EXIT_FAILURE);
    }

    //--- Write 'multiply' command to FIFO2 ------------------------------------------------
    if (write_fifo_with_retry(fifo2_fd, COMMAND, strlen(COMMAND) + 1) == -1)
    {
        perror("Failed to write command to FIFO2");
        close_fd(fifo1_fd);
        close_fd(fifo2_fd);
        free(numbers);
        exit(EXIT_FAILURE);
    }
    print("Parent process: 'multiply' command is written to FIFO2\n");

    //--- Write array to FIFO2 -------------------------------------------------------------
    if (write_fifo_with_retry(fifo2_fd, numbers, arr_size * sizeof(int)) == -1)
    {
        perror("Failed to write numbers to FIFO2");
        close_fd(fifo1_fd);
        close_fd(fifo2_fd);
        free(numbers);
        exit(EXIT_FAILURE);
    }
    print("Parent process: numbers array are written to FIFO2\n");

    while (child_count > 0)
        ;

    // Free resources
    free(numbers);
    close_fd(fifo1_fd);
    close_fd(fifo2_fd);
    remove_fifo(SERVER_FIFO_PATH);
    remove_fifo(FIFO1_PATH);
    remove_fifo(FIFO2_PATH);

    print("Parent process: Terminating...\n");
    return 0;
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------

void handle_child1(int arr_size)
{
    int sum = 0;
    int fifo1_fd, fifo2_fd, server_fifo_fd;
    char request[REQUEST_SIZE];
    int *numbers = (int *)malloc(arr_size * sizeof(int));
    if (numbers == NULL)
    {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    //--- Open SERVER_FIFO to write a request ----------------------------------------------
    server_fifo_fd = open_fifo_with_retry(SERVER_FIFO_PATH, O_WRONLY);
    if (server_fifo_fd == -1)
    {
        perror("Failed to open SERVER_FIFO for writing");
        close_fd(server_fifo_fd);
        free(numbers);
        exit(EXIT_FAILURE);
    }

    //--- Write request to SERVER_FIFO -----------------------------------------------------
    if (write_fifo_with_retry(server_fifo_fd, REQUEST, strlen(REQUEST)) == -1)
    {
        perror("Failed to read request from SERVER_FIFO");
        close_fd(server_fifo_fd);
        free(numbers);
        exit(EXIT_FAILURE);
    }

    //--- Open FIFO1 to read ---------------------------------------------------------------
    fifo1_fd = open_fifo_with_retry(FIFO1_PATH, O_RDONLY);
    if (fifo1_fd == -1)
    {
        perror("Failed to open FIFO1 for writing");
        close_fd(server_fifo_fd);
        close_fd(fifo1_fd);
        free(numbers);
        exit(EXIT_FAILURE);
    }

    //--- Read array from FIFO1 ------------------------------------------------------------
    if (read_fifo_with_retry(fifo1_fd, numbers, arr_size * sizeof(int)) == -1)
    {
        perror("Failed to read from FIFO1");
        close_fd(server_fifo_fd);
        close_fd(fifo1_fd);
        free(numbers);
        exit(EXIT_FAILURE);
    }
    print("Child process 1: numbers are read from FIFO1\n");

    close_fd(server_fifo_fd);
    close_fd(fifo1_fd);

    for (int i = 0; i < arr_size; i++)
        sum += numbers[i];

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "Child process 1: Summation result = %d\n", sum);
    print(buffer);

    free(numbers);

    //--- Open SERVER_FIFO to read a request from Child 2 ----------------------------------
    server_fifo_fd = open_fifo_with_retry(SERVER_FIFO_PATH, O_RDONLY);
    if (server_fifo_fd == -1)
    {
        perror("Failed to open SERVER_FIFO for reading");
        close_fd(server_fifo_fd);
        exit(EXIT_FAILURE);
    }

    //--- Read request from Child 2 --------------------------------------------------------
    if (read_fifo_with_retry(server_fifo_fd, &request, strlen(REQUEST)) == -1)
    {
        perror("Failed to read from SERVER_FIFO");
        close_fd(server_fifo_fd);
        exit(EXIT_FAILURE);
    }

    //--- Open FIFO2 to write --------------------------------------------------------------
    fifo2_fd = open_fifo_with_retry(FIFO2_PATH, O_WRONLY);
    if (fifo2_fd == -1)
    {
        perror("Failed to open FIFO2 for writing");
        close_fd(server_fifo_fd);
        close_fd(fifo2_fd);
        exit(EXIT_FAILURE);
    }

    //--- Write sum to FIFO2 ---------------------------------------------------------------
    if (write_fifo_with_retry(fifo2_fd, &sum, sizeof(sum)) == -1)
    {
        perror("Failed to write to FIFO2");
        close_fd(server_fifo_fd);
        close_fd(fifo2_fd);
        exit(EXIT_FAILURE);
    }
    snprintf(buffer, sizeof(buffer), "Child process 1: sum = %d is written to FIFO2\n", sum);
    print(buffer);

    close_fd(server_fifo_fd);
    close_fd(fifo2_fd);
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------

void handle_child2(int arr_size)
{
    int fifo2_fd, server_fifo_fd;
    char command[10];
    long long mult = 1;

    //--- Open FIFO2 to read ---------------------------------------------------------------
    fifo2_fd = open_fifo_with_retry(FIFO2_PATH, O_RDONLY);
    if (fifo2_fd == -1)
    {
        perror("Failed to open FIFO2 for reading");
        close_fd(fifo2_fd);
        exit(EXIT_FAILURE);
    }

    //--- Read 'multiply' command from FIFO2 -----------------------------------------------
    if (read_fifo_with_retry(fifo2_fd, command, strlen(COMMAND) + 1) == -1)
    {
        perror("Failed to read from FIFO2");
        close_fd(fifo2_fd);
        exit(EXIT_FAILURE);
    }
    command[strlen(COMMAND)] = '\0';
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "Child process 2: command '%s' is read from FIFO2\n", command);
    print(buffer);

    //--- Compare 'multiply' command -------------------------------------------------------
    if (strcmp(command, COMMAND) == 0)
    {
        int *numbers = (int *)malloc(arr_size * sizeof(int));
        if (numbers == NULL)
        {
            perror("Memory allocation failed");
            close_fd(fifo2_fd);
            exit(EXIT_FAILURE);
        }

        //--- Read array from FIFO2 ------------------------------------------------------------
        if (read_fifo_with_retry(fifo2_fd, numbers, arr_size * sizeof(int)) == -1)
        {
            perror("Failed to read from FIFO2");
            close_fd(fifo2_fd);
            free(numbers);
            exit(EXIT_FAILURE);
        }
        print("Child process 2: numbers are read from FIFO2\n");

        for (int i = 0; i < arr_size; i++)
            mult = mult * numbers[i];

        snprintf(buffer, sizeof(buffer), "Child process 2: Multiplication result = %lld\n", mult);
        print(buffer);

        free(numbers);

        //--- Open SERVER_FIFO to write a request ----------------------------------------------
        server_fifo_fd = open_fifo_with_retry(SERVER_FIFO_PATH, O_WRONLY);
        if (server_fifo_fd == -1)
        {
            perror("Failed to open SERVER_FIFO for writing");
            close_fd(server_fifo_fd);
            exit(EXIT_FAILURE);
        }

        //--- Write request to SERVER_FIFO -----------------------------------------------------
        if (write_fifo_with_retry(server_fifo_fd, REQUEST, strlen(REQUEST)) == -1)
        {
            perror("Failed to read request from SERVER_FIFO");
            close_fd(server_fifo_fd);
            exit(EXIT_FAILURE);
        }

        int prev_sum = 0;
        //--- Read sum from FIFO2 -------------------------------------------------------------
        if (read_fifo_with_retry(fifo2_fd, &prev_sum, sizeof(prev_sum)) == -1)
        {
            perror("Failed to read from FIFO2");
            close_fd(server_fifo_fd);
            close_fd(fifo2_fd);
            exit(EXIT_FAILURE);
        }

        snprintf(buffer, sizeof(buffer), "Child process 2: sum = %d is read from FIFO2\n", prev_sum);
        print(buffer);

        long long result = (long long)prev_sum + mult;
        snprintf(buffer, sizeof(buffer), "Result: %lld\n", result);
        print(buffer);

        close_fd(fifo2_fd);
        close_fd(server_fifo_fd);
    }
    else
    {
        fprintf(stderr, "Invalid command received: %s\n", command);
        exit(EXIT_FAILURE);
    }
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------

void sigchld_handler(int signo)
{
    int status;
    pid_t pid;
    char buffer[256];
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if (WIFEXITED(status))
            snprintf(buffer, sizeof(buffer), "Child process pid: %d exited with status: %d\n", pid, WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            snprintf(buffer, sizeof(buffer), "Child process pid: %d terminated by signal: %d\n", pid, WTERMSIG(status));
        print(buffer);

        if (child_count > 0)
            child_count--;

        if (pid == -1 && errno != ECHILD)
        {
            perror("waitpid failed");
            exit(EXIT_FAILURE);
        }
    }
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------

void sigusr1_handler(int signo)
{
    child_finished = 1;
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------

void create_fifo(const char *fifo_path)
{
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    if (access(fifo_path, F_OK) == -1)
    {
        if (mkfifo(fifo_path, mode) == -1)
        {
            perror("Failed to create FIFO");
            exit(EXIT_FAILURE);
        }
    }
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------

int open_fifo_with_retry(const char *fifo_path, int flags)
{
    int fd;
    do
    {
        fd = open(fifo_path, flags);
        if (fd == -1 && errno != EINTR)
        {
            perror("Failed to open FIFO");
            return -1;
        }
    } while (fd == -1);
    return fd;
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------

ssize_t read_fifo_with_retry(int fd, void *buf, size_t count)
{
    ssize_t bytes_read;
    do
    {
        bytes_read = read(fd, buf, count);
        if (bytes_read == -1 && errno != EINTR)
        {
            perror("Failed to read from FIFO");
            return -1;
        }
    } while (bytes_read == -1);
    return bytes_read;
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------

ssize_t write_fifo_with_retry(int fd, const void *buf, size_t count)
{
    ssize_t bytes_written;
    do
    {
        bytes_written = write(fd, buf, count);
        if (bytes_written == -1 && errno != EINTR)
        {
            perror("Failed to write to FIFO");
            return -1;
        }
    } while (bytes_written == -1);
    return bytes_written;
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------

void remove_fifo(const char *fifo_path)
{
    if (unlink(fifo_path) == -1)
    {
        perror("Failed to remove FIFO");
        exit(EXIT_FAILURE);
    }
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------

void close_fd(int fd)
{
    if (close(fd) == -1)
    {
        perror("Failed to close file descriptor");
        exit(EXIT_FAILURE);
    }
}

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------

void print(const char *message)
{
    ssize_t bytes_written;
    while (((bytes_written = write(STDOUT_FILENO, message, strlen(message))) == -1) && errno == EINTR)
        ;
    if (bytes_written == -1)
    {
        perror("Error while writing to stdout.\n");
        exit(EXIT_FAILURE);
    }
}
