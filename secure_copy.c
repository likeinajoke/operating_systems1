#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "buffer.h"
#include "libcaesar/caesar.h"

volatile int keep_running = 1;

typedef struct {
    FILE *input;
    FILE *output;
    Buffer *buffer;
    int key;

    long total_size;
    long processed;

} Context;

void sigint_handler(int sig)
{
    (void)sig;
    keep_running = 0;
}

void show_progress(long done, long total)
{
    static int last_percent = -1;

    int percent = (done * 100) / total;

    if (percent / 10 != last_percent / 10)
    {
        last_percent = percent;

        int bars = percent / 10;

        printf("\r[");
        for(int i=0;i<10;i++)
        {
            if(i < bars) printf("=");
            else printf(" ");
        }

        printf("] %d%%", percent);
        fflush(stdout);
    }
}

void* producer(void *arg)
{
    Context *ctx = (Context*)arg;
    Buffer *buf = ctx->buffer;

    while(keep_running)
    {
        pthread_mutex_lock(&buf->mutex);

        while(buf->full && keep_running)
            pthread_cond_wait(&buf->can_produce,&buf->mutex);

        if(!keep_running)
        {
            pthread_mutex_unlock(&buf->mutex);
            break;
        }

        size_t n = fread(buf->data,1,BUFFER_SIZE,ctx->input);

        if(n == 0)
        {
            buf->finished = 1;
            pthread_cond_signal(&buf->can_consume);
            pthread_mutex_unlock(&buf->mutex);
            break;
        }

        set_key(ctx->key);
        caesar(buf->data, buf->data, n);

        buf->size = n;
        buf->full = 1;

        pthread_cond_signal(&buf->can_consume);
        pthread_mutex_unlock(&buf->mutex);
    }

    return NULL;
}

void* consumer(void *arg)
{
    Context *ctx = (Context*)arg;
    Buffer *buf = ctx->buffer;

    while(keep_running)
    {
        pthread_mutex_lock(&buf->mutex);

        while(!buf->full && !buf->finished && keep_running)
            pthread_cond_wait(&buf->can_consume,&buf->mutex);

        if(buf->full)
        {
            fwrite(buf->data,1,buf->size,ctx->output);

            ctx->processed += buf->size;

            show_progress(ctx->processed,ctx->total_size);

            buf->full = 0;

            pthread_cond_signal(&buf->can_produce);
        }

        if(buf->finished)
        {
            pthread_mutex_unlock(&buf->mutex);
            break;
        }

        pthread_mutex_unlock(&buf->mutex);
    }

    return NULL;
}

long get_file_size(FILE *f)
{
    fseek(f,0,SEEK_END);
    long size = ftell(f);
    rewind(f);
    return size;
}

int main(int argc,char *argv[])
{
    if(argc != 4)
    {
        fprintf(stderr, "Usage: ./secure_copy input output key\n");
        return 1;
    }

    signal(SIGINT,sigint_handler);

    FILE *input = fopen(argv[1],"rb");

    if(!input)
    {
        perror("Ошибка открытия входного файла");
        return 1;
    }

    FILE *output = fopen(argv[2],"wb");

    if(!output)
    {
        perror("Ошибка создания выходного файла");
        fclose(input);
        return 1;
    }

    int key = atoi(argv[3]);

    Buffer buffer;

    buffer.full = 0;
    buffer.finished = 0;

    pthread_mutex_init(&buffer.mutex,NULL);
    pthread_cond_init(&buffer.can_produce,NULL);
    pthread_cond_init(&buffer.can_consume,NULL);

    Context ctx;

    ctx.input = input;
    ctx.output = output;
    ctx.buffer = &buffer;
    ctx.key = key;

    ctx.total_size = get_file_size(input);
    ctx.processed = 0;

    pthread_t producer_thread;
    pthread_t consumer_thread;

    pthread_create(&producer_thread,NULL,producer,&ctx);
    pthread_create(&consumer_thread,NULL,consumer,&ctx);

    pthread_join(producer_thread,NULL);
    pthread_join(consumer_thread,NULL);

    fclose(input);
    fclose(output);

    pthread_mutex_destroy(&buffer.mutex);
    pthread_cond_destroy(&buffer.can_produce);
    pthread_cond_destroy(&buffer.can_consume);

    if(!keep_running)
        printf("\nОперация прервана пользователем\n");
    else
        printf("\nГотово\n");

    return 0;
}