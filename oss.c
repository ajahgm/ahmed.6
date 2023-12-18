// Ahmed Ahmed
// Project 6
// 12/16/23


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#define PAGE_SIZE 1024
#define TOTAL_MEMORY 262144 // 256K
#define FRAME_SIZE PAGE_SIZE
#define NUM_FRAMES (TOTAL_MEMORY / FRAME_SIZE)
#define MAX_PAGES_PER_PROCESS 32
#define MAX_CHILD_PROCESSES 18
#define DISK_READ_TIME_MS 14
#define DISK_WRITE_TIME_MS 14

struct Frame {
    int inUse;
    int processId;
    int pageNumber;
    int referenceBit;
    int dirtyBit;
};

struct PageTableEntry {
    int valid;
    int frameNumber;
};

struct PageTable {
    struct PageTableEntry entries[MAX_PAGES_PER_PROCESS];
};

struct Message {
    long msgType;
    int processId;
    int action; // 0 for read, 1 for write
    int pageNumber;
};

struct SharedClock {
    unsigned int seconds;
    unsigned int nanoseconds;
};

struct Frame frameTable[NUM_FRAMES];
struct PageTable processPageTables[MAX_CHILD_PROCESSES];
int msgQueueId;
FILE *logFile;
int totalMemoryAccesses = 0;
int totalPageFaults = 0;
pid_t childPIDs[MAX_CHILD_PROCESSES];
int numChildProcesses = 0;
int shmId;
struct SharedClock *sharedClock;
int initialChildProcessCount = 5; // Adjust as necessary

void initializeMemoryManagement() {
    for (int i = 0; i < NUM_FRAMES; i++) {
        frameTable[i].inUse = 0;
        frameTable[i].processId = -1;
        frameTable[i].pageNumber = -1;
        frameTable[i].referenceBit = 0;
        frameTable[i].dirtyBit = 0;
    }

    for (int i = 0; i < MAX_CHILD_PROCESSES; i++) {
        for (int j = 0; j < MAX_PAGES_PER_PROCESS; j++) {
            processPageTables[i].entries[j].valid = 0;
            processPageTables[i].entries[j].frameNumber = -1;
        }
    }
}

void initializeLogging(const char *filename) {
    logFile = fopen(filename, "w");
    if (logFile == NULL) {
        perror("Error opening log file");
        exit(EXIT_FAILURE);
    }
}

void logEvent(const char *message) {
    fprintf(logFile, "%s\n", message);
    fflush(logFile);
}

int findFrameForReplacement() {
    static int clockHand = 0;
    while (1) {
        if (frameTable[clockHand].inUse) {
            if (frameTable[clockHand].referenceBit == 0) {
                int frameToReplace = clockHand;
                clockHand = (clockHand + 1) % NUM_FRAMES;
                return frameToReplace;
            } else {
                frameTable[clockHand].referenceBit = 0;
            }
        }
        clockHand = (clockHand + 1) % NUM_FRAMES;
    }
}


void handlePageFault(int processId, int pageNumber) {
    totalPageFaults++;
    int frameToReplace = findFrameForReplacement();

    if (frameTable[frameToReplace].dirtyBit) {
        printf("Writing back dirty page of Frame %d\n", frameToReplace);
        usleep(DISK_WRITE_TIME_MS * 1000);
    }

    printf("Reading page into Frame %d\n", frameToReplace);
    usleep(DISK_READ_TIME_MS * 1000);
    
    frameTable[frameToReplace].inUse = 1;
    frameTable[frameToReplace].processId = processId;
    frameTable[frameToReplace].pageNumber = pageNumber;
    frameTable[frameToReplace].dirtyBit = 0;

    processPageTables[processId].entries[pageNumber].valid = 1;
    processPageTables[processId].entries[pageNumber].frameNumber = frameToReplace;

    char logMsg[100];
    sprintf(logMsg, "Page Fault: Process %d for Page %d", processId, pageNumber);
    logEvent(logMsg);
}

void handleMemoryRequest(struct Message *requestMessage) {
    int processId = requestMessage->processId;
    int pageNumber = requestMessage->pageNumber;

    // Validate the processId and pageNumber before accessing the array
    if (processId < 0 || processId >= MAX_CHILD_PROCESSES || pageNumber < 0 || pageNumber >= MAX_PAGES_PER_PROCESS) {
        fprintf(stderr, "Error: Invalid processId or pageNumber\n");
        return; // Exit the function if invalid
    }

    totalMemoryAccesses++;

    if (!processPageTables[processId].entries[pageNumber].valid) {
        handlePageFault(processId, pageNumber);
    } else {
        int frameNumber = processPageTables[processId].entries[pageNumber].frameNumber;
        frameTable[frameNumber].referenceBit = 1;
        char logMsg[100];
        sprintf(logMsg, "Memory Access: Process %d accessed Page %d in Frame %d", processId, pageNumber, frameNumber);
        logEvent(logMsg);
    }
}



void handleWriteOperation(int processId, int pageNumber) {
    if (processPageTables[processId].entries[pageNumber].valid) {
        int frameNumber = processPageTables[processId].entries[pageNumber].frameNumber;
        frameTable[frameNumber].dirtyBit = 1;
        char logMsg[100];
        sprintf(logMsg, "Write Operation: Process %d wrote to Page %d", processId, pageNumber);
        logEvent(logMsg);
    } else {
        handlePageFault(processId, pageNumber);
    }
}

void logStatistics() {
    if (totalMemoryAccesses > 0) {
        float faultsPerAccess = (float)totalPageFaults / totalMemoryAccesses;
        char statsMsg[100];
        sprintf(statsMsg, "Statistics: Memory Accesses: %d, Page Faults: %d, Faults per Access: %.2f", totalMemoryAccesses, totalPageFaults, faultsPerAccess);
        logEvent(statsMsg);
    }
}

void cleanupLogging() {
    if (logFile != NULL) {
        fclose(logFile);
    }
}

void setupIPC() {
    key_t msgQueueKey = ftok(".", 'M');
    msgQueueId = msgget(msgQueueKey, IPC_CREAT | 0666);
    if (msgQueueId == -1) {
        perror("Error creating message queue");
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

void cleanupIPC() {
    if (msgctl(msgQueueId, IPC_RMID, NULL) == -1) {
        perror("Error removing message queue");
        exit(EXIT_FAILURE);
    }
}

void setupSharedMemory() {
    key_t key = ftok("shmfile", 65);
    shmId = shmget(key, sizeof(struct SharedClock), 0666|IPC_CREAT);
    if (shmId == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }

    sharedClock = (struct SharedClock*) shmat(shmId, NULL, 0);
    if (sharedClock == (void *) -1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }

    sharedClock->seconds = 0;
    sharedClock->nanoseconds = 0;
}

void advanceClock(unsigned int sec, unsigned int nanosec) {
    sharedClock->nanoseconds += nanosec;
    sharedClock->seconds += sec + sharedClock->nanoseconds / 1000000000;
    sharedClock->nanoseconds %= 1000000000;
}

void detachSharedMemory() {
    if (shmdt(sharedClock) == -1) {
        perror("shmdt failed");
        exit(EXIT_FAILURE);
    }
}

void cleanupSharedMemory() {
    if (shmctl(shmId, IPC_RMID, NULL) == -1) {
        perror("shmctl failed");
        exit(EXIT_FAILURE);
    }
}


void createChildProcess(int index) {
    if (numChildProcesses >= MAX_CHILD_PROCESSES) {
        printf("Maximum number of child processes reached.\n");
        return;
    }

    pid_t pid = fork();

    if (pid == -1) {
        perror("Error creating child process");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        char indexStr[10];
        sprintf(indexStr, "%d", index);
        execl("./worker", "worker", indexStr, (char *) NULL);
        perror("execl failed");
        exit(EXIT_FAILURE);
    } else {
        printf("Parent: Created child process with PID: %d\n", pid);
        childPIDs[numChildProcesses++] = pid;
    }
}


void terminateChildProcess(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGTERM);
        printf("Terminated child process with PID: %d\n", pid);
    } else {
        for (int i = 0; i < numChildProcesses; i++) {
            if (childPIDs[i] > 0) {
                kill(childPIDs[i], SIGTERM);
                printf("Terminated child process with PID: %d\n", childPIDs[i]);
            }
        }
    }
}

void signalHandler(int sig) {
    printf("Received signal %d, terminating child processes\n", sig);
    terminateChildProcess(-1); // -1 indicates terminating all child processes
    cleanupIPC();
    cleanupSharedMemory();
    cleanupLogging();
    exit(0);
}


void handleChildTermination(pid_t pid) {
    for (int i = 0; i < MAX_CHILD_PROCESSES; i++) {
        if (childPIDs[i] == pid) {
            childPIDs[i] = -1;
            for (int j = 0; j < MAX_PAGES_PER_PROCESS; j++) {
                processPageTables[i].entries[j].valid = 0;
                processPageTables[i].entries[j].frameNumber = -1;
            }
            numChildProcesses--;
            break;
        }
    }
    printf("Resources for child process %d freed.\n", pid);
}

int isPageFault(struct Message msg) {
    return !processPageTables[msg.processId].entries[msg.pageNumber].valid;
}

void setPageFaultEvent(struct Message *msg) {
    printf("Page fault event set for Process ID: %d, Page Number: %d\n", msg->processId, msg->pageNumber);
    // Logic to queue the page fault event
  
}

void logTables() {
    fprintf(logFile, "Current Memory State:\n");
    // Log the frame table
    for (int i = 0; i < NUM_FRAMES; i++) {
        fprintf(logFile, "Frame %d: In Use: %d, PID: %d, Page: %d, Ref: %d, Dirty: %d\n",
                i, frameTable[i].inUse, frameTable[i].processId, frameTable[i].pageNumber,
                frameTable[i].referenceBit, frameTable[i].dirtyBit);
    }
    // Log the page table for each process
    for (int i = 0; i < MAX_CHILD_PROCESSES; i++) {
        fprintf(logFile, "Page Table for Process %d:\n", i);
        for (int j = 0; j < MAX_PAGES_PER_PROCESS; j++) {
            fprintf(logFile, "Page %d: Valid: %d, Frame: %d\n",
                    j, processPageTables[i].entries[j].valid, processPageTables[i].entries[j].frameNumber);
        }
    }
    fflush(logFile);
}

void mainSimulationLoop() {
    int running = 1;
    int processCount = 0;
    int childrenToLaunch = initialChildProcessCount;
    time_t startTime = time(NULL);
    time_t lastLogTime = time(NULL);

    while (running) {
        pid_t endedPid;
        int status;

        // Non-blocking waitpid to check for terminated child processes
        while ((endedPid = waitpid(-1, &status, WNOHANG)) > 0) {
            handleChildTermination(endedPid);
        }

        // Check if we should launch a new child process
        if (numChildProcesses < MAX_CHILD_PROCESSES && childrenToLaunch > 0) {
            createChildProcess(numChildProcesses);
            childrenToLaunch--;
        }

        struct Message msg;

        // Check for a message from a child process
        if (msgrcv(msgQueueId, &msg, sizeof(struct Message) - sizeof(long), 1, IPC_NOWAIT) != -1) {
            if (isPageFault(msg)) {
                setPageFaultEvent(&msg);
            } else {
                handleMemoryRequest(&msg);
                processCount++;
            }

            // Update the system clock
            advanceClock(0, 100000);
        }

        // Log tables every half a second
        if (difftime(time(NULL), lastLogTime) >= 0.5) {
            logTables();
            lastLogTime = time(NULL);
        }

        // Termination logic
        if (processCount > 100 || difftime(time(NULL), startTime) > 5) {
            running = 0;
        }
    }
}

void startSimulation() {
    for (int i = 0; i < initialChildProcessCount; i++) {
        createChildProcess(i);
    }
    mainSimulationLoop();

    // Terminate all child processes after exiting the loop
    terminateChildProcess(-1);
}

int main() {
    signal(SIGINT, signalHandler);

    initializeLogging("memory_management.log");
    setupIPC();
    setupSharedMemory();
    initializeMemoryManagement();

    startSimulation();

    // Cleanup
    cleanupIPC();
    cleanupSharedMemory();
    cleanupLogging();

    return 0;
}