#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define DEBUG_LOG(msg,...) printf("threading DEBUG: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    struct thread_data* thread_func_args = (struct thread_data *)thread_param;
    
    usleep(thread_func_args->wait_to_obtain_ms * 1000);

    pthread_mutex_lock(thread_func_args->mutex);
    {
       usleep(thread_func_args->wait_to_release_ms * 1000);
    }
    pthread_mutex_unlock(thread_func_args->mutex);

    thread_func_args->thread_complete_success = true;

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,
        int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * allocate memory for thread_data, setup mutex and wait arguments, 
     * pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     */
    struct thread_data* data = (struct thread_data*)malloc(sizeof(struct thread_data));
    if (data == NULL) {
        ERROR_LOG("Failed to allocate memory to create thread_data struct");
        return false;
    }

    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->mutex = mutex;
    data->thread_complete_success = false;

    pthread_t t_handle;
    int ret = pthread_create(&t_handle, NULL, threadfunc, (void *)data);
    if (ret != 0) {
        ERROR_LOG("Failed to create thread");
        return false;
    }

    *thread = t_handle;
    return true;
}

