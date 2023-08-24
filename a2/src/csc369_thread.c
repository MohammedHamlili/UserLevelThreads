#include "csc369_thread.h"

#include <ucontext.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/time.h>

// TODO: You may find this useful, otherwise remove it
//#define DEBUG_USE_VALGRIND // uncomment to debug with valgrind
#ifdef DEBUG_USE_VALGRIND
#include <valgrind/valgrind.h>
#endif

#include "csc369_interrupts.h"

//****************************************************************************
// Private Definitions
//****************************************************************************
// TODO: You may find this useful, otherwise remove it
typedef enum
{
  CSC369_THREAD_FREE = 0,
  CSC369_THREAD_READY = 1,
  CSC369_THREAD_RUNNING = 2,
  CSC369_THREAD_ZOMBIE = 3,
  CSC369_THREAD_BLOCKED = 4
} CSC369_ThreadState;

/**
 * The Thread Control Block.
 */
typedef struct
{
  // TODO: Populate this struct with other things you need
  Tid id;
  CSC369_ThreadState state;
  void* stack_address;

  /**
   * The thread context.
   */
  ucontext_t context; // TODO: You may find this useful, otherwise remove it

  /**
   * What code the thread exited with.
   */
  int exit_code; // TODO: You may find this useful, otherwise remove it

  /**
  * Exit code collected from a thread we were waiting on.
  */
  int code_collected;

  /**
   * The queue of threads that are waiting on this thread to finish.
   */
  CSC369_WaitQueue* join_threads; // TODO: You may find this useful, otherwise remove it
} TCB;

typedef struct node
{
  TCB* thread;
  struct node* next;
} node;

/**
 * A wait queue.
 */
typedef struct csc369_wait_queue_t
{
  // TCB* head;
  node* head;
} CSC369_WaitQueue;

//**************************************************************************************************
// Private Global Variables (Library State)
//**************************************************************************************************
/**
 * All possible threads have their control blocks stored contiguously in memory.
 */
TCB threads[CSC369_MAX_THREADS]; // TODO: you may find this useful, otherwise remove it

/**
 * Threads that are ready to run in FIFO order.
 */
CSC369_WaitQueue ready_threads; // TODO: you may find this useful, otherwise remove it

/**
 * Threads that need to be cleaned up.
 */
CSC369_WaitQueue zombie_threads; // TODO: you may find this useful, otherwise remove it

/* 
 * Variable pointing to the current running thread. 
*/
TCB* current_thread; 

//**************************************************************************************************
// Helper Functions
//**************************************************************************************************
void // TODO: You may find this useful, otherwise remove it
Queue_Init(CSC369_WaitQueue* queue)
{
  int const prev_state = CSC369_InterruptsDisable();
  // queue = malloc(sizeof(CSC369_WaitQueue));
  queue->head = NULL;
  CSC369_InterruptsSet(prev_state);
}

// Return 1 if queue is empty. 0 otherwise.
int // TODO: You may find this useful, otherwise remove it
Queue_IsEmpty(CSC369_WaitQueue* queue)
{
  int const prev_state = CSC369_InterruptsDisable();
  if (queue->head == NULL) {
    CSC369_InterruptsSet(prev_state);
    return 1;
  }
  CSC369_InterruptsSet(prev_state);
  return 0; // FIXME
}

void // TODO: You may find this useful, otherwise remove it
Queue_Enqueue(CSC369_WaitQueue* queue, TCB* tcb)
{
  int const prev_state = CSC369_InterruptsDisable();
  node* newNode = (node*) malloc(sizeof(node));
  assert(newNode);
  newNode->thread = tcb;
  newNode->next = NULL; 
  if (Queue_IsEmpty(queue)) {
    queue->head = newNode;
  } else {
    node* temp = queue->head;
    while (temp->next != NULL) {
      temp = temp->next;
    }
    temp->next = newNode;
  }
  CSC369_InterruptsSet(prev_state);
}

TCB* // TODO: You may find this useful, otherwise remove it
Queue_Dequeue(CSC369_WaitQueue* queue)
{
  int const prev_state = CSC369_InterruptsDisable();
  if (Queue_IsEmpty(queue)) {
    // printf("Queue is Empty. Function: Queue_Dequeue\n");
    CSC369_InterruptsSet(prev_state);
    return NULL;
  } else {
    node* nodeToRun = queue->head;
    // assert(nodeToRun->thread->state == CSC369_THREAD_READY);
    if (nodeToRun->next == NULL) {
      queue->head = NULL;
    } else {
      queue->head = nodeToRun->next;
      nodeToRun->next = NULL;
    }
    TCB* result = nodeToRun->thread;
    free(nodeToRun);
    CSC369_InterruptsSet(prev_state);
    return result;
  }
}

TCB* // TODO: You may find this useful, otherwise remove it
Queue_Remove(CSC369_WaitQueue* queue, Tid id)
{
  int const prev_state = CSC369_InterruptsDisable();
  if (Queue_IsEmpty(queue)) {
    // printf("Queue is Empty. Function: Queue_Remove.\n");
    CSC369_InterruptsSet(prev_state);
    return NULL;
  } else {
    node* temp = queue->head;
    node* prev = NULL;
    while (temp != NULL && temp->thread->id != id) {
      prev = temp;
      temp = temp->next;
    }
    if (temp == NULL) {
      // printf("The thread with the specified id is non existent in queue of Ready Queues.\n");
      CSC369_InterruptsSet(prev_state);
      return NULL;
    } else if (prev == NULL) { // Case: Specified thread is the head of the queue.
      assert(queue->head->thread->id == id);
      // assert(queue->head->thread->state == CSC369_THREAD_READY);
      queue->head = temp->next; 
      TCB* result = temp->thread;
      free(temp);
      CSC369_InterruptsSet(prev_state);
      return result;
    } else {
      prev->next = temp->next;
      temp->next = NULL;
      assert(temp->thread->state == CSC369_THREAD_READY);
      TCB* result = temp->thread;
      free(temp);
      CSC369_InterruptsSet(prev_state);
      return result;
    }
  }
  // FIXME
}

void
clear_Zombies() 
{
  int const prev_state = CSC369_InterruptsDisable();
  for (int i = 0; i < CSC369_MAX_THREADS; i++) {
    if (threads[i].state == CSC369_THREAD_ZOMBIE) {
      threads[i].state = CSC369_THREAD_FREE;
      if (i != 0) free(threads[i].stack_address);
      // COME BACK: FREE THE WAIT QUEUE (TASK 2)
      // CSC369_ThreadWakeAll(threads[i].join_threads);
      CSC369_WaitQueueDestroy(threads[i].join_threads);
    }
  }
  CSC369_InterruptsSet(prev_state);
}

// void // TODO: You may find this useful, otherwise remove it
// TCB_Init(TCB* tcb, Tid thread_id)
// {
//   // FIXME
// }

// void // TODO: You may find this useful, otherwise remove it
// TCB_Free(TCB* tcb)
// {
//   // FIXME
// #ifdef DEBUG_USE_VALGRIND
//   // VALGRIND_STACK_DEREGISTER(...);
// #endif
// }

/* Thread Stub function*/
void
MyThreadStub(void (*f)(void*), void *arg) {
  if (current_thread->state == CSC369_THREAD_READY) {
    // printf("State is Ready. Changing it to Running.\n");
    current_thread->state = CSC369_THREAD_RUNNING;
    CSC369_InterruptsEnable();
    f(arg);
    CSC369_ThreadExit(current_thread->exit_code); // COME BACK TO CHANGE EXIT CODE (TASK 2)
  }
  else {
    // printf("State is incorrect. %d\n", (int) current_thread->state);
    exit(0);
  }
}

// TODO: You may find it useful to create a helper function to create a context
// TODO: You may find it useful to create a helper function to switch contexts

//**************************************************************************************************
// thread.h Functions
//**************************************************************************************************
int
CSC369_ThreadInit(void)
{
  int i = 0;
  int const prev_state = CSC369_InterruptsDisable();
  while (i < CSC369_MAX_THREADS) {
    threads[i].state = CSC369_THREAD_FREE;
    threads[i].id = i;
    i++;
  }
  threads[0].state = CSC369_THREAD_RUNNING;
  threads[0].stack_address = NULL; // first thread
  threads[0].exit_code = 0;
  threads[0].join_threads = CSC369_WaitQueueCreate();

  // getcontext(&threads[0].context);
  current_thread = &threads[0];
  Queue_Init(&ready_threads);
  // Queue_Init(zombie_threads);
  CSC369_InterruptsSet(prev_state);
  return 0;  
}

Tid
CSC369_ThreadId(void)
{
  return current_thread->id;
}

Tid
CSC369_ThreadCreate(void (*f)(void*), void* arg)
{
  int const prev_state = CSC369_InterruptsDisable();
  int i = 0;
  clear_Zombies();
  // Find a Free TCB.
  while (i < CSC369_MAX_THREADS && threads[i].state != CSC369_THREAD_FREE) {
    i++;
  }
  // Check that we found a free TCB. Else, return error.
  if (i >= CSC369_MAX_THREADS) {
    CSC369_InterruptsSet(prev_state);
    return CSC369_ERROR_SYS_THREAD;
  }
  // Initialize TCB
  threads[i].id = i;
  threads[i].state = CSC369_THREAD_READY;
  
  ///////////////////////////////////////////////////////::
  // COME BACK : CREATE THE WAIT QUEUE FOR JOIN (TASK 2)
  threads[i].join_threads = CSC369_WaitQueueCreate();
  ///////////////////////////////////////////////////////////:

  void* stack_address_pointer = (void*) malloc(CSC369_THREAD_STACK_SIZE + 8);
  // Check that space was allocated successfully.
  if (stack_address_pointer == NULL) {
    CSC369_InterruptsSet(prev_state);
    return CSC369_ERROR_SYS_MEM;
  }
  threads[i].stack_address = stack_address_pointer;

  int err = getcontext(&(threads[i].context));
  assert(!err);
  threads[i].context.uc_mcontext.gregs[REG_RIP] = (unsigned long) &MyThreadStub;
  threads[i].context.uc_mcontext.gregs[REG_RDI] = (unsigned long) f;
  threads[i].context.uc_mcontext.gregs[REG_RSI] = (unsigned long) arg;
  unsigned long a = (unsigned long) stack_address_pointer + CSC369_THREAD_STACK_SIZE - 8;
  threads[i].context.uc_mcontext.gregs[REG_RSP] = a; // Could be misaligned.

  // Add Thread to queue of Ready Threads.
  Queue_Enqueue(&ready_threads, &threads[i]);
  CSC369_InterruptsSet(prev_state);
  return i;
}

void
CSC369_ThreadExit(int exit_code)
{
  int const prev_state = CSC369_InterruptsDisable();
  current_thread->exit_code = exit_code;
  CSC369_ThreadWakeAll(current_thread->join_threads);
  if (Queue_IsEmpty(&ready_threads)) {
    CSC369_InterruptsSet(prev_state);
    exit(exit_code);
  }
  volatile int flagcontext = 0;
  volatile int result = -1;
  // current_thread->state = CSC369_THREAD_ZOMBIE;
  getcontext(&current_thread->context);
  clear_Zombies();
  if (flagcontext != 0) {
    printf("Shouldn't have gotten back to this thread since it already exited.\n");
    printf("current. id: %d, state: %d\n", current_thread->id, current_thread->state);

    return result;
  }
  current_thread->state = CSC369_THREAD_ZOMBIE;
  TCB* next = Queue_Dequeue(&ready_threads);
  while (current_thread->id == next->id) next = Queue_Dequeue(&ready_threads);
  current_thread = next;
  result = current_thread->id;
  flagcontext++;
  setcontext(&next->context);
}

Tid
CSC369_ThreadKill(Tid tid)
{
  int const prev_state = CSC369_InterruptsDisable();
  if (tid < 0 || tid > CSC369_MAX_THREADS) {
    CSC369_InterruptsSet(prev_state);
    return CSC369_ERROR_TID_INVALID;
  } else if (tid == current_thread->id) {
    CSC369_InterruptsSet(prev_state);
    return CSC369_ERROR_THREAD_BAD;
  }
  else {
    TCB* toKill = Queue_Remove(&ready_threads, tid);
    if (toKill == NULL) {
      for (int i = 0; i < CSC369_MAX_THREADS; i++) {
        if (threads[i].state != CSC369_THREAD_FREE) {
          toKill = Queue_Remove(threads[i].join_threads, tid);
          if (toKill != NULL) {
            break;
          }
        }
      }
      if (toKill == NULL) {
        CSC369_InterruptsSet(prev_state);
        return CSC369_ERROR_SYS_THREAD;
      }  
    }
    
    toKill->state = CSC369_THREAD_ZOMBIE;
    toKill->exit_code = CSC369_EXIT_CODE_KILL;
    CSC369_InterruptsSet(prev_state);
    return toKill->id;
  }
}

int
CSC369_ThreadYield()
{
  int const prev_state = CSC369_InterruptsDisable();
  if (Queue_IsEmpty(&ready_threads)) {
    // printf("No Ready Thread to yield to.\n");
    CSC369_InterruptsSet(prev_state);
    return CSC369_ThreadId();
  }
  volatile int flagcontext = 0;
  volatile int result = -1;
  
  getcontext(&current_thread->context);
  clear_Zombies();
  if (flagcontext != 0) {
    CSC369_InterruptsSet(prev_state);
    return result;
  }
  current_thread->state = CSC369_THREAD_READY;
  Queue_Enqueue(&ready_threads, current_thread);
  TCB* next = Queue_Dequeue(&ready_threads);
  assert(next->state == CSC369_THREAD_READY);
  current_thread = next;
  result = current_thread->id;
  flagcontext++;
  setcontext(&next->context);
  CSC369_InterruptsSet(prev_state);
  return -1; // Should never get to this part.
}

int
CSC369_ThreadYieldTo(Tid tid)
{
  int const prev_state = CSC369_InterruptsDisable();
  if (tid < 0 || tid >= CSC369_MAX_THREADS) {
    CSC369_InterruptsSet(prev_state);
    return CSC369_ERROR_TID_INVALID;
  }
  if (tid == current_thread->id) {
    CSC369_InterruptsSet(prev_state);
    return current_thread->id;
  }
  else {
    volatile int flagcontext = 0;
    volatile int result = -1;
    getcontext(&current_thread->context);
    clear_Zombies();
    if (flagcontext != 0) {
      CSC369_InterruptsSet(prev_state);
      return result;
    }
    current_thread->state = CSC369_THREAD_READY;
    Queue_Enqueue(&ready_threads, current_thread);
    TCB* next = Queue_Remove(&ready_threads, tid);
    if (next == NULL || next->state == CSC369_THREAD_BLOCKED) {
      CSC369_InterruptsSet(prev_state);
      return CSC369_ERROR_THREAD_BAD;
    }
    current_thread = next;
    result = current_thread->id;
    flagcontext++;
    setcontext(&next->context);
    CSC369_InterruptsSet(prev_state);// Should never get to this part.
    return -1; 
  }
}

//****************************************************************************
// New Assignment 2 Definitions - Task 2
//****************************************************************************
CSC369_WaitQueue*
CSC369_WaitQueueCreate(void)
{
  CSC369_WaitQueue* waitQueue;
  waitQueue = (CSC369_WaitQueue*) malloc(sizeof(CSC369_WaitQueue));
  if (waitQueue == NULL) {
    return NULL;
  }
  Queue_Init(waitQueue);
  return waitQueue;
}

int
CSC369_WaitQueueDestroy(CSC369_WaitQueue* queue)
{
  // node* current = queue->head;
  // node* next = NULL;
  // while (current != NULL) {
  //   next = current->next;
  //   free(current);
  //   current = next;
  // }
  if (!Queue_IsEmpty(queue)) {
    return CSC369_ERROR_OTHER;
  }
  free(queue);
  return 0;
}

void
CSC369_ThreadSpin(int duration)
{
  struct timeval start, end, diff;

  int ret = gettimeofday(&start, NULL);
  assert(!ret);

  while (1) {
    ret = gettimeofday(&end, NULL);
    assert(!ret);
    timersub(&end, &start, &diff);

    if ((diff.tv_sec * 1000000 + diff.tv_usec) >= duration) {
      return;
    }
  }
}

int
CSC369_ThreadSleep(CSC369_WaitQueue* queue)
{
  assert(queue != NULL);
  int const prev_state = CSC369_InterruptsDisable();
  if (Queue_IsEmpty(&ready_threads)) {
    CSC369_InterruptsSet(prev_state);
    return CSC369_ERROR_SYS_THREAD;
  }
  volatile int flagcontext = 0;
  volatile int result = -1;
  getcontext(&current_thread->context);
  clear_Zombies();
  if (flagcontext != 0) {
    CSC369_InterruptsSet(prev_state);
    return result;
  }
  current_thread->state = CSC369_THREAD_BLOCKED;
  Queue_Enqueue(queue, current_thread);
  TCB* next = Queue_Dequeue(&ready_threads);
  assert(next->state == CSC369_THREAD_READY);
  current_thread = next;
  result = current_thread->id;
  flagcontext++;
  setcontext(&next->context);
  // Should never get to this part.
  CSC369_InterruptsSet(prev_state);
  return -1;
}

int
CSC369_ThreadWakeNext(CSC369_WaitQueue* queue)
{
  assert(queue != NULL);
  int const prev_state = CSC369_InterruptsDisable();
  if (Queue_IsEmpty(queue)) {
    CSC369_InterruptsSet(prev_state);
    return 0;
  }
  else {
    TCB* awaked = Queue_Dequeue(queue);
    if (awaked->state == CSC369_THREAD_ZOMBIE) {
      CSC369_InterruptsSet(prev_state);
      return 1;
    }
    else if (awaked->state == CSC369_THREAD_BLOCKED) {
      awaked->state = CSC369_THREAD_READY;
      awaked->code_collected = current_thread->exit_code;
      Queue_Enqueue(&ready_threads, awaked);
      CSC369_InterruptsSet(prev_state);
      return 1;
    } else {
      printf("This thread shouldn't be here. Debug\n");
    }
  }
  CSC369_InterruptsSet(prev_state);
  return -1;
}

int
CSC369_ThreadWakeAll(CSC369_WaitQueue* queue)
{
  assert(queue != NULL);
  int const prev_state = CSC369_InterruptsDisable();
  int counter = 0;
  while (CSC369_ThreadWakeNext(queue)) counter ++;
  CSC369_InterruptsSet(prev_state);
  return counter;
}

//****************************************************************************
// New Assignment 2 Definitions - Task 3
//****************************************************************************
int
CSC369_ThreadJoin(Tid tid, int* exit_code)
{
  int const prev_state = CSC369_InterruptsDisable();
  if (tid < 0 || tid > CSC369_MAX_THREADS) {
    CSC369_InterruptsSet(prev_state);
    return CSC369_ERROR_TID_INVALID;
  } else if (tid == CSC369_ThreadId()) {
    CSC369_InterruptsSet(prev_state);
    return CSC369_ERROR_THREAD_BAD;
  } else if (threads[tid].state == CSC369_THREAD_ZOMBIE || threads[tid].state == CSC369_THREAD_FREE) {
    CSC369_InterruptsSet(prev_state); 
    return CSC369_ERROR_SYS_THREAD;
  } else {
    CSC369_ThreadSleep(threads[tid].join_threads);
    *exit_code = current_thread->code_collected;
    CSC369_InterruptsSet(prev_state);
    return tid;
  }
}
