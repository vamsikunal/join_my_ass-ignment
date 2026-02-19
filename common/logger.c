/* logger.c — CSV result file writer
 * Distributed Hashcash PoW System
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include "logger.h"

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── logger_init ─────────────────────────────────────────────── */
void logger_init(void) {
    /* Create results/ directory if missing */
    mkdir("results", 0755);

    /* Create CSV with header if file does not exist */
    struct stat st;
    if (stat(RESULTS_PATH, &st) != 0) {
        FILE *f = fopen(RESULTS_PATH, "w");
        if (f) {
            fputs(CSV_HEADER, f);
            fclose(f);
        }
    }
}

/* ── log_result ──────────────────────────────────────────────── */
int log_result(const RoundResult *r) {
    if (!r) return -1;

    pthread_mutex_lock(&log_mutex);

    FILE *f = fopen(RESULTS_PATH, "a");
    if (!f) {
        pthread_mutex_unlock(&log_mutex);
        return -1;
    }

    fprintf(f, "%d,\"%s\",%d,%ld,%s,%d,%ld,%s\n",
        r->difficulty,
        r->stamp,
        r->bits_verified,
        r->time_ms,
        r->winner_ip,
        r->active_workers,
        r->est_hashrate,
        r->timestamp);

    fflush(f);
    fclose(f);

    pthread_mutex_unlock(&log_mutex);
    return 0;
}
