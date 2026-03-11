#ifndef BUFFER_H
#define BUFFER_H

#include <pthread.h>

#define BUFFER_SIZE 4096

typedef struct {
    char data[BUFFER_SIZE];
    size_t size;

    int full;
    int finished;

    pthread_mutex_t mutex;
    pthread_cond_t can_produce;
    pthread_cond_t can_consume;

} Buffer;

#endif