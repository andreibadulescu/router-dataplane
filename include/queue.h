#ifndef QUEUE_H
#define QUEUE_H

struct queue;
typedef struct queue *queue;

/* create an empty queue */
extern queue create_queue(void);

/* insert an element at the end of the queue */
extern void queue_enq(queue q, void *element);

/* delete the front element on the queue and return it */
extern void *queue_deq(queue q);

/* return a true value if and only if the queue is empty */
extern int queue_empty(queue q);

/* return the length of the queue */
extern unsigned int queue_size(queue q);

#endif
