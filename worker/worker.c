/* worker.c — Mining node
 * Distributed Hashcash PoW System
 * CS740 Assignment — vamsikunal/join_my_ass-ignment
 *
 * Build:  see Makefile
 * Usage:  ./worker <coordinator_ip> <port>
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include "../common/protocol.h"
#include "../common/logger.h"
#include "../common/tui.h"
#include "hashcash.h"   /* original hashcash library */

/* ═══════════════════════════════════════════════════════════════
 * Global State
 * ═══════════════════════════════════════════════════════════════ */

static int          g_sock          = -1;
static char         g_coord_ip[MAX_IP_LEN] = {0};
static int          g_coord_port    = DEFAULT_PORT;

/* Mining state — written by network thread, read by miner threads */
static volatile int g_stop_flag     = 1;   /* 1 = stop/idle, 0 = mine */
static volatile int g_winner_found  = 0;
static int          g_current_bits  = 0;
static pthread_mutex_t g_state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Statistics */
static volatile long   g_hashrate   = 0;
static volatile long   g_round_start_ms = 0;
static volatile int    g_rounds_won  = 0;
static volatile int    g_rounds_part = 0;

/* Found stamp storage */
static char g_found_stamp[MAX_STAMP_LEN] = {0};

/* TUI redraw flag */
static volatile int g_running = 1;

/* ═══════════════════════════════════════════════════════════════
 * Utility
 * ═══════════════════════════════════════════════════════════════ */

static long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)(tv.tv_sec * 1000L + tv.tv_usec / 1000L);
}

static int send_msg(int fd, const char *msg) {
    size_t len = strlen(msg);
    ssize_t sent = send(fd, msg, len, MSG_NOSIGNAL);
    return (sent == (ssize_t)len) ? 0 : -1;
}

static void get_my_ip(char *buf, size_t buflen) {
    /* Get the IP we used to reach the coordinator */
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(g_sock, (struct sockaddr*)&addr, &len) == 0)
        inet_ntop(AF_INET, &addr.sin_addr, buf, (socklen_t)buflen);
    else
        strncpy(buf, "unknown", buflen - 1);
}

/* ═══════════════════════════════════════════════════════════════
 * Hashcash Mint Callback
 * Called by hashcash_mint every ~100ms.
 * Returning 0 causes mint to abort (HASHCASH_USER_ABORT).
 * ═══════════════════════════════════════════════════════════════ */

typedef struct {
    int thread_id;
} MintCallbackArg;

static int mint_callback(int percent, int largest, int target,
                         double count, double expected, void *user) {
    (void)percent; (void)largest; (void)target;
    (void)count; (void)expected; (void)user;

    /* Stop if coordinator said STOP or another thread found a solution */
    if (g_stop_flag || g_winner_found)
        return 0;   /* abort mint */
    return 1;       /* continue */
}

/* ═══════════════════════════════════════════════════════════════
 * Miner Thread
 * Each of the 12 threads calls hashcash_mint independently.
 * hashcash_mint uses a random counter start internally so threads
 * cover different parts of the nonce space naturally.
 * ═══════════════════════════════════════════════════════════════ */

static void* miner_thread(void *arg) {
    int tid = *(int*)arg;
    free(arg);

    while (g_running) {
        /* Wait for work */
        while (g_stop_flag && g_running)
            usleep(5000);   /* 5ms poll */

        if (!g_running) break;

        int bits = g_current_bits;   /* snapshot */
        char *stamp = NULL;
        double tries = 0.0;

        int ret = hashcash_mint(
            time(NULL),          /* now_time */
            HC_TIME_WIDTH,       /* time_width: YYMMDD */
            HC_RESOURCE,         /* resource */
            (unsigned)bits,      /* bits */
            0,                   /* anon_period */
            &stamp,              /* output stamp */
            NULL,                /* anon_random */
            &tries,              /* tries_taken */
            NULL,                /* ext */
            0,                   /* compress */
            mint_callback,       /* callback */
            NULL                 /* user arg */
        );

        if (ret == HASHCASH_OK && stamp != NULL) {
            /* We found it — claim the win */
            int already_won = __sync_val_compare_and_swap(&g_winner_found, 0, 1);
            if (already_won == 0) {
                /* First thread to find */
                pthread_mutex_lock(&g_state_mutex);
                strncpy(g_found_stamp, stamp, MAX_STAMP_LEN - 1);
                g_found_stamp[MAX_STAMP_LEN - 1] = '\0';
                g_stop_flag = 1;   /* stop all other threads */
                pthread_mutex_unlock(&g_state_mutex);

                /* Notify coordinator */
                char msg[MAX_STAMP_LEN + 16];
                snprintf(msg, sizeof(msg), "FOUND|%s\n", g_found_stamp);

                pthread_mutex_lock(&g_state_mutex);
                send_msg(g_sock, msg);
                g_rounds_won++;
                pthread_mutex_unlock(&g_state_mutex);

                /* Also save to local results.csv */
                RoundResult r;
                memset(&r, 0, sizeof(r));
                r.difficulty    = bits;
                r.bits_verified = (int)hashcash_count(g_found_stamp);
                r.time_ms       = now_ms() - g_round_start_ms;
                r.est_hashrate  = g_hashrate;
                get_my_ip(r.winner_ip, sizeof(r.winner_ip));
                strncpy(r.stamp, g_found_stamp, MAX_STAMP_LEN - 1);
                r.active_workers = 1;  /* worker doesn't know total count */
                time_t t = time(NULL);
                struct tm *tm_info = gmtime(&t);
                strftime(r.timestamp, sizeof(r.timestamp),
                         "%Y-%m-%d %H:%M:%S", tm_info);
                log_result(&r);

                printf("\n[Thread %d] FOUND stamp at difficulty %d\n",
                       tid, bits);
                fflush(stdout);
            }
        }

        if (stamp) hashcash_free(stamp);

        /* Idle until next round */
        while (!g_stop_flag && g_running)
            usleep(1000);
    }

    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 * Stats Thread
 * Reads hashcash_per_sec every second and updates g_hashrate.
 * ═══════════════════════════════════════════════════════════════ */

static void* stats_thread(void *arg) {
    (void)arg;
    while (g_running) {
        g_hashrate = (long)hashcash_per_sec();
        sleep(1);
    }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 * TUI Thread
 * Redraws the worker display every 500ms.
 * ═══════════════════════════════════════════════════════════════ */

static void* tui_thread(void *arg) {
    (void)arg;
    char my_ip[MAX_IP_LEN] = "connecting...";
    char time_buf[16];
    char rate_buf[20];
    char coord_addr[64];

    snprintf(coord_addr, sizeof(coord_addr), "%s:%d", g_coord_ip, g_coord_port);

    while (g_running) {
        get_my_ip(my_ip, sizeof(my_ip));

        long elapsed_ms = g_stop_flag ? 0 : (now_ms() - g_round_start_ms);
        tui_fmt_time(time_buf, elapsed_ms);

        /* Use cached g_hashrate — never call hashcash_per_sec() here.
         * That function runs an internal benchmark and conflicts with
         * the stats thread, causing the TUI to freeze. */
        long hr = g_hashrate;
        tui_fmt_rate(rate_buf, hr);

        const char *status     = g_stop_flag ? "IDLE" : "MINING";
        const char *status_col = tui_state_color(status);

        tui_clear();

        /* ── Intro banner (static info, always visible) ── */
        printf(ANSI_BOLD ANSI_BCYAN
               "  CS740 — Distributed Hashcash Proof-of-Work  |  Lab Experiment\n"
               ANSI_RESET);
        printf(ANSI_DIM
               "  What    : SHA-1 based Proof-of-Work (hashcash.org original library)\n"
               "  Goal    : Find a stamp with N leading zero bits in SHA-1 hash\n"
               "  Results : ~/distributed-pow/results.csv  (updated after every round)\n"
               "  Verify  : hashcash -c -b <bits> -r distributed-pow-lab <stamp>\n"
               ANSI_RESET);
        printf("\n");

        /* ── Main TUI box ── */
        tui_top();
        tui_title(ANSI_BCYAN, "DISTRIBUTED HASHCASH  —  WORKER");
        tui_divider();

        /* Connection info */
        tui_kv("Coordinator :", ANSI_BWHITE, coord_addr);
        tui_kv("My IP       :", ANSI_BWHITE, my_ip);
        tui_kv("Status      :", status_col, status);

        tui_divider();

        /* Mining info */
        char bits_str[16];
        if (g_current_bits > 0)
            snprintf(bits_str, sizeof(bits_str), "%d  bits", g_current_bits);
        else
            strncpy(bits_str, "waiting...", sizeof(bits_str));

        tui_kv("Difficulty  :", ANSI_BYELLOW, bits_str);
        tui_kv("Resource    :", ANSI_DIM, HC_RESOURCE);

        char thr_str[32];
        snprintf(thr_str, sizeof(thr_str), "%d active", WORKER_THREADS);
        tui_kv("Threads     :", ANSI_BWHITE, thr_str);

        tui_divider();

        /* Performance — computed from cached rate only, no library calls */
        tui_kv("Hash Rate   :", ANSI_BGREEN,
               hr > 0 ? rate_buf : "benchmarking...");
        tui_kv("Round Time  :", ANSI_BWHITE, time_buf);

        /* Estimate: expected_tries(bits) / cached_rate — no benchmark call */
        if (!g_stop_flag && g_current_bits > 0 && hr > 0) {
            double expected = hashcash_expected_tries(g_current_bits);
            double elapsed_s = elapsed_ms / 1000.0;
            double remain_s  = (expected / (double)hr) - elapsed_s;
            if (remain_s < 0) remain_s = 0;
            char rem_buf[12];
            tui_fmt_time(rem_buf, (long)(remain_s * 1000));
            tui_kv("Est. Remain :", ANSI_BYELLOW, rem_buf);
        } else {
            tui_kv("Est. Remain :", ANSI_DIM, "—");
        }

        tui_divider();

        /* Session stats */
        char won_str[64];
        snprintf(won_str, sizeof(won_str), "%-4d    Participated: %d",
                 g_rounds_won, g_rounds_part);
        tui_kv("Rounds Won  :", ANSI_BGREEN, won_str);
        tui_kv("Results     :", ANSI_BWHITE, "~/distributed-pow/results.csv");

        tui_bottom();
        fflush(stdout);

        usleep(500000);   /* 500ms */
    }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 * Network: Connect to Coordinator
 * ═══════════════════════════════════════════════════════════════ */

static int connect_to_coordinator(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)g_coord_port);

    if (inet_pton(AF_INET, g_coord_ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid coordinator IP: %s\n", g_coord_ip);
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd;
}

/* ═══════════════════════════════════════════════════════════════
 * Network Loop — receives WORK / STOP / PING from coordinator
 * ═══════════════════════════════════════════════════════════════ */

static void network_loop(void) {
    char buf[RECV_BUF_SIZE];
    char line[RECV_BUF_SIZE];
    size_t buf_used = 0;

    /* Send READY */
    send_msg(g_sock, "READY\n");
    logger_init();

    while (g_running) {
        ssize_t n = recv(g_sock, buf + buf_used,
                         sizeof(buf) - buf_used - 1, 0);
        if (n <= 0) {
            if (n == 0)
                fprintf(stderr, "\nCoordinator disconnected.\n");
            else
                perror("\nrecv");
            break;
        }
        buf_used += (size_t)n;
        buf[buf_used] = '\0';

        /* Process complete newline-terminated messages */
        char *start = buf;
        char *nl;
        while ((nl = memchr(start, '\n', buf_used - (size_t)(start - buf))) != NULL) {
            size_t msg_len = (size_t)(nl - start);
            if (msg_len >= sizeof(line)) msg_len = sizeof(line) - 1;
            memcpy(line, start, msg_len);
            line[msg_len] = '\0';
            start = nl + 1;

            /* ── WORK|<bits> ── */
            if (strncmp(line, "WORK|", 5) == 0) {
                int bits = atoi(line + 5);
                pthread_mutex_lock(&g_state_mutex);
                g_current_bits  = bits;
                g_winner_found  = 0;
                g_stop_flag     = 0;
                g_round_start_ms = now_ms();
                g_rounds_part++;
                pthread_mutex_unlock(&g_state_mutex);

            /* ── STOP ── */
            } else if (strcmp(line, "STOP") == 0) {
                pthread_mutex_lock(&g_state_mutex);
                g_stop_flag    = 1;
                g_winner_found = 0;
                pthread_mutex_unlock(&g_state_mutex);

            /* ── PING ── */
            } else if (strcmp(line, "PING") == 0) {
                send_msg(g_sock, "PONG\n");
            }
        }

        /* Shift remaining data to front of buffer */
        size_t remaining = buf_used - (size_t)(start - buf);
        if (remaining > 0)
            memmove(buf, start, remaining);
        buf_used = remaining;
    }

    g_running = 0;
    g_stop_flag = 1;
}

/* ═══════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <coordinator_ip> <port>\n", argv[0]);
        return 1;
    }

    strncpy(g_coord_ip, argv[1], sizeof(g_coord_ip) - 1);
    g_coord_port = atoi(argv[2]);

    signal(SIGPIPE, SIG_IGN);

    /* Select fastest hashcash mint core for this CPU */
    hashcash_use_core(-1);  /* auto-select */

    /* Connect */
    g_sock = connect_to_coordinator();
    if (g_sock < 0) {
        fprintf(stderr, "Failed to connect to %s:%d\n",
                g_coord_ip, g_coord_port);
        return 1;
    }

    /* Launch TUI thread */
    pthread_t tid_tui, tid_stats;
    pthread_create(&tid_tui,   NULL, tui_thread,   NULL);
    pthread_create(&tid_stats, NULL, stats_thread, NULL);

    /* Launch 12 miner threads */
    pthread_t miner_tids[WORKER_THREADS];
    for (int i = 0; i < WORKER_THREADS; i++) {
        int *id = malloc(sizeof(int));
        *id = i;
        pthread_create(&miner_tids[i], NULL, miner_thread, id);
    }

    /* Main thread runs network loop */
    network_loop();

    /* Cleanup */
    g_running  = 0;
    g_stop_flag = 1;
    close(g_sock);

    for (int i = 0; i < WORKER_THREADS; i++)
        pthread_join(miner_tids[i], NULL);
    pthread_join(tid_stats, NULL);
    pthread_join(tid_tui,   NULL);

    printf("\nWorker exiting.\n");
    return 0;
}
