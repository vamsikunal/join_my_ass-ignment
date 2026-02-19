/* protocol.h — shared message format and constants
 * Distributed Hashcash PoW System
 * CS740 Assignment — vamsikunal/join_my_ass-ignment
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

/* ── Network ─────────────────────────────────────────────────── */
#define DEFAULT_PORT      9000
#define HTTP_PORT         8000
#define BACKLOG           16
#define RECV_BUF_SIZE     1024
#define SEND_BUF_SIZE     1024

/* ── Hashcash Parameters ─────────────────────────────────────── */
#define HC_RESOURCE       "distributed-pow-lab"
#define HC_START_BITS     10          /* starting difficulty */
#define HC_TIME_WIDTH     6           /* YYMMDD date format  */
#define WORKER_THREADS    12          /* 6 cores × 2 HT      */

/* ── Protocol Messages ───────────────────────────────────────── */
/*
 * Coordinator → Worker:
 *   WORK|<bits>\n          — start mining at this difficulty
 *   STOP\n                 — abort current round
 *   PING\n                 — heartbeat check
 *
 * Worker → Coordinator:
 *   READY\n                — connected and waiting for work
 *   FOUND|<stamp>\n        — found a valid stamp
 *   PONG\n                 — heartbeat reply
 */

#define MSG_WORK    "WORK"
#define MSG_STOP    "STOP"
#define MSG_PING    "PING"
#define MSG_READY   "READY"
#define MSG_FOUND   "FOUND"
#define MSG_PONG    "PONG"

#define MSG_DELIM   "|"
#define MSG_END     "\n"

/* ── Worker States ───────────────────────────────────────────── */
typedef enum {
    WS_CONNECTING  = 0,
    WS_READY       = 1,
    WS_MINING      = 2,
    WS_WAITING     = 3,   /* joined mid-round, waits for next */
    WS_DEAD        = 4
} WorkerState;

/* ── Worker Info (coordinator side) ─────────────────────────── */
#define MAX_IP_LEN  48

typedef struct {
    int           fd;
    char          ip[MAX_IP_LEN];
    WorkerState   state;
    int           rounds_participated;
    int           rounds_won;
} WorkerInfo;

#define MAX_WORKERS  64

/* ── Result Record ───────────────────────────────────────────── */
#define MAX_STAMP_LEN  256

typedef struct {
    int    difficulty;
    char   stamp[MAX_STAMP_LEN];
    int    bits_verified;
    long   time_ms;
    char   winner_ip[MAX_IP_LEN];
    int    active_workers;
    long   est_hashrate;
    char   timestamp[32];
} RoundResult;

/* ── Helpers ─────────────────────────────────────────────────── */
#define SAFE_STR(s)   ((s) ? (s) : "")

#endif /* PROTOCOL_H */
