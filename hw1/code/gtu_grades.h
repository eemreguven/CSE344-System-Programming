#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <fcntl.h>

#define MAX_LENGTH 100
#define BUFFER_SIZE_L 1024
#define BUFFER_SIZE_M 256
#define BUFFER_SIZE_S 128

const char *grades[] = {
    "AA", "BA", "BB", "CB", "CC", "DC", "DD", "FD", "FF", "VF", "NA"};

//------------------------------------------------------------------------------------

void command_create_file(const char *filename);
void command_display_commands();
void command_add_grade(const char *filename, char *arguments);
void add_grade_to_file(const char *filename, const char *student_name, const char *student_grade);
void command_search_student(const char *filename, const char *student_name);
void command_sort_all(const char *filename);
void command_show_all(const char *filename);
void command_list_grades(const char *filename);
void command_list_some(const char *filename, char *arguments);
void print_entries_page(const char *filename, int page_size, int page_number);
void handle_command_process(const char *command, const char *filename, char *arguments);
int execute_command();
void parse_name_and_grade(char *arguments, char *student_name, char *student_grade);
void parse_command(char *command, char *sub_command, char *filename, char *arguments);
int is_token_grade(const char *token);
void log_msg(const char *entry_1, const char *entry_2, const char *message);
void print(const char *message);
void print_error(const char *message);
void handle_no_exist_file(const char *filename);
int is_file_exists(const char *filename);
void close_fd(const char *filename, int fd);

//------------------------------------------------------------------------------------

void command_create_file(const char *filename)
{
    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd == -1)
    {
        print_error("Error while opening or creating file.\n");
        log_msg(filename, NULL, "file cannot be opened or created.");
        exit(EXIT_FAILURE);
    }
    else
        log_msg(filename, NULL, "file is created.");

    close_fd(filename, fd);
}

//------------------------------------------------------------------------------------

void command_display_commands()
{
    const char *commands_manual =
        "\n-----------------------------------------------------\n"
        "COMMAND\n\t gtuStudentGrades \"filename\"\n"
        "\nDESCRIPTION\n\t Create a file in given filename.\n"
        "\nUSAGE\n\t gtuStudentGrades grades.txt\n"
        "-----------------------------------------------------\n"
        "COMMAND\n\t addStudentGrade \"filename\" \"Name Surname\" \"Grade\"\n"
        "\nDESCRIPTION\n\t Append student name surname and grade to the end of the file.\n"
        "\t If student already exists in file, update his/her grade.\n"
        "\n\t Possible grades:\n"
        "\t 'AA', 'BA', 'BB', 'CB', 'CC', 'DC', 'DD', 'FD', 'FF', 'VF', 'NA'\n"
        "\nUSAGE\n\t addStudentGrade grades.txt Emre Güven AA\n"
        "-----------------------------------------------------\n"
        "COMMAND\n\t searchStudent \"filename\" \"Name Surname\"\n"
        "\nDESCRIPTION\n\t Display student name surname and grade from given filename.\n"
        "\nUSAGE\n\t searchStudent grades.txt Emre Güven\n"
        "-----------------------------------------------------\n"
        "COMMAND\n\t sortAll \"filename\" \"sort option\" \"order option\"\n"
        "\nDESCRIPTION\n\t Display all entries by sorting according to options.\n"
        "\t sort option : n - g | n: name - g: grade\n"
        "\t order option: a - d | a: ascending - d: descending\n"
        "\nUSAGE\n\t sortAll grades.txt n a\n"
        "\t Displays all of the sorted entries in grades.txt, sorted by name in ascending order. \n"
        "-----------------------------------------------------\n"
        "COMMAND\n\t showAll \"filename\"\n"
        "\nDESCRIPTION\n\t Display all of the entries in the file.\n"
        "\nUSAGE\n\t showAll grades.txt\n"
        "-----------------------------------------------------\n"
        "COMMAND\n\t listGrades \"filename\"\n"
        "\nDESCRIPTION\n\t Display first 5 entries in the file.\n"
        "\nUSAGE\n\t listGrades grades.txt\n"
        "-----------------------------------------------------\n"
        "COMMAND\n\t listSome \"numofEntries\" \"pageNumber\" \"filename\"\n"
        "\nDESCRIPTION\n\t Display number of the entries in given page number in the file.\n"
        "\nUSAGE\n\t listSome 5 2 grades.txt\n"
        "\t Displays 5 entries in the 2nd page in the file, 6th-10th entries. \n"
        "-----------------------------------------------------\n";
    print(commands_manual);
}

//------------------------------------------------------------------------------------

void command_add_grade(const char *filename, char *arguments)
{
    if (!is_file_exists(filename))
    {
        handle_no_exist_file(filename);
        return;
    }

    char student_name[50];
    char student_grade[3];
    parse_name_and_grade(arguments, student_name, student_grade);
    add_grade_to_file(filename, student_name, student_grade);
}

//------------------------------------------------------------------------------------

void add_grade_to_file(const char *filename, const char *student_name, const char *student_grade)
{
    int fd = open(filename, O_RDWR);
    if (fd == -1)
    {
        print_error("Error while opening file.\n");
        log_msg(filename, NULL, "file cannot be opened.");
        exit(EXIT_FAILURE);
    }

    // update grade if student exists
    char buffer[BUFFER_SIZE_L];
    ssize_t bytes_read;
    off_t offset = 0;
    int student_found = 0;
    while ((bytes_read = read(fd, buffer, BUFFER_SIZE_L)) > 0 && !student_found)
    {
        buffer[bytes_read] = '\0';
        char *entry_start = buffer;
        char *entry_end;

        while (entry_end = strchr(entry_start, '\n'))
        {
            char *token = strtok(entry_start, ",");
            if (strcmp(student_name, token) == 0)
            {
                off_t position = offset + (entry_start - buffer) + strlen(student_name) + strlen(", ");
                if (lseek(fd, position, SEEK_SET) == -1)
                {
                    print_error("Error while seeking to position.\n");
                    close_fd(filename, fd);
                    exit(EXIT_FAILURE);
                }

                ssize_t bytes_written = write(fd, student_grade, strlen(student_grade));
                if (bytes_written == -1)
                {
                    print_error("Error while writing to file.\n");
                    log_msg(filename, NULL, " grade cannot be updated.");
                    close_fd(filename, fd);
                    exit(EXIT_FAILURE);
                }
                else
                    log_msg(filename, NULL, "grade is updated.");
                student_found = 1;
                break;
            }
            entry_start = entry_end + 1;
        }
        offset += bytes_read;
    }

    if (bytes_read == -1)
    {
        print_error("Error while reading from file.\n");
        close_fd(filename, fd);
        exit(EXIT_FAILURE);
    }

    // append student
    if (!student_found)
    {
        char written_text[BUFFER_SIZE_M];
        strcpy(written_text, student_name);
        strcat(written_text, ", ");
        strcat(written_text, student_grade);

        ssize_t bytes_written = write(fd, written_text, strlen(written_text));
        if (bytes_written == -1)
        {
            print_error("Error while writing to file.\n");
            log_msg(filename, written_text, "cannot be added.");
            close_fd(filename, fd);
            exit(EXIT_FAILURE);
        }
        else
            log_msg(filename, written_text, "is added.");

        bytes_written = write(fd, "\n", 1);
        if (bytes_written == -1)
        {
            print_error("Error while writing to file.\n");
            close_fd(filename, fd);
            exit(EXIT_FAILURE);
        }
    }

    close_fd(filename, fd);
}

//------------------------------------------------------------------------------------

void command_search_student(const char *filename, const char *student_name)
{
    if (!is_file_exists(filename))
    {
        handle_no_exist_file(filename);
        return;
    }

    int fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        print_error("Error while opening file.\n");
        log_msg(filename, NULL, "file cannot be opened.");
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE_L];
    ssize_t bytes_read;
    int student_found = 0;
    while ((bytes_read = read(fd, buffer, BUFFER_SIZE_L)) > 0 && !student_found)
    {
        buffer[bytes_read] = '\0';
        char *entry_start = buffer;
        char *entry_end;

        while (entry_end = strchr(entry_start, '\n'))
        {
            char *token = strtok(entry_start, ",");
            if (strcmp(student_name, token) == 0)
            {
                token = strtok(NULL, "\n");

                char written_text[BUFFER_SIZE_L];
                strcpy(written_text, student_name);
                strcat(written_text, " -");
                strcat(written_text, token);
                print(written_text);
                log_msg(filename, written_text, "is displayed.");

                student_found = 1;
                break;
            }
            entry_start = entry_end + 1;
        }
    }

    if (bytes_read == -1)
    {
        print_error("Error while reading from file.\n");
        close_fd(filename, fd);
        exit(EXIT_FAILURE);
    }

    if (!student_found)
    {
        log_msg(filename, student_name, "cannot found in the file.");

        char written_text[BUFFER_SIZE_M];
        strcpy(written_text, student_name);
        strcat(written_text, " cannot found in the file: ");
        strcat(written_text, filename);
        print(written_text);
    }

    close_fd(filename, fd);
}

//------------------------------------------------------------------------------------

void command_sort_all(const char *filename)
{
    if (!is_file_exists(filename))
    {
        handle_no_exist_file(filename);
        return;
    }
    print("NOT IMPLEMENTED");
}

//------------------------------------------------------------------------------------

void command_show_all(const char *filename)
{
    if (!is_file_exists(filename))
    {
        handle_no_exist_file(filename);
        return;
    }

    int fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        print_error("Error while opening file.\n");
        log_msg(filename, NULL, "file cannot be opened.");
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE_L];
    ssize_t bytes_read;
    while ((bytes_read = read(fd, buffer, BUFFER_SIZE_L)) > 0)
    {
        if (write(STDOUT_FILENO, buffer, bytes_read) == -1)
        {
            print_error("Error while writing to stdout.\n");
            close_fd(filename, fd);
            exit(EXIT_FAILURE);
        }
    }

    if (bytes_read == -1)
    {
        print_error("Error while reading from file.\n");
        close_fd(filename, fd);
        exit(EXIT_FAILURE);
    }
    log_msg(filename, NULL, "all entries are displayed.");
    close_fd(filename, fd);
}

//------------------------------------------------------------------------------------

void command_list_grades(const char *filename)
{
    if (!is_file_exists(filename))
    {
        handle_no_exist_file(filename);
        return;
    }
    print_entries_page(filename, 5, 1);
    log_msg(filename, NULL, " first 5 entries are displayed.");
}

//------------------------------------------------------------------------------------

void command_list_some(const char *filename, char *arguments)
{
    if (!is_file_exists(filename))
    {
        handle_no_exist_file(filename);
        return;
    }
    int page_size = 0;
    int page_number = 0;

    char *token = strtok(arguments, " ");
    if (token != NULL)
    {
        // atoi function expects numeric string
        page_size = atoi(token);
        token = strtok(NULL, " ");
        if (token != NULL)
            page_number = atoi(token);
    }

    print_entries_page(filename, page_size, page_number);
    log_msg(filename, NULL, " some entries are displayed.");
}

//------------------------------------------------------------------------------------

void print_entries_page(const char *filename, int page_size, int page_number)
{
    int fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        print_error("Error while opening file.\n");
        log_msg(filename, NULL, "file cannot be opened.");
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE_L];
    ssize_t bytes_read;
    int entry_count = 0;
    int current_entry = 1;

    int start_entry = (page_number - 1) * page_size + 1;
    int end_entry = start_entry + page_size - 1;

    while ((bytes_read = read(fd, buffer, BUFFER_SIZE_L)) > 0 && current_entry <= end_entry)
    {
        buffer[bytes_read] = '\0';

        char *entry_start = buffer;
        char *entry_end;

        while ((entry_end = strchr(entry_start, '\n')) != NULL && current_entry <= end_entry)
        {
            if (current_entry >= start_entry)
            {
                if (write(STDOUT_FILENO, entry_start, entry_end - entry_start + 1) == -1)
                {
                    print_error("Error while writing to stdout.\n");
                    exit(EXIT_FAILURE);
                }
            }
            entry_start = entry_end + 1;
            current_entry++;
        }
    }

    if (bytes_read == -1)
    {
        print_error("Error while reading from file.\n");
        close_fd(filename, fd);
        exit(EXIT_FAILURE);
    }
    close_fd(filename, fd);
}

//------------------------------------------------------------------------------------

void handle_command_process(const char *command, const char *filename, char *arguments)
{
    if (strcmp(command, "gtuStudentGrades") == 0)
    {
        if (filename[0] != '\0')
            command_create_file(filename);
        else
            command_display_commands();
    }
    else if (strcmp(command, "addStudentGrade") == 0)
    {
        command_add_grade(filename, arguments);
    }
    else if (strcmp(command, "searchStudent") == 0)
    {
        command_search_student(filename, arguments);
    }
    else if (strcmp(command, "sortAll") == 0)
    {
        command_sort_all(filename);
    }
    else if (strcmp(command, "showAll") == 0)
    {
        command_show_all(filename);
    }
    else if (strcmp(command, "listGrades") == 0)
    {
        command_list_grades(filename);
    }
    else if (strcmp(command, "listSome") == 0)
    {
        command_list_some(filename, arguments);
    }
    else
    {
        log_msg(command, NULL, "command not found.");

        char write_text[BUFFER_SIZE_S];
        strcpy(write_text, "'");
        strcat(write_text, command);
        strcat(write_text, "' command not found.");
        print(write_text);
    }
    exit(EXIT_SUCCESS);
}

//------------------------------------------------------------------------------------

int execute_command()
{
    char command[100];
    char sub_command[30];
    char filename[50];
    char arguments[100];

    if (write(STDOUT_FILENO, "~$ ", 3) == -1)
    {
        print_error("Error while writing to stdout.\n");
        exit(EXIT_FAILURE);
    }

    ssize_t bytes_read = read(STDIN_FILENO, command, sizeof(command));
    if (bytes_read == -1)
    {
        print_error("Error while reading from stdin.\n");
        exit(EXIT_FAILURE);
    }

    if (command[bytes_read - 1] == '\n')
        command[bytes_read - 1] = '\0';

    log_msg(command, NULL, "command is entered.");

    if (strcmp(command, "q") == 0)
        return -1;
    else
    {
        parse_command(command, sub_command, filename, arguments);

        pid_t pid = fork();
        if (pid < 0)
        {
            print_error("Error while creating child process.\n");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
            handle_command_process(sub_command, filename, arguments);
        else
        {
            int status;
            if (waitpid(pid, &status, 0) == -1)
            {
                print_error("Error while waiting child process.\n");
                exit(EXIT_FAILURE);
            }
        }
    }
    return 0;
}

//------------------------------------------------------------------------------------

void parse_name_and_grade(char *arguments, char *student_name, char *student_grade)
{
    char *token = strtok(arguments, " ");
    if (token != NULL)
    {
        strcpy(student_name, token);
        token = strtok(NULL, " ");
        while (token != NULL)
        {
            if (is_token_grade(token)) // grade
            {
                strcpy(student_grade, token);
                break;
            }
            else // remaining part of name
            {
                strcat(student_name, " ");
                strcat(student_name, token);
                token = strtok(NULL, " ");
            }
        }
    }
}

//------------------------------------------------------------------------------------

void parse_command(char *command, char *sub_command, char *filename, char *arguments)
{
    char *token = strtok(command, " ");
    if (token != NULL)
    {
        strcpy(sub_command, token);
        token = strtok(NULL, " ");
        if (token != NULL)
        {
            if (strcmp(sub_command, "listSome") == 0)
            {
                strcpy(arguments, token); // page size
                token = strtok(NULL, " ");
                if (token != NULL)
                {
                    strcat(arguments, " ");
                    strcat(arguments, token); // page number
                    token = strtok(NULL, " ");
                    if (token != NULL)
                        strcpy(filename, token);
                    else
                        filename[0] = '\0';
                }
                else
                {
                    filename[0] = '\0';
                    arguments[0] = '\0';
                }
            }
            else
            {
                strcpy(filename, token);
                token = strtok(NULL, "");
                if (token != NULL)
                    strcpy(arguments, token);
                else
                    arguments[0] = '\0';
            }
        }
        else
        {
            filename[0] = '\0';
            arguments[0] = '\0';
        }
    }
    else
    {
        sub_command[0] = '\0';
        filename[0] = '\0';
        arguments[0] = '\0';
    }
}

//------------------------------------------------------------------------------------

int is_token_grade(const char *token)
{
    for (int i = 0; i < sizeof(grades) / sizeof(grades[0]); i++)
    {
        if (strcmp(token, grades[i]) == 0)
            return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------------

// log record in desired format
void log_msg(const char *entry_1, const char *entry_2, const char *message)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        print_error("Error while creating child process.\n");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        int fd = open("log.txt", O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd == -1)
        {
            print_error("Error while opening or creating log file.\n");
            exit(EXIT_FAILURE);
        }

        char log_message[BUFFER_SIZE_M] = "";

        static char timeBuffer[20];
        time_t rawtime;
        struct tm *timeinfo;

        time(&rawtime);
        timeinfo = localtime(&rawtime);
        strftime(timeBuffer, 20, "%Y-%m-%dT%H:%M:%S", timeinfo);
        strcpy(log_message, timeBuffer);
        strcat(log_message, " | ");

        if (entry_1 != NULL)
        {
            strcat(log_message, entry_1);
            strcat(log_message, " | ");
        }
        if (entry_2 != NULL)
        {
            strcat(log_message, entry_2);
            strcat(log_message, " ");
        }
        strcat(log_message, message);
        strcat(log_message, "\n");

        ssize_t bytes_written = write(fd, log_message, strlen(log_message));
        if (bytes_written == -1)
        {
            print_error("Error while writing to log file.\n");
            close(fd);
            exit(EXIT_FAILURE);
        }

        if (close(fd) == -1)
        {
            print_error("Error while writing to log file.\n");
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }
    else
    {
        int status;
        if (waitpid(pid, &status, 0) == -1)
        {
            print_error("Error while waiting child process.\n");
            exit(EXIT_FAILURE);
        }
    }
}

//------------------------------------------------------------------------------------

void print(const char *message)
{
    ssize_t bytes_written = write(STDOUT_FILENO, message, strlen(message));
    if (bytes_written == -1)
    {
        print_error("Error while writing to stdout.\n");
        exit(EXIT_FAILURE);
    }

    bytes_written = write(STDOUT_FILENO, "\n", 1);
    if (bytes_written == -1)
    {
        print_error("Error while writing to stdout.\n");
        exit(EXIT_FAILURE);
    }
}

//------------------------------------------------------------------------------------

void print_error(const char *message)
{
    ssize_t bytes_written = write(STDERR_FILENO, message, strlen(message));
    if (bytes_written == -1)
    {
        exit(EXIT_FAILURE);
    }

    bytes_written = write(STDERR_FILENO, "\n", 1);
    if (bytes_written == -1)
    {
        exit(EXIT_FAILURE);
    }
}

//------------------------------------------------------------------------------------

int is_file_exists(const char *filename)
{
    struct stat buffer;
    if (stat(filename, &buffer) == -1)
        return 0;

    return 1;
}

//------------------------------------------------------------------------------------

void handle_no_exist_file(const char *filename)
{
    log_msg(filename, NULL, "file does not exist.");

    char write_text[BUFFER_SIZE_S];
    strcpy(write_text, "'");
    strcat(write_text, filename);
    strcat(write_text, "' file does not exist.");
    print(write_text);
}

//------------------------------------------------------------------------------------

void close_fd(const char *filename, int fd)
{
    if (close(fd) == -1)
    {
        print_error("Error while closing file.\n");
        log_msg(filename, NULL, "file cannot be closed.");
        exit(EXIT_FAILURE);
    }
}