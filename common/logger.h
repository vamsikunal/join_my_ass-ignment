/* logger.h — CSV result file writer
 * Distributed Hashcash PoW System
 */

#ifndef LOGGER_H
#define LOGGER_H

#include "protocol.h"

#define RESULTS_PATH  "results/results.csv"
#define CSV_HEADER    "difficulty,stamp,bits_verified,time_ms,winner_ip," \
                      "active_workers,est_hashrate,timestamp\n"

/* Write one round result to results/results.csv.
 * Creates the file with header if it does not already exist.
 * Thread-safe: uses an internal mutex.
 * Returns 0 on success, -1 on error. */
int log_result(const RoundResult *r);

/* Ensure results directory + file exist with header. Call once at startup. */
void logger_init(void);

#endif /* LOGGER_H */
