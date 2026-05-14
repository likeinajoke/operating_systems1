#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#include "buffer.h"
#include "libcaesar/caesar.h"

#define MAX_FILES 100
#define THREAD_COUNT 4

typedef enum
{
    MODE_SEQUENTIAL,
    MODE_PARALLEL,
    MODE_AUTO,

} Mode;

typedef struct
{
    double total_time;
    double avg_time;
    int processed_files;

} Statistics;

char *files[MAX_FILES];
int file_count = 0;
int current_file = 0;
char *outdir;

pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;

int files_done = 0;

volatile int keep_running = 1;

double get_time()
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

void write_log(const char *filename, const char *status)
{
    FILE *log = fopen("log.txt","a");
    if(!log) return;

    time_t now = time(NULL);
    char *time_str = ctime(&now);

    fprintf(log,"%s | thread %llu | %s | %s\n",
            time_str,
            (unsigned long long)pthread_self(),
            filename,
            status);

    fclose(log);
}

int lock_with_timeout(pthread_mutex_t *mutex)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;

    int res = pthread_mutex_timedlock(mutex, &ts);

    if(res == ETIMEDOUT)
    {
        printf("Deadlock warning: thread %llu waited too long\n", (unsigned long long)pthread_self());
        return 0;
    }

    return 1;
}

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

void process_file(const char *filename, int key)
{
    FILE *in = fopen(filename, "rb");

    if(!in)
    {
        perror("Input open error");

        write_log(filename, "error open");

        return;
    }

    char *base = strrchr(filename, '/');

    if(!base)
        base = strrchr(filename, '\\');

    if(base)
        base++;
    else
        base = (char*)filename;

    char outname[256];

    sprintf(outname, "%s/%s", outdir, base);

    printf("INPUT: %s\n", filename);
    printf("OUTPUT: %s\n", outname);

    FILE *out = fopen(outname, "wb");

    if(!out)
    {
        perror("Output open error");

        write_log(filename, "error output");

        fclose(in);

        return;
    }

    char buffer[BUFFER_SIZE];

    size_t n;

    while((n = fread(buffer, 1, BUFFER_SIZE, in)) > 0)
    {
        set_key(key);

        caesar(buffer, buffer, n);

        fwrite(buffer, 1, n, out);
    }

    fclose(in);
    fclose(out);

    write_log(filename, "success");
}

void* worker(void *arg)
{
    int key = *(int*)arg;

    while(keep_running)
    {
        while(!lock_with_timeout(&global_mutex));

        if(current_file >= file_count)
        {
            pthread_mutex_unlock(&global_mutex);
            break;
        }

        char *filename = files[current_file];
        current_file++;

        pthread_mutex_unlock(&global_mutex);
        process_file(filename, key);

        pthread_mutex_lock(&global_mutex);
        files_done++;
        pthread_mutex_unlock(&global_mutex);

        write_log(filename,"success");
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

Statistics run_sequential(int key)
{
    Statistics stats;

    double start = get_time();

    for(int i = 0; i < file_count; i++)
    {
        process_file(files[i], key);

        files_done++;
    }

    double end = get_time();

    stats.total_time = end - start;

    stats.processed_files = files_done;

    stats.avg_time = stats.total_time / files_done;

    return stats;
}

Statistics run_parallel(int key)
{
    Statistics stats;

    double start = get_time();

    pthread_t threads[THREAD_COUNT];

    for(int i = 0; i < THREAD_COUNT; i++)
    {
        pthread_create(&threads[i], NULL, worker, &key);
    }

    for(int i = 0; i < THREAD_COUNT; i++)
    {
        pthread_join(threads[i], NULL);
    }

    double end = get_time();

    stats.total_time = end - start;

    stats.processed_files = files_done;

    stats.avg_time = stats.total_time / files_done;

    return stats;
}

int main(int argc,char *argv[])
{
    if(argc <= 4)
    {
        fprintf(stderr, "Usage: ./secure_copy --mode=... file1 file2 ... outdir key\n");
        return 1;
    }

    signal(SIGINT,sigint_handler);
    Mode mode = MODE_AUTO;
    
    if(strcmp(argv[1], "--mode=sequential") == 0)
    {
        mode = MODE_SEQUENTIAL;
    }
    else if(strcmp(argv[1], "--mode=parallel") == 0)
    {
        mode = MODE_PARALLEL;
    }
    else if(strcmp(argv[1], "--mode=auto") == 0)
    {
        mode = MODE_AUTO;
    }
    else
    {
        fprintf(stderr, "Unknown mode\n");

        return 1;
    }
    
    int key = atoi(argv[argc - 1]);
    file_count = argc - 4;
    outdir = argv[argc-2];

    for(int i = 0; i < file_count; i++)
    {
        files[i] = argv[i+1];
    }

    char *outdir = argv[argc-2];

    current_file = 0;
    files_done = 0;

    mkdir(outdir);

    if(mode == MODE_AUTO)
    {
        if(file_count < 5)
        {
            mode = MODE_SEQUENTIAL;
        }
        else
        {
            mode = MODE_PARALLEL;
        }
    }
    
    Statistics stats;

    if(mode == MODE_SEQUENTIAL)
    {
        printf("Running sequential mode\n");

        stats = run_sequential(key);
    }
    else
    {
        printf("Running parallel mode\n");

        stats = run_parallel(key);
    }

    if(!keep_running)
        printf("\nОперация прервана пользователем\n");
    printf("\n===== STATISTICS =====\n");

    printf("Processed files: %d\n", stats.processed_files);

    printf("Total time: %.3f sec\n", stats.total_time);

    printf("Average per file: %.3f sec\n", stats.avg_time);

    return 0;
}
