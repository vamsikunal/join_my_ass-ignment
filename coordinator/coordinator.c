/* coordinator.c — Master node (manages only, never mines)
 * Distributed Hashcash PoW System
 * CS740 Assignment — vamsikunal/join_my_ass-ignment
 *
 * Build:  see Makefile
 * Usage:  ./coordinator <port>
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

static WorkerInfo  g_workers[MAX_WORKERS];
static int         g_worker_count  = 0;
static pthread_mutex_t g_workers_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Difficulty loop signalling */
static pthread_cond_t  g_found_cond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_found_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int    g_found_flag  = 0;
static char            g_found_stamp[MAX_STAMP_LEN] = {0};
static char            g_winner_ip[MAX_IP_LEN]  = {0};

/* Current round state (for TUI) */
static volatile int    g_current_bits   = 0;
static volatile int    g_current_round  = 0;
static volatile long   g_round_start_ms = 0;
static volatile long   g_session_start_ms = 0;

/* Last result (for TUI) */
static char   g_last_stamp[MAX_STAMP_LEN] = {0};
static char   g_last_winner[MAX_IP_LEN]   = {0};
static int    g_last_bits   = 0;
static long   g_last_time_ms = 0;

static volatile int g_running = 1;
static int g_server_fd = -1;
static int g_port = DEFAULT_PORT;

/* ═══════════════════════════════════════════════════════════════
 * Utility
 * ═══════════════════════════════════════════════════════════════ */

static long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)(tv.tv_sec * 1000L + tv.tv_usec / 1000L);
}

static int send_to(int fd, const char *msg) {
    size_t len = strlen(msg);
    ssize_t sent = send(fd, msg, len, MSG_NOSIGNAL);
    return (sent == (ssize_t)len) ? 0 : -1;
}

/* Broadcast a message to all MINING workers */
static void broadcast(const char *msg) {
    pthread_mutex_lock(&g_workers_mutex);
    for (int i = 0; i < g_worker_count; i++) {
        if (g_workers[i].state == WS_MINING ||
            g_workers[i].state == WS_READY) {
            send_to(g_workers[i].fd, msg);
        }
    }
    pthread_mutex_unlock(&g_workers_mutex);
}

/* Remove dead worker by index (call with g_workers_mutex held) */
static void remove_worker_locked(int idx) {
    close(g_workers[idx].fd);
    g_workers[idx] = g_workers[g_worker_count - 1];
    g_worker_count--;
}

/* Count workers in a given state */
static int count_workers_in_state(WorkerState s) {
    int c = 0;
    pthread_mutex_lock(&g_workers_mutex);
    for (int i = 0; i < g_worker_count; i++)
        if (g_workers[i].state == s) c++;
    pthread_mutex_unlock(&g_workers_mutex);
    return c;
}

/* ═══════════════════════════════════════════════════════════════
 * Worker Reader Thread
 * One thread per connected worker — reads READY / FOUND / PONG.
 * ═══════════════════════════════════════════════════════════════ */

typedef struct { int worker_idx; } ReaderArg;

static void* worker_reader(void *arg) {
    int idx = ((ReaderArg*)arg)->worker_idx;
    free(arg);

    char buf[RECV_BUF_SIZE];
    char line[RECV_BUF_SIZE];
    size_t buf_used = 0;
    int fd;

    pthread_mutex_lock(&g_workers_mutex);
    fd = g_workers[idx].fd;
    pthread_mutex_unlock(&g_workers_mutex);

    while (g_running) {
        ssize_t n = recv(fd, buf + buf_used,
                         sizeof(buf) - buf_used - 1, 0);
        if (n <= 0) break;   /* disconnected */

        buf_used += (size_t)n;
        buf[buf_used] = '\0';

        char *start = buf;
        char *nl;
        while ((nl = memchr(start, '\n',
                            buf_used - (size_t)(start - buf))) != NULL) {
            size_t msg_len = (size_t)(nl - start);
            if (msg_len >= sizeof(line)) msg_len = sizeof(line) - 1;
            memcpy(line, start, msg_len);
            line[msg_len] = '\0';
            start = nl + 1;

            /* ── READY ── */
            if (strcmp(line, "READY") == 0) {
                pthread_mutex_lock(&g_workers_mutex);
                if (idx < g_worker_count)
                    g_workers[idx].state = WS_READY;
                pthread_mutex_unlock(&g_workers_mutex);

            /* ── FOUND|<stamp> ── */
            } else if (strncmp(line, "FOUND|", 6) == 0) {
                const char *stamp_str = line + 6;

                /* Only accept the first valid FOUND per round */
                pthread_mutex_lock(&g_found_mutex);
                if (!g_found_flag) {
                    /* Verify the stamp */
                    int verified_bits = (int)hashcash_count(stamp_str);
                    if (verified_bits >= g_current_bits) {
                        strncpy(g_found_stamp, stamp_str, MAX_STAMP_LEN - 1);
                        pthread_mutex_lock(&g_workers_mutex);
                        if (idx < g_worker_count)
                            strncpy(g_winner_ip, g_workers[idx].ip,
                                    MAX_IP_LEN - 1);
                        pthread_mutex_unlock(&g_workers_mutex);
                        g_found_flag = 1;
                        pthread_cond_signal(&g_found_cond);
                    }
                }
                pthread_mutex_unlock(&g_found_mutex);

                /* Update win count */
                pthread_mutex_lock(&g_workers_mutex);
                if (idx < g_worker_count)
                    g_workers[idx].rounds_won++;
                pthread_mutex_unlock(&g_workers_mutex);

            /* ── PONG ── */
            } else if (strcmp(line, "PONG") == 0) {
                /* heartbeat OK — no action needed */
            }
        }

        size_t remaining = buf_used - (size_t)(start - buf);
        if (remaining > 0) memmove(buf, start, remaining);
        buf_used = remaining;
    }

    /* Mark worker as dead */
    pthread_mutex_lock(&g_workers_mutex);
    for (int i = 0; i < g_worker_count; i++) {
        if (g_workers[i].fd == fd) {
            g_workers[i].state = WS_DEAD;
            break;
        }
    }
    pthread_mutex_unlock(&g_workers_mutex);

    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 * Accept Thread
 * Runs forever. Adds new workers to g_workers[].
 * ═══════════════════════════════════════════════════════════════ */

static void* accept_thread(void *arg) {
    (void)arg;
    struct sockaddr_in cli;
    socklen_t cli_len = sizeof(cli);

    while (g_running) {
        int cfd = accept(g_server_fd, (struct sockaddr*)&cli, &cli_len);
        if (cfd < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK) continue;
            break;
        }

        char ip[MAX_IP_LEN];
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));

        pthread_mutex_lock(&g_workers_mutex);
        if (g_worker_count < MAX_WORKERS) {
            int idx = g_worker_count++;
            g_workers[idx].fd    = cfd;
            g_workers[idx].state = WS_CONNECTING;
            g_workers[idx].rounds_participated = 0;
            g_workers[idx].rounds_won = 0;
            strncpy(g_workers[idx].ip, ip, MAX_IP_LEN - 1);

            /* Spawn reader thread for this worker */
            ReaderArg *ra = malloc(sizeof(ReaderArg));
            ra->worker_idx = idx;
            pthread_t t;
            pthread_create(&t, NULL, worker_reader, ra);
            pthread_detach(t);
        } else {
            /* No room */
            close(cfd);
        }
        pthread_mutex_unlock(&g_workers_mutex);
    }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 * Heartbeat Thread
 * Pings all workers every 5 seconds; prunes dead ones.
 * ═══════════════════════════════════════════════════════════════ */

static void* heartbeat_thread(void *arg) {
    (void)arg;
    while (g_running) {
        sleep(5);
        pthread_mutex_lock(&g_workers_mutex);
        for (int i = 0; i < g_worker_count; ) {
            if (g_workers[i].state == WS_DEAD) {
                remove_worker_locked(i);
                /* don't increment i — slot was filled by last worker */
            } else {
                if (send_to(g_workers[i].fd, "PING\n") < 0)
                    g_workers[i].state = WS_DEAD;
                i++;
            }
        }
        pthread_mutex_unlock(&g_workers_mutex);
    }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 * TUI Thread
 * Redraws the coordinator display every 500ms.
 * ═══════════════════════════════════════════════════════════════ */

static void* tui_thread(void *arg) {
    (void)arg;
    char round_time_buf[16];
    char session_time_buf[16];
    char rate_buf[20];
    char bits_str[16];
    char round_str[32];
    char workers_str[16];

    while (g_running) {
        long elapsed_round   = g_round_start_ms ?
                               now_ms() - g_round_start_ms : 0;
        long elapsed_session = g_session_start_ms ?
                               now_ms() - g_session_start_ms : 0;

        tui_fmt_time(round_time_buf,   elapsed_round);
        tui_fmt_time(session_time_buf, elapsed_session);
        tui_fmt_rate(rate_buf, (long)hashcash_per_sec());

        int active_workers;
        pthread_mutex_lock(&g_workers_mutex);
        active_workers = g_worker_count;
        pthread_mutex_unlock(&g_workers_mutex);

        const char *status = (g_current_bits > 0 && !g_found_flag)
                             ? "MINING..." : "WAITING FOR WORKERS";

        tui_clear();
        tui_top();
        tui_title(ANSI_BCYAN, "DISTRIBUTED HASHCASH  —  COORDINATOR");
        tui_divider();

        /* Round info */
        snprintf(bits_str,  sizeof(bits_str),  "%d", g_current_bits);
        snprintf(round_str, sizeof(round_str), "Round %d / ∞", g_current_round);
        snprintf(workers_str, sizeof(workers_str), "%d",    active_workers);

        tui_kv("Difficulty  :", ANSI_BYELLOW, bits_str);
        tui_kv("Round       :", ANSI_BWHITE,  round_str);
        tui_kv("Status      :", tui_state_color(status), status);
        tui_kv("Resource    :", ANSI_DIM, HC_RESOURCE);

        tui_divider();

        /* Worker table header */
        printf(BOX_V "  " ANSI_BOLD "%-3s  %-20s  %-12s  %-16s" ANSI_RESET "   " BOX_V "\n",
               "#", "Worker IP", "Status", "Won/Participated");
        printf(BOX_V "  %s  %s  %s  %s   " BOX_V "\n",
               "───", "────────────────────",
               "────────────", "────────────────");

        pthread_mutex_lock(&g_workers_mutex);
        /* Clean up dead workers inline */
        for (int i = 0; i < g_worker_count; i++) {
            const char *s;
            switch (g_workers[i].state) {
                case WS_MINING:     s = "MINING";    break;
                case WS_READY:      s = "READY";     break;
                case WS_WAITING:    s = "WAITING";   break;
                case WS_DEAD:       s = "DEAD";      break;
                default:            s = "CONN...";   break;
            }
            char stat_col[32];
            snprintf(stat_col, sizeof(stat_col), "%s%s%s",
                     tui_state_color(s), s, ANSI_RESET);
            char won_str[32];
            snprintf(won_str, sizeof(won_str), "%d / %d",
                     g_workers[i].rounds_won,
                     g_workers[i].rounds_participated);
            printf(BOX_V "  %-3d  %-20s  %-12s  %-18s " BOX_V "\n",
                   i + 1,
                   g_workers[i].ip,
                   s,
                   won_str);
        }
        if (g_worker_count == 0)
            printf(BOX_V "  %-63s " BOX_V "\n",
                   "  (no workers connected — waiting...)");
        pthread_mutex_unlock(&g_workers_mutex);

        tui_divider();

        /* Last result */
        if (g_last_bits > 0) {
            char last_bits_str[16], last_time_str[16];
            snprintf(last_bits_str, sizeof(last_bits_str), "%d", g_last_bits);
            char last_info[64];
            tui_fmt_time(last_time_str, g_last_time_ms);
            snprintf(last_info, sizeof(last_info),
                     "Diff %-4s  Time: %s  Winner: %s",
                     last_bits_str, last_time_str, g_last_winner);
            tui_kv("Last Result :", ANSI_BGREEN, last_info);

            /* Truncate stamp for display */
            char stamp_preview[48];
            snprintf(stamp_preview, sizeof(stamp_preview),
                     "%.45s...", g_last_stamp);
            tui_kv("Stamp       :", ANSI_DIM, stamp_preview);
        } else {
            tui_kv("Last Result :", ANSI_DIM, "—  (no rounds completed yet)");
        }

        tui_divider();

        /* Timers */
        char timer_line[64];
        snprintf(timer_line, sizeof(timer_line),
                 "Round: %-10s  Session: %s",
                 round_time_buf, session_time_buf);
        tui_kv("Timers      :", ANSI_BWHITE, timer_line);
        tui_kv("Results     :", ANSI_DIM, "results/results.csv");

        tui_bottom();
        fflush(stdout);

        usleep(500000);   /* 500ms */
    }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 * Difficulty Loop Thread
 * Core logic: loops difficulty from HC_START_BITS upward forever.
 * ═══════════════════════════════════════════════════════════════ */

static void* difficulty_loop(void *arg) {
    (void)arg;

    int bits = HC_START_BITS;

    while (g_running) {
        /* ── Wait until at least one worker is READY ── */
        while (g_running) {
            int ready = count_workers_in_state(WS_READY);
            if (ready > 0) break;
            sleep(1);
        }
        if (!g_running) break;

        /* ── Snapshot ready workers, mark them MINING ── */
        int active_count = 0;
        pthread_mutex_lock(&g_workers_mutex);
        for (int i = 0; i < g_worker_count; i++) {
            if (g_workers[i].state == WS_READY ||
                g_workers[i].state == WS_WAITING) {
                g_workers[i].state = WS_MINING;
                g_workers[i].rounds_participated++;
                active_count++;
            }
        }
        pthread_mutex_unlock(&g_workers_mutex);

        if (active_count == 0) { sleep(1); continue; }

        /* ── Start round ── */
        g_current_bits   = bits;
        g_current_round++;
        g_round_start_ms = now_ms();

        pthread_mutex_lock(&g_found_mutex);
        g_found_flag = 0;
        memset(g_found_stamp, 0, sizeof(g_found_stamp));
        memset(g_winner_ip,   0, sizeof(g_winner_ip));
        pthread_mutex_unlock(&g_found_mutex);

        /* ── Broadcast WORK ── */
        char work_msg[64];
        snprintf(work_msg, sizeof(work_msg),
                 "WORK|%d\n", bits);
        broadcast(work_msg);

        /* ── Wait for FOUND ── */
        pthread_mutex_lock(&g_found_mutex);
        while (!g_found_flag && g_running) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 2;   /* check every 2s even without signal */
            pthread_cond_timedwait(&g_found_cond, &g_found_mutex, &ts);
        }
        pthread_mutex_unlock(&g_found_mutex);

        if (!g_running) break;

        long time_ms = now_ms() - g_round_start_ms;

        /* ── Broadcast STOP ── */
        broadcast("STOP\n");

        /* ── Verify & record ── */
        int verified = (int)hashcash_count(g_found_stamp);

        RoundResult r;
        memset(&r, 0, sizeof(r));
        r.difficulty     = bits;
        r.bits_verified  = verified;
        r.time_ms        = time_ms;
        r.active_workers = active_count;
        r.est_hashrate   = (long)hashcash_per_sec() * active_count;
        strncpy(r.stamp,      g_found_stamp, MAX_STAMP_LEN - 1);
        strncpy(r.winner_ip,  g_winner_ip,   MAX_IP_LEN - 1);
        time_t t = time(NULL);
        struct tm *tm_info = gmtime(&t);
        strftime(r.timestamp, sizeof(r.timestamp),
                 "%Y-%m-%d %H:%M:%S", tm_info);
        log_result(&r);

        /* ── Update TUI last-result ── */
        g_last_bits    = bits;
        g_last_time_ms = time_ms;
        strncpy(g_last_stamp,  g_found_stamp, MAX_STAMP_LEN - 1);
        strncpy(g_last_winner, g_winner_ip,   MAX_IP_LEN - 1);

        /* ── Reset miners to READY for next round ── */
        pthread_mutex_lock(&g_workers_mutex);
        for (int i = 0; i < g_worker_count; i++) {
            if (g_workers[i].state == WS_MINING)
                g_workers[i].state = WS_READY;
        }
        pthread_mutex_unlock(&g_workers_mutex);

        /* ── Stabilization window: 2s for new workers to join ── */
        sleep(2);

        bits++;   /* next difficulty — no upper cap */
    }

    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    if (argc >= 2)
        g_port = atoi(argv[1]);

    signal(SIGPIPE, SIG_IGN);

    /* ── Create TCP server socket ── */
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)g_port);

    if (bind(g_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(g_server_fd, BACKLOG) < 0) {
        perror("listen"); return 1;
    }

    /* ── Init logger ── */
    logger_init();

    g_session_start_ms = now_ms();

    /* ── Launch threads ── */
    pthread_t t_accept, t_difficulty, t_tui, t_heartbeat;
    pthread_create(&t_accept,     NULL, accept_thread,    NULL);
    pthread_create(&t_difficulty, NULL, difficulty_loop,  NULL);
    pthread_create(&t_tui,        NULL, tui_thread,       NULL);
    pthread_create(&t_heartbeat,  NULL, heartbeat_thread, NULL);

    /* Main thread waits */
    pthread_join(t_difficulty, NULL);

    g_running = 0;
    close(g_server_fd);
    pthread_join(t_accept,    NULL);
    pthread_join(t_tui,       NULL);
    pthread_join(t_heartbeat, NULL);

    printf("\nCoordinator exiting.\n");
    return 0;
}
