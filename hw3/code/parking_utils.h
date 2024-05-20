#ifndef _PARKING_UTILS_H
#define _PARKING_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#define MAX_AUTOMOBILES 8
#define MAX_PICKUPS 4
#define BUFFER_SIZE 256

#define SEM_NEW_PICKUP "/sem_newPickup"
#define SEM_IN_CHARGE_PICKUP "/sem_inChargeforPickup"
#define SEM_NEW_AUTOMOBILE "/sem_newAutomobile"
#define SEM_IN_CHARGE_AUTOMOBILE "/sem_inChargeforAutomobile"
#define SEM_PICKUP_COUNTER_CONTROL "/sem_pickupCounterControl"
#define SEM_AUTOMOBILE_COUNTER_CONTROL "/sem_automobileCounterControl"

#define SHM_PARKING_LOT "/shm_parking_lot"

typedef struct
{
    int mFree_automobile;
    int mFree_pickup;
} ParkingLot;

typedef struct
{
    int id;
    int vehicleType;
} Vehicle;

ParkingLot *init_shared_memory(const char *shm_name)
{
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("Failed to create shared memory");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, sizeof(ParkingLot)) == -1)
    {
        perror("Failed to set size of shared memory");
        exit(EXIT_FAILURE);
    }
    ParkingLot *shm_ptr = (ParkingLot *)mmap(0, sizeof(ParkingLot), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED)
    {
        perror("Failed to map shared memory");
        exit(EXIT_FAILURE);
    }
    shm_ptr->mFree_automobile = MAX_AUTOMOBILES;
    shm_ptr->mFree_pickup = MAX_PICKUPS;
    return shm_ptr;
}

void free_shared_memory(ParkingLot *shm_ptr, const char *shm_name)
{
    if (munmap(shm_ptr, sizeof(ParkingLot)) == -1)
    {
        perror("Failed to unmap shared memory");
    }
    if (shm_unlink(shm_name) == -1)
    {
        perror("Failed to unlink shared memory");
    }
}

sem_t *init_semaphore(const char *sem_name, unsigned int value)
{
    sem_t *sem_ptr = sem_open(sem_name, O_CREAT, 0666, value);
    if (sem_ptr == SEM_FAILED)
    {
        perror("Failed to open semaphore");
        exit(EXIT_FAILURE);
    }
    return sem_ptr;
}

void free_semaphore(sem_t *sem_ptr, const char *sem_name)
{
    if (sem_close(sem_ptr) == -1)
    {
        perror("Failed to close semaphore");
    }
    if (sem_unlink(sem_name) == -1)
    {
        perror("Failed to unlink semaphore");
    }
}

void print(const char *message)
{
    ssize_t bytes_written;
    while (((bytes_written = write(STDOUT_FILENO, message, strlen(message))) == -1) && errno == EINTR)
        ;
    if (bytes_written == -1)
    {
        perror("Failed to write to stdout");
        exit(EXIT_FAILURE);
    }
}

#endif
