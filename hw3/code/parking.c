#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "parking_utils.h"

#define NUM_VEHICLES 50

sem_t *newPickup;
sem_t *inChargeforPickup;
sem_t *newAutomobile;
sem_t *inChargeforAutomobile;
sem_t *pickupCounterControl;
sem_t *automobileCounterControl;

ParkingLot *parkingLot;

void *carOwner(void *arg);
void *carAttendant(void *arg);
void parkingSimulation();

volatile int stopFlag = 0;

int main()
{
    srand(time(NULL));

    parkingLot = init_shared_memory(SHM_PARKING_LOT);
    newPickup = init_semaphore(SEM_NEW_PICKUP, 0);
    inChargeforPickup = init_semaphore(SEM_IN_CHARGE_PICKUP, 1);
    newAutomobile = init_semaphore(SEM_NEW_AUTOMOBILE, 0);
    inChargeforAutomobile = init_semaphore(SEM_IN_CHARGE_AUTOMOBILE, 1);
    pickupCounterControl = init_semaphore(SEM_PICKUP_COUNTER_CONTROL, 1);
    automobileCounterControl = init_semaphore(SEM_AUTOMOBILE_COUNTER_CONTROL, 1);

    parkingSimulation();

    free_shared_memory(parkingLot, SHM_PARKING_LOT);
    free_semaphore(newPickup, SEM_NEW_PICKUP);
    free_semaphore(inChargeforPickup, SEM_IN_CHARGE_PICKUP);
    free_semaphore(newAutomobile, SEM_NEW_AUTOMOBILE);
    free_semaphore(inChargeforAutomobile, SEM_IN_CHARGE_AUTOMOBILE);
    free_semaphore(pickupCounterControl, SEM_PICKUP_COUNTER_CONTROL);
    free_semaphore(automobileCounterControl, SEM_AUTOMOBILE_COUNTER_CONTROL);

    return 0;
}

void parkingSimulation()
{
    pthread_t threads[NUM_VEHICLES + 2];
    pthread_create(&threads[0], NULL, carAttendant, (void *)"automobile");
    pthread_create(&threads[1], NULL, carAttendant, (void *)"pickup");

    for (int i = 0; i < NUM_VEHICLES; i++)
    {
        int *vehicleType = (int *)malloc(sizeof(int));
        *vehicleType = rand() % 2; // 0 for automobile, 1 for pickup
        pthread_create(&threads[2 + i], NULL, carOwner, (void *)vehicleType);
        usleep(rand() % 100000);
    }

    for (int i = 2; i < NUM_VEHICLES + 2; i++)
    {
        pthread_join(threads[i], NULL);
    }

    stopFlag = 1; 

    sem_post(newAutomobile);
    sem_post(newPickup);

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
}

void *carOwner(void *arg)
{
    char buffer[BUFFER_SIZE];
    int vehicleType = *(int *)arg;
    free(arg);

    if (vehicleType == 0)
    {
        sem_wait(automobileCounterControl);
        snprintf(buffer, BUFFER_SIZE, "Car Owner: Attempting to park an automobile.\n");
        print(buffer);
        if (parkingLot->mFree_automobile > 0)
        {
            parkingLot->mFree_automobile--;
            sem_post(automobileCounterControl);
            snprintf(buffer, BUFFER_SIZE, "Car Owner: Found a spot for an automobile. Remaining: %d\n", parkingLot->mFree_automobile);
            print(buffer);
            sem_post(newAutomobile);
            sem_wait(inChargeforAutomobile);
            snprintf(buffer, BUFFER_SIZE, "Car Owner: Parked an automobile successfully.\n");
            print(buffer);
        }
        else
        {
            sem_post(automobileCounterControl);
            snprintf(buffer, BUFFER_SIZE, "Car Owner: No space for an automobile, leaving.\n");
            print(buffer);
        }
    }
    else
    {
        sem_wait(pickupCounterControl);
        snprintf(buffer, BUFFER_SIZE, "Car Owner: Attempting to park a pickup.\n");
        print(buffer);
        if (parkingLot->mFree_pickup > 0)
        {
            parkingLot->mFree_pickup--;
            sem_post(pickupCounterControl);
            snprintf(buffer, BUFFER_SIZE, "Car Owner: Found a spot for a pickup. Remaining: %d\n", parkingLot->mFree_pickup);
            print(buffer);
            sem_post(newPickup);
            sem_wait(inChargeforPickup);
            snprintf(buffer, BUFFER_SIZE, "Car Owner: Parked a pickup successfully.\n");
            print(buffer);
        }
        else
        {
            sem_post(pickupCounterControl);
            snprintf(buffer, BUFFER_SIZE, "Car Owner: No space for a pickup, leaving.\n");
            print(buffer);
        }
    }
    return NULL;
}

void *carAttendant(void *arg)
{
    char buffer[BUFFER_SIZE];
    char *vehicleType = (char *)arg;
    while (!stopFlag)
    {
        if (strcmp(vehicleType, "automobile") == 0)
        {
            sem_wait(newAutomobile);
            if (stopFlag)
                break;
            sem_wait(automobileCounterControl);
            parkingLot->mFree_automobile++;
            snprintf(buffer, BUFFER_SIZE, "Car Attendant: Automobile parked. Available spaces now: %d\n", parkingLot->mFree_automobile);
            print(buffer);
            sem_post(automobileCounterControl);
            sem_post(inChargeforAutomobile);
        }
        else if (strcmp(vehicleType, "pickup") == 0)
        {
            sem_wait(newPickup);
            if (stopFlag)
                break;
            sem_wait(pickupCounterControl);
            parkingLot->mFree_pickup++;
            snprintf(buffer, BUFFER_SIZE, "Car Attendant: Pickup parked. Available spaces now: %d\n", parkingLot->mFree_pickup);
            print(buffer);
            sem_post(pickupCounterControl);
            sem_post(inChargeforPickup);
        }
        usleep(rand() % 500000);
    }
    usleep(rand() % 500000);

    return NULL;
}
