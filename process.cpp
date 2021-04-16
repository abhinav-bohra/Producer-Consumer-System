/****************************************************************************************************************************  
*				Systems Laboratory (CS39002) Spring Semester 2020-2021 					    *  
*							 								    *
*	Assignment 5: Implementation of multiple producer-consumer system where producers create prioritized jobs	    *
*	Part 1: Implement a producer / consumer set of processes using shared memory, with the given specifications         *  
*							 								    *
*				Group 31 | Animesh Jain 18C10004 & Abhinav Bohra 18CS30049 				    *  
*							 								    *
*****************************************************************************************************************************/

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <iostream>
using namespace std;

/* ---------------------------------------Glossary---------------------------------------

Main                    Main Function (forks multiple producers and consumers)
Producer                Producer Function (Creates and Inserts jobs)
Consumer	        Consumer Function (Removes jobs)
Queue	                Functions for Priority Queue implementation
Shared	                Functions related to semaphores and shared memory

-------------------------------------End of Glossary------------------------------------- */

typedef struct Job {
    int producer_pid;           // Producer process_id
    int producer_no;            // Producer Number
    int priority;               // Priority between 1 and 10
    int compute_time;           // Compute Time between 1 and 4
    int job_id;                 // Job ID between 1 and 100000
} JOB;


/* ---------------------------------------Queue--------------------------------------- */

#define QUEUE_SIZE 8

struct priority_queue {
    JOB job_queue[QUEUE_SIZE];       // Queue job_queueay
    int back;                        // Last index of Queue;
};

void init_queue(struct priority_queue *pq);
int isEmpty(struct priority_queue pq);
int isFull(struct priority_queue pq);
int enqueue(struct priority_queue *pq, JOB job);
JOB dequeue(struct priority_queue *pq);
void print_priority_queue(struct priority_queue pq);

void init_queue(struct priority_queue *pq) {
    pq->back = 0;
}

int isEmpty(struct priority_queue pq) {
    if(pq.back==0) return 1;
    return 0;
}

int isFull(struct priority_queue pq) {
    if(pq.back == QUEUE_SIZE) return 1;
    return 0;
}

int enqueue(struct priority_queue *pq, JOB job) {
    if(isFull(*pq)) return -1;

    int i=0;
    while(i < pq->back && (pq->job_queue[i]).priority < job.priority) i++;
    
    for(int j=pq->back-1; j>=i; j--) 
        pq->job_queue[j+1] = pq->job_queue[j];
        
    pq->job_queue[i] = job;
    (pq->back)++;
    return 0;
}

JOB dequeue(struct priority_queue *pq) {
    if(isEmpty(*pq)) {
        JOB job;
        job.job_id = -1;
        return job;
    }

    (pq->back)--;
    return pq->job_queue[pq->back];
}

/*void print_priority_queue(struct priority_queue pq) {
    int i=0;
    printf("\n");
    printf("pq ");
    while(i < pq.back) {
        printf("->(%d, %d, %d, %d)", pq.job_queue[i].producer_no, pq.job_queue[i].job_id, pq.job_queue[i].priority, pq.job_queue[i].compute_time);
        i++;
    }
    printf("\n\n");
}*/

/* -------------------------------------End of Queue------------------------------------- */

/* ---------------------------------------Shared--------------------------------------- */
#define EMPTY_ID 0
#define FULL_ID 1
#define MUTEX_ID 2
#define NSEM_SIZE 3
#define SHM_KEY 9
#define SEM_KEY "."

typedef struct SHMSegment {
    struct priority_queue pq; 	//priority queue of 8 elements
    int job_created;		//counter of number of jobs created
    int job_completed;		//counter of number of jobs completed
} SMT;

static struct sembuf downEmpty = { EMPTY_ID, -1, 0 };
static struct sembuf upEmpty = { EMPTY_ID, 1, 0 };
static struct sembuf upFull = { FULL_ID, 1, 0 };
static struct sembuf downFull = { FULL_ID, -1, 0 };
static struct sembuf downMutex = { MUTEX_ID, -1, 0 };
static struct sembuf upMutex = { MUTEX_ID, 1, 0 };

//Function to initialise Shared Memory Segment varaibles
int init_SHM(SMT* shmseg) {
    shmseg->job_completed = 0;
    shmseg->job_created = 0;
    init_queue(&shmseg->pq);
    return 0;
}

//Function to create shared memory segment. It returns a pointer to a shared memory buffer that the producer can write to.
SMT *create_SHM(int *shm_id) {

  key_t key = SHM_KEY; //use key to access a shared memory segment 
 
  int shmid = shmget(key, sizeof(SMT), IPC_CREAT | SHM_R | SHM_W); // give create, read and write access 
  if (errno > 0) {
    	printf("\x1b[31ERROR: Shared Memory Segment Creation Failed\x1b[0m\n"); 
    	exit (EXIT_FAILURE);
  }

  SMT *shmseg = (SMT*)shmat(shmid, NULL, 0);
  if (errno > 0) {
    	printf("\x1b[31ERROR: Shared Memory Segment Attachment Failed\x1b[0m\n"); 
        exit (EXIT_FAILURE);
  }

  *shm_id = shmid; // clear garbage memory in shared memory
  return shmseg;
}

//Function to create FULL and EMPTY semaphores 
int create_semaphore_set() {

  key_t key = ftok(SEM_KEY, 'E');
  int semid = semget(key, NSEM_SIZE, 0600 | IPC_CREAT);
  
if (errno > 0) {
    perror("Failed to create semaphore array");
    exit (EXIT_FAILURE);
  } 

  semctl(semid, FULL_ID, SETVAL, 0);
  if (errno > 0) {
    perror("Failed to set FULL semaphore");
    exit (EXIT_FAILURE);
  }

  semctl(semid, EMPTY_ID, SETVAL, QUEUE_SIZE);
  if (errno > 0) {
    perror("Failed to set EMPTY sempahore");
    exit (EXIT_FAILURE);
  }

  semctl(semid, MUTEX_ID, SETVAL, 1);
  if (errno > 0) {
    perror("Failed to create mutex");
  }

  return semid;
}

/* -------------------------------------End of Shared------------------------------------- */



/* ---------------------------------------Producer--------------------------------------- */

int insert_job(JOB job, SMT *shmseg) {
    //Add job to the sorted priority queue
    if( enqueue(&(shmseg->pq), job) == -1) {
    	//cout<<"QUEUE GOT FULL"<<endl;
	return -1; 
    }

    //Print Details
    printf("\033[0;32m+--------------------------+\033[0m\n");
    printf("\033[0;32m| New Job Created!         |\033[0m\n");
    printf("\033[0;32m+--------------+-----------+\033[0m\n");
    printf("\033[0;32m| Producer     | %8d  |\033[0m\n", job.producer_no);
    printf("\033[0;32m| Producer pid | %8d  |\033[0m\n", job.producer_pid);
    printf("\033[0;32m| Priority     | %8d  |\033[0m\n", job.priority);
    printf("\033[0;32m| Compute Time | %8d  |\033[0m\n", job.compute_time);
    printf("\033[0;32m+--------------+-----------+\033[0m\n\n");

    return 0;
}

JOB produce_job(int producer_no) {

    JOB job;
    job.producer_pid = getpid(); 	
    job.producer_no  = producer_no;
    job.priority     = 1 + rand() % 10; 	//Generate random number between 1 and 10
    job.compute_time = 1 + rand() % 4;	 	//Generate random number between 1 and 4
    job.job_id       = 1 + rand() % 100000;	//Generate random number between 1 and 100000

    return job;
}
 
/* Part 1 (c) -> Each producer process generates a computing job, waits for a random interval of time between 0 and 3 seconds, and inserts the
	         computing job in shared memory queue. After insertion, the producer will repeat the process.*/
int producer_main(int i, int NJ)
{   
    srand(time(NULL) ^ (getpid()<<16));  // Seed the random generator
    int shm_id;
    SMT *shmseg = create_SHM(&shm_id);
    int semid = create_semaphore_set();

    while(true) {
	//Step 1: Each producer process should generate a computing job, 
        JOB job = produce_job(i);
	
	//Step 2; Waits for a random interval of time between 0 and 3 seconds		
        int sleep_time = rand()%4;
        sleep(sleep_time);

        semop(semid, &downEmpty, 1);
        semop(semid, &downMutex, 1);
	
	//Step 3: Inserts the computing job in shared memory queue and increment count of number of jobs created				
        if(shmseg->job_created < NJ) {
            if( insert_job(job, shmseg) == -1) { //If queue if FUll, then wait
		semop(semid, &upMutex, 1);
        	semop(semid, &upEmpty, 1);		
		continue;
	    }
	    shmseg->job_created++;
            printf("\033[0;34m+--------------+-----------+\033[0m\n");    	
            printf("\033[0;34m| Job Created  | %8d  |\033[0m\n", shmseg->job_created);
            printf("\033[0;34m+--------------+-----------+\033[0m\n\n");
        }

        semop(semid, &upMutex, 1);
        semop(semid, &upFull, 1);

	//Step 4: After insertion, the producer will repeat the process. (Continues next iteration)  
	}
	exit(0); 
}
/* -------------------------------------End of Producer------------------------------------- */

/* ---------------------------------------Consumer--------------------------------------- */

JOB remove_job(int consumer_no, SMT* shmseg) {
    
    JOB job = dequeue(&(shmseg->pq)); //Remove job from priority queue

    if(job.job_id==-1) {
	//cout<<"QUEUE GOT EMPTY"<<endl;
	return job;
    }
    
    printf("\033[0;31m+--------------------------+\033[0m\n");
    printf("\033[0;31m| Job Removed From Queue   |\033[0m\n");
    printf("\033[0;31m+--------------+-----------+\033[0m\n");
    printf("\033[0;31m| Consumer     | %8d  |\033[0m\n", consumer_no);
    printf("\033[0;31m| Consumer pid | %8d  |\033[0m\n", getpid());
    printf("\033[0;31m| Producer     | %8d  |\033[0m\n", job.producer_no);
    printf("\033[0;31m| Producer pid | %8d  |\033[0m\n", job.producer_pid);
    printf("\033[0;31m| Priority     | %8d  |\033[0m\n", job.priority);
    printf("\033[0;31m| Compute Time | %8d  |\033[0m\n", job.compute_time);
    printf("\033[0;31m+--------------+-----------+\033[0m\n\n");
    return job;
}

/* Part 1 (e) -> Each consumer process waits for a random interval of time between 0 and 3 seconds, retrieves the job with highest priority in the shared memory
	         priority queue, removed the job and prints the job details on the screenmentioning the consumer number, consumer pid, producer number, producer pid,
		 priority, and compute time. Then the consumer will increase the job_completed counter and will sleep for "compute time" seconds. 
   		 If the priority queue is empty the consumer process will wait till a job is inserted in the buffer.*/

int consumer_main(int i, int NJ) {

    srand(time(NULL) ^ (getpid()<<16));  // Seed the random generator
    int shm_id;
    SMT *shmseg = create_SHM(&shm_id);
    int semid = create_semaphore_set();

    int item = 0;

    while(true) {
	//Step 1: Each consumer process waits for a random interval of time between 0 and 3 seconds			
        int wait_time = rand()%4;
	sleep(wait_time);

        //Step 2: Retrieves the job with highest priority in the shared memory priority queue and removes the Job				
        JOB job;
        int flag = 0;
	
	semop(semid, &downFull, 1);
        semop(semid, &downMutex, 1);
				
        if(shmseg->job_completed < NJ) {
            if(remove_job(i, shmseg).job_id ==-1) {  //If queue is empty, release lock and continue waiting
	 	semop(semid, &upMutex, 1);
        	semop(semid, &upEmpty, 1);		
		continue;
	    }
	    //Step 3: Consumer increases the job_completed counter			
            shmseg->job_completed++;
            printf("\033[0;34m+---------------+----------+\n");
            printf("| Job completed | %8d |\n", shmseg->job_completed);
            printf("+---------------+----------+\033[0m\n\n");
            flag = 1;
        }

        semop(semid, &upMutex, 1);
        semop(semid, &upEmpty, 1);

	//Step 4: Consumer sleeps for "compute time" seconds. 
        if(flag==1) sleep(job.compute_time);
    
    } 
}

/* -------------------------------------End of Consumer------------------------------------- */

/* -------------------------------------------Main------------------------------------------ */

int main() {

    int NP,NC,NJ;
    time_t start_t, end_t;
    double diff_t;
    time(&start_t);  //Recording start time to find total execution time
    
    /* Part 1 (a) -> Reading the values of NP (number of producers) and NC (number of consumers), and  also NJ (number of total jobs) to run as an input. */

    cout<<"Enter number of Producers: ";
    cin>>NP;
    cout<<"Enter number of Consumers: ";
    cin>>NC;
    cout<<"Enter number of Jobs: ";
    cin>>NJ;

    int all_pid[1024];
    int k_p = 0;
    pid_t child_pid, wpid;
    int status = 0;

    /* Part 1 (b) -> Create a shared memory segment SHM, which is shared among all the producer and consumer processes */

    int shm_id;
    SMT *shmseg = create_SHM(&shm_id);
    init_SHM(shmseg);
    int semid = create_semaphore_set();
    
    /* Part 1 (c) -> Each producer process should generates a computing job */

    for(int i=0; i<NP; i++) {
        int pid = fork();
        if(pid==0)  producer_main(i, NJ);
        else        all_pid[k_p++] = pid;
    }

    /* Part 1 (e) -> Each consumer retrieves the job with highest priority in the shared memory & removes it and sleep for "compute time" seconds. */

    for(int i=0; i<NC; i++) {
        int pid = fork();
        if(pid==0)  consumer_main(i, NJ);
        else        all_pid[k_p++] = pid;
    }

    /* Part 1 (f) -> The parent process will wait till both job_created counter and job_completed counter reaches a specified number of jobs*/

    union semun {int val; struct semid_ds *buf; unsigned short  *array;  } arg;
    
    //Parent Process
    while(true) {
        sleep(1);

        semop(semid, &downMutex, 1);

        if(shmseg->job_created == NJ && shmseg->job_completed == NJ) {

            for(int i=0; i<k_p; i++) {
                kill(all_pid[i], SIGTERM);   //Kill processes
            }

            time(&end_t);  				//Recording end time to find total execution time
            diff_t = difftime(end_t, start_t);		//Total execution time = end time - start time
            printf("Time taken to run %d jobs = %0.4fs\n", NJ, diff_t);

            semctl(semid, 0, IPC_RMID, arg);
            shmctl(shm_id, IPC_RMID, NULL);
            exit(0);
        }

        semop(semid, &upMutex, 1);
    }

    return 0;

}

/* -------------------------------------End of Main------------------------------------- */


