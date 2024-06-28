#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROCESSES 10000
#define MAX_INSTRUCTIONS 10000

typedef struct {
    char type[4]; // "exe" or "io"
    int duration;
    int decoded;
} Instruction;

typedef struct {
    int pid;
    int priority;
    int arrival_t;
    Instruction instructions[MAX_INSTRUCTIONS];
    int num_instructions;
    int current_instruction;
    int ready_time;
    int complete_time;
    char queue;
    int successive_interupt_count;
    int quantum;
} Process;

typedef struct {
    Process **processes;
    int size;
} Queue;

typedef struct {
    int occupied;
    Instruction running_instruction;
    Process *running_process;
} CPU;

typedef struct {
    int running_processes_count;
    Process *running_processes[MAX_PROCESSES];
} IO;

Queue *createQueue();
void enqueue(Queue *q, Process *p);
Process *dequeue(Queue *q);
int isEmpty(Queue *q);
void destroyQueue(Queue *q);
void parseInputFile(char *filename);
void printProcesses();
void printQueue(Queue *q);
void executeProcess();
void promote(Process *process);
void printStatistics();
void decode(Process *process);
void runIO();
void updateReadyTime();

// global vars
Queue *queueA;
Queue *queueB;
int current_time = 0;
Process *processes[MAX_PROCESSES];
int num_processes = 0;
int quantum_A;
int quantum_B;
CPU *cpu;
IO *io;
int start_time = -1;
int total_ready_time = 0;

/**
 * Simulate - Executes a simulation of process scheduling.
 *
 * This function simulates a process scheduling algorithm using two queues (queueA and queueB),
 * a CPU, and an IO system. It schedules processes based on their arrival times, priorities,
 * and specified quantum times for the queues. The simulation runs in a time-driven manner,
 * incrementing the current time in each iteration of the main loop.
 *
 * The simulation handles both preemptive and non-preemptive scheduling based on the
 * 'preEmp' parameter. It also manages the promotion of processes if they are interrupted
 * successively multiple times.
 *
 * @param quantumA The time quantum for processes in queueA.
 * @param quantumB The time quantum for processes in queueB.
 * @param preEmp A flag indicating if preemptive scheduling is enabled (1 for preemptive, 0 for non-preemptive).
 */
void Simulate(int quantumA, int quantumB, int preEmp) {
    quantum_A = quantumA;
    quantum_B = quantumB;
    queueA = createQueue();
    queueB = createQueue();
    cpu = malloc(sizeof(CPU));
    io = malloc(sizeof(IO));
    cpu->occupied = 0;
    io->running_processes_count = 0;

    while (1) {
        // Check for process arrivals and enqueue them to Queue B
        for (int i = 0; i < num_processes; i++) {
            Process *process = processes[i];
            if (process->arrival_t == current_time) {
                enqueue(queueB, process);
                if (start_time == -1) {
                    start_time = current_time;
                }
            }
        }
        // preemptive: if running process is lower prio than the waiting process
        if (preEmp && (
            (!isEmpty(queueB) &&
                cpu->occupied &&
                cpu->running_process->queue == 'B' &&
                cpu->running_process->priority < queueB->processes[0]->priority) ||
            (!isEmpty(queueA) &&
                cpu->occupied &&
                cpu->running_process->queue == 'B')
            )) {
            // update running process's successive_interupt_count and move it out of the cpu,
            // potentially promote it if successive_interupt_count >= 3, else put it back to the q.
            // dequeue the higher prio process and put stick it into the cpu to decode.

            Process *process = cpu->running_process;
            cpu->occupied = 0;

            if (process->successive_interupt_count + 1 >= 3) { // promote
                promote(process);
            } else {
                enqueue(queueB, process);
            }

            if (!isEmpty(queueA)) {
                decode(dequeue(queueA));
                updateReadyTime();
            } else if (!isEmpty(queueB)) {
                decode(dequeue(queueB));
                updateReadyTime();
            }
        } else {
            // non-preemptive: if the CPU is free, dispatch a process into it
            if (cpu->occupied == 0) {
                if (!isEmpty(queueA)) {
                    Process *process = dequeue(queueA);
                    decode(process);
                    updateReadyTime();
                } else if (!isEmpty(queueB)) {
                    Process *process = dequeue(queueB);
                    decode(process);
                    updateReadyTime();
                }
            } else {
                updateReadyTime();
                executeProcess();
            }
        }

        // update IO parallel to the timeline
        runIO();
        
        // increment current time
        current_time++;

        // check for all completion
        int all_processes_completed = 1;
        for (int i = 0; i < num_processes; i++) {
            if (processes[i]->current_instruction < processes[i]->num_instructions) {
                all_processes_completed = 0;
                break;
            }
        }

        // complete the run when all processes have run
        if (all_processes_completed == 1 &&
            isEmpty(queueA) &&
            isEmpty(queueB)) {
            break;
        }
    }
    printStatistics();
    free(cpu);
    free(io);
    destroyQueue(queueA);
    destroyQueue(queueB);
    for (int i = 0; i < num_processes; i++) {
        free(processes[i]);
    }
}

/**
 * decode - Decodes and dispatches a process to the CPU or IO system.
 *
 * This function handles the decoding of a process's current instruction and
 * dispatches the process either to the CPU for execution or to the IO system
 * if the current instruction is an IO operation. It also handles the termination
 * of the process if all instructions are completed.
 *
 * @param process - A pointer to the Process structure that is to be decoded and dispatched.
 */
void decode(Process *process) {
    int inst = process->current_instruction;
    int quantum = process->quantum;

    // put the process in the CPU
    cpu->occupied = 1;
    cpu->running_instruction = process->instructions[inst];
    cpu->running_process = process;

    // if current instruction index equals/greater than number of instructions excluding terminal
    if (inst >= process->num_instructions) { // terminate
        cpu->occupied = 0;
        process->complete_time = current_time + 1;
    } else {
        int i = process->current_instruction;
        // if the current instruction is io, move it from cpu to io
        // purposefully leave the instruction as undecoded to skip the update in runIO first time
        if (strcmp(process->instructions[i].type, "io") == 0) {
            if (quantum > 0) {
                process->successive_interupt_count++;
            }
            io->running_processes[io->running_processes_count++] = process;
            cpu->occupied = 0;
        } else { // if exe, then decrement quantum and mark as decoded
            cpu->running_process->quantum--;
            process->instructions[inst].decoded = 1;
        }
    }
}

/**
 * runIO - Executes and manages IO operations for processes in the IO system.
 *
 * This function handles the execution of IO operations for processes that are currently
 * in the IO system. It updates the duration of each IO instruction, checks for completion
 * of IO operations, and moves completed processes back to their respective queues.\
 */
void runIO() {
    // loop thru all processes in the io
    for (int i = 0; i < io->running_processes_count; i++) {
        Process *p = io->running_processes[i];

        // if the current io instruction is decoding, mark it as decoded and move on to next io instruction
        if (p->instructions[p->current_instruction].decoded == 0) {
            p->instructions[p->current_instruction].decoded = 1;
            continue;
        }

        p->instructions[p->current_instruction].duration--;

        // if the current io instruction's time is up
        if (p->instructions[p->current_instruction].duration <= 0) {
            // remove the current io instruction from the io, and put it back into the queue
            for (int j = i; j < io->running_processes_count; j++) {
                io->running_processes[j] = io->running_processes[j + 1];
            }
            io->running_processes_count--;
            i--;

            if (p->successive_interupt_count >= 3 && p->queue == 'B') {
                promote(p);
            } else {
                if (p->queue == 'A') {
                    enqueue(queueA, p);
                } else {
                    enqueue(queueB, p);
                }
            }

            p->current_instruction++;
        }
    }
}

/**
 * executeProcess - Executes the current instruction of the process running on the CPU.
 *
 * This function manages the execution of the current instruction for the process
 * that is currently running on the CPU. It decrements the duration of the instruction
 * and the process's quantum, checks if the instruction is complete or if the quantum
 * has expired, and handles process promotion, completion, and re-queuing as necessary.
 */
void executeProcess() {
    int org_quantum;
    Process *p = cpu->running_process;
    Queue *q;
    p->instructions[p->current_instruction].duration--;
    p->quantum--;

    if (cpu->running_process->queue == 'A') {
        q = queueA;
        org_quantum = quantum_A;
    } else {
        q = queueB;
        org_quantum = quantum_B;
    }

    // if the instruction's duration is 0
    if (p->instructions[p->current_instruction].duration <= 0) {
        if (p->quantum > 0) {
            p->successive_interupt_count++;
        } else if (p->quantum == 0) {
            p->successive_interupt_count = 0;
        }

        if (p->successive_interupt_count >= 3 && q == queueB) {
            promote(p);
        }
        // evict the process from the cpu, update to the next instruction in the process
        cpu->occupied = 0;
        p->current_instruction++;

        if (p->successive_interupt_count >= 3 && q == queueB) {
            promote(p);
        } else {
            p->quantum = org_quantum;
            enqueue(q, p);
        }
    } else if (p->quantum <= 0) { // else if used up all quantum
        // clear the cpu, instruction becomes undecoded, process is enqueued back into the queue
        cpu->occupied = 0;
        p->successive_interupt_count = 0;
        p->instructions[p->current_instruction].decoded = 0;
        p->quantum = org_quantum;
        enqueue(q, p);
    } 
}

/**
 * promote - Promotes a process from Queue B to Queue A.
 *
 * This function handles the promotion of a process from Queue B to Queue A.
 * When a process is promoted, its queue attribute is updated to 'A', its successive
 * interrupt count is reset to 0, and its quantum is set to the quantum for Queue A.
 * The promoted process is then enqueued into Queue A.
 *
 * @param process A pointer to the Process structure representing the process to be promoted.
 */
void promote(Process *process) {
    process->queue = 'A';
    process->successive_interupt_count = 0;
    process->quantum = quantum_A;
    enqueue(queueA, process);
}

/**
 * updateReadyTime - Updates the ready time for all processes in Queue A and Queue B.
 *
 * This function increments the ready time for all processes currently in Queue A and Queue B.
 * The total ready time is also incremented for each process in both queues.
 * This helps in tracking how long each process has been waiting in their respective queues.
 */
void updateReadyTime() {
    for (int i = 0; i < queueA->size; i++) {
        total_ready_time++;
        queueA->processes[i]->ready_time++;
    }
    for (int i = 0; i < queueB->size; i++) {
        total_ready_time++;
        queueB->processes[i]->ready_time++;
    }
}

/**
 * printStatistics - Calculates and prints statistics for the simulation.
 *
 * This function calculates various statistics including the maximum and minimum ready times,
 * the number of completed processes, and the total number of instructions completed.
 * It then prints these statistics along with the start and end times of the simulation,
 * and detailed information for each process, such as completion time, waiting time, and the queue from which it terminated.
 */
void printStatistics() {
    // calculate statistics
    int max_ready_time = 0;
    int min_ready_time = 1000; // Assuming a large initial value
    int completed_processes = 0;
    int total_instructions = 0;
    for (int i = 0; i < num_processes; i++) {
        Process *process = processes[i];
        
        if (process->ready_time > max_ready_time) {
            max_ready_time = process->ready_time;
        }
        if (process->ready_time < min_ready_time) {
            min_ready_time = process->ready_time;
        }
        total_instructions += process->num_instructions + 1;
        completed_processes++;
    }

    // print statistics
    printf("Start/end time: %d, %d\n", start_time, current_time);
    printf("Processes completed: %d\n", completed_processes);
    printf("Instructions completed: %d\n", total_instructions);
    if (total_ready_time % num_processes == 0) {
        printf("Ready time average: %d\n", total_ready_time / num_processes);
    } else {
        printf("Ready time average: %.1f\n", (double)total_ready_time / num_processes);
    }
    printf("Ready time maximum: %d\n", max_ready_time);
    printf("Ready time minimum: %d\n", min_ready_time);

    for (int i = 0; i < num_processes; i++) {
        Process *process = processes[i];
        printf("P%d time_completion: %d time_waiting: %d termination_queue: %c\n", process->pid, process->complete_time, process->ready_time, process->queue);
    }
}

/**
 * parseInputFile - Parses the input file to initialize processes.
 *
 * This function reads process data from a specified input file and initializes
 * the processes array with the parsed data. Each process includes attributes such as
 * process ID, priority, arrival time, and instructions. The instructions can be either
 * execution or I/O operations with specified durations. The function also handles 
 * initialization of process-specific attributes like ready time, complete time, and
 * queue placement.
 * 
 * @param filename The name of the file containing process data.
 */

void parseInputFile(char *filename) {
    // get file
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Error opening file\n");
        exit(1);
    }   

    // parse
    char line[100];
    while (fgets(line, sizeof(line), file)) {
        Process *process = malloc(sizeof(Process));
        process->num_instructions = 0;
        process->current_instruction = 0;
        process->ready_time = 0;
        process->complete_time = -1;
        process->queue = 'B'; // start in B
        process->successive_interupt_count = 0;
        process->quantum = quantum_B;

        sscanf(line, "P%d:%d", &process->pid, &process->priority);

        fgets(line, sizeof(line), file);
        sscanf(line, "arrival_t:%d", &process->arrival_t);

        while (fgets(line, sizeof(line), file) && 
                strcmp(line, "terminate\n") != 0 && 
                strcmp(line, "terminate") != 0) {
            
            char type[4];
            int duration;

            if (strstr(line, "exe:") != NULL) {
                sscanf(line, "%3s:%d\n", type, &duration);
            } else {
                sscanf(line, "%2s:%d\n", type, &duration);
            }

            int i = process->num_instructions;
            strcpy(process->instructions[i].type, type);
            process->instructions[i].duration = duration;
            process->num_instructions++;
        }

        processes[num_processes++] = process;
    }

    fclose(file);
}

/**
 * createQueue - Creates and initializes a new queue.
 *
 * This function allocates memory for a new Queue structure and its internal 
 * processes array. It sets the initial size of the queue to 0.
 *
 * Return: A pointer to the newly created Queue.
 */
Queue *createQueue() {
    Queue *q = malloc(sizeof(Queue));
    q->processes = malloc(sizeof(Process *) * MAX_PROCESSES);
    q->size = 0;
    return q;
}

/**
 * enqueue - Adds a process to the appropriate queue.
 * 
 * This function enqueues a process into either queueA or queueB. For queueA, 
 * processes are simply added at the end. For queueB, processes are inserted 
 * based on their priority, maintaining a sorted order.
 * 
 * @param q The queue to which the process will be added.
 * @param p The process to be added to the queue.
 */
void enqueue(Queue *q, Process *p) {
    if (q == queueA) {
        q->processes[q->size] = p;
        p->quantum = quantum_A;
    } else if (q == queueB) {
        int i = q->size - 1; // least prioritized process
        while (i >= 0 && q->processes[i]->priority < p->priority) {
            q->processes[i + 1] = q->processes[i];
            i--;
        }
        
        q->processes[i + 1] = p;
        p->quantum = quantum_B;
    }
    q->size++;
}

/**
 * dequeue - Removes and returns the front process from the queue.
 * 
 * This function removes the process at the front of the queue and returns a 
 * pointer to it. It shifts all remaining processes in the queue one position forward.
 *
 * @param q The queue from which the process will be removed.
 * 
 * Return: A pointer to the process removed from the queue.
 */
Process *dequeue(Queue *q) {
    Process *p = q->processes[0];
    for (int i = 0; i < q->size - 1; i++) {
        q->processes[i] = q->processes[i + 1];
    }
    q->size--;

    return p;
}

/**
 * isEmpty - Checks if the queue is empty.
 *
 * This function returns a non-zero value if the queue is empty, otherwise it 
 * returns 0.
 * 
 * @param q The queue to be checked.
 *
 * Return: 1 if the queue is empty, 0 otherwise.
 */
int isEmpty(Queue *q) {
    return q->size == 0;
}

/**
 * destroyQueue - Frees the memory allocated for the queue.
 *
 * This function frees the memory allocated for the processes array and the 
 * queue structure itself.
 * 
 * @param q The queue to be destroyed.
 */
void destroyQueue(Queue *q) {
    free(q->processes);
    free(q);
}

int main(int argc, char **argv) {
    if (argc != 5) {
      printf("Incorrect number of arguments\n");
      return -1;
    }

    char* fileName = argv[1];
    int quantumA = atoi(argv[2]);
    int quantumB = atoi(argv[3]);
    int preemption = atoi(argv[4]);
    quantum_A = quantumA;
    quantum_B = quantumB;

    parseInputFile(fileName);

    // Run simulation
    Simulate(quantumA, quantumB, preemption);
    
    return 0;
}