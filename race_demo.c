#include <pthread.h>
#include <stdio.h>

#define THREADS 2
#define ITERATIONS 1000000L

static long unsafe_counter = 0;
static long safe_counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *unsafe_worker(void *arg) {
    (void)arg;
    for (long i = 0; i < ITERATIONS; ++i)
        ++unsafe_counter;
    return NULL;
}

static void *safe_worker(void *arg) {
    (void)arg;
    for (long i = 0; i < ITERATIONS; ++i) {
        pthread_mutex_lock(&counter_mutex);
        ++safe_counter;
        pthread_mutex_unlock(&counter_mutex);
    }
    return NULL;
}

int main(void) {
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; ++i)
        if (pthread_create(&threads[i], NULL, unsafe_worker, NULL) != 0) {
            perror("pthread_create");
            return 1;
        }
    for (int i = 0; i < THREADS; ++i)
        pthread_join(threads[i], NULL);

    printf("Без мьютекса: %ld (ожидалось %ld; результат непредсказуем)\n",
           unsafe_counter, THREADS * ITERATIONS);

    for (int i = 0; i < THREADS; ++i)
        if (pthread_create(&threads[i], NULL, safe_worker, NULL) != 0) {
            perror("pthread_create");
            return 1;
        }
    for (int i = 0; i < THREADS; ++i)
        pthread_join(threads[i], NULL);

    printf("С мьютексом:  %ld (ожидалось %ld)\n",
           safe_counter, THREADS * ITERATIONS);
    pthread_mutex_destroy(&counter_mutex);
    return 0;
}
