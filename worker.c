// Ahmed Ahmed
// Project 6
// 12/16/23


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>

#define MAX_MEMORY_REQUESTS 1000
#define READ 0
#define WRITE 1
#define MAX_CHILD_PROCESSES 18
#define MAX_PAGES_PER_PROCESS 32

struct Message {
    long msgType;     // Message type for IPC
    int processId;    // Process ID
    int action;       // Action: 0 for read, 1 for write
    int pageNumber;   // Page number being requested
};

struct SharedClock {
    unsigned int seconds;
    unsigned int nanoseconds;
};

int msgQueueId;
struct SharedClock *sharedClock; // Pointer to shared clock

void setupIPC() {
    key_t msgQueueKey = ftok(".", 'M');
    msgQueueId = msgget(msgQueueKey, 0666);
    if (msgQueueId == -1) {
        perror("Error getting message queue");
        exit(EXIT_FAILURE);
    }
}

void setupSharedMemory() {
    key_t key = ftok("shmfile", 65); // Ensure this key matches with oss.c
    int shmId = shmget(key, sizeof(struct SharedClock), 0666);
    if (shmId == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }
    sharedClock = (struct SharedClock*) shmat(shmId, NULL, 0);
    if (sharedClock == (void *) -1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }
}

void sendMessage(struct Message *msg) {
    if (msgsnd(msgQueueId, msg, sizeof(struct Message) - sizeof(long), 0) == -1) {
        perror("Error sending message");
        exit(EXIT_FAILURE);
    }
}

void receiveMessage(struct Message *msg, int msgType) {
    if (msgrcv(msgQueueId, msg, sizeof(struct Message) - sizeof(long), msgType, 0) == -1) {
        perror("Error receiving message");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: worker <process_index>\n");
        exit(EXIT_FAILURE);
    }

    int processId = atoi(argv[1]);
    if (processId < 0 || processId >= MAX_CHILD_PROCESSES) {
        fprintf(stderr, "Invalid process index.\n");
        exit(EXIT_FAILURE);
    }

    setupIPC();
    setupSharedMemory();

    struct Message msg;
    msg.msgType = 1; // Message type for requests

    for (int i = 0; i < MAX_MEMORY_REQUESTS; i++) {
        msg.processId = processId;
        msg.action = rand() % 2; // Randomly decide between read (0) and write (1)
        msg.pageNumber = rand() % MAX_PAGES_PER_PROCESS; 

        sendMessage(&msg);
        receiveMessage(&msg, processId); // Wait for response from the parent process

        usleep(rand() % 500000); // Simulate processing time
    }

    // Detach from shared memory if used
    if (shmdt(sharedClock) == -1) {
        perror("shmdt failed");
        exit(EXIT_FAILURE);
    }

    return 0;
}
