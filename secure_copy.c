#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#include "buffer.h"
#include "libcaesar/caesar.h"

#define MAX_FILES 100
#define THREAD_COUNT 3

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

typedef struct
{
    void *memory;
    size_t size;

} ProtectedKey;

ProtectedKey protected_key;

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

void segv_handler(int sig, siginfo_t *info, void *context)
{
    (void)sig;
    (void)context;

    fprintf(stderr,
            "\nSECURITY ERROR: illegal access to protected memory at %p\n",
            info->si_addr);

    if(protected_key.memory != NULL)
    {
        mprotect(protected_key.memory,
                 protected_key.size,
                 PROT_READ | PROT_WRITE);

        memset(protected_key.memory,0,protected_key.size);

        munmap(protected_key.memory, protected_key.size);
    }

    exit(1);
}

void setup_signal_handler()
{
    struct sigaction sa;

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = segv_handler;

    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV,&sa,NULL);
}

void init_protected_key(int key)
{
    protected_key.size = sizeof(int);

    protected_key.memory = mmap(NULL,
                                protected_key.size,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS,
                                -1,
                                0);

    if(protected_key.memory == MAP_FAILED)
    {
        perror("mmap failed");
        exit(1);
    }

    memcpy(protected_key.memory, &key, sizeof(int));

    if(mprotect(protected_key.memory,
                protected_key.size,
                PROT_NONE) != 0)
    {
        perror("mprotect failed");
        exit(1);
    }
}

int get_protected_key()
{
    if(mprotect(protected_key.memory,
                protected_key.size,
                PROT_READ) != 0)
    {
        perror("mprotect read failed");
        exit(1);
    }

    int key;

    memcpy(&key, protected_key.memory, sizeof(int));

    if(mprotect(protected_key.memory,
                protected_key.size,
                PROT_NONE) != 0)
    {
        perror("mprotect reset failed");
        exit(1);
    }

    return key;
}

void destroy_protected_key()
{
    if(protected_key.memory == NULL)
        return;

    mprotect(protected_key.memory,
             protected_key.size,
             PROT_READ | PROT_WRITE);

    memset(protected_key.memory,0,protected_key.size);

    mprotect(protected_key.memory,
             protected_key.size,
             PROT_NONE);

    munmap(protected_key.memory,
           protected_key.size);

    protected_key.memory = NULL;
}

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

void process_file(const char *filename)
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
    (void)arg;

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
        
        process_file(filename);

        pthread_mutex_lock(&global_mutex);
        files_done++;
        pthread_mutex_unlock(&global_mutex);
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

Statistics run_sequential()
{
    Statistics stats;

    double start = get_time();

    for(int i = 0; i < file_count; i++)
    {
        process_file(files[i]);

        files_done++;
    }

    double end = get_time();

    stats.total_time = end - start;

    stats.processed_files = files_done;

    if(files_done > 0)
        stats.avg_time = stats.total_time / files_done;
    else
        stats.avg_time = 0;

    return stats;
}

Statistics run_parallel()
{
    Statistics stats;

    double start = get_time();

    pthread_t threads[THREAD_COUNT];

    for(int i = 0; i < THREAD_COUNT; i++)
    {
        pthread_create(&threads[i], NULL, worker, NULL);
    }

    for(int i = 0; i < THREAD_COUNT; i++)
    {
        pthread_join(threads[i], NULL);
    }

    double end = get_time();

    stats.total_time = end - start;

    stats.processed_files = files_done;

    if(files_done > 0)
        stats.avg_time = stats.total_time / files_done;
    else
        stats.avg_time = 0;

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
    setup_signal_handler();
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
    init_protected_key(key);

    ///int *hack = (int*)protected_key.memory;

    ///*hack = 999;
    
    file_count = argc - 4;
    outdir = argv[argc-2];

    for(int i = 0; i < file_count; i++)
    {
        files[i] = argv[i+2];
    }

    current_file = 0;
    files_done = 0;

    mkdir(outdir? 0777);

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

        stats = run_sequential();
    }
    else
    {
        printf("Running parallel mode\n");

        stats = run_parallel();
    }

    if(!keep_running)
        printf("\nОперация прервана пользователем\n");
    printf("\n===== STATISTICS =====\n");

    printf("Processed files: %d\n", stats.processed_files);

    printf("Total time: %.3f sec\n", stats.total_time);

    printf("Average per file: %.3f sec\n", stats.avg_time);

    destroy_protected_key();
    
    return 0;
}
