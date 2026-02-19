/* tui.h — ANSI terminal UI helpers (pure header, no ncurses)
 * Distributed Hashcash PoW System
 */

#ifndef TUI_H
#define TUI_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ── ANSI Escape Codes ───────────────────────────────────────── */
#define ANSI_RESET      "\033[0m"
#define ANSI_BOLD       "\033[1m"
#define ANSI_DIM        "\033[2m"
#define ANSI_CLEAR      "\033[H\033[2J"   /* cursor home + clear screen */
#define ANSI_HOME       "\033[H"

/* Colors */
#define ANSI_BLACK      "\033[30m"
#define ANSI_RED        "\033[31m"
#define ANSI_GREEN      "\033[32m"
#define ANSI_YELLOW     "\033[33m"
#define ANSI_BLUE       "\033[34m"
#define ANSI_MAGENTA    "\033[35m"
#define ANSI_CYAN       "\033[36m"
#define ANSI_WHITE      "\033[37m"

/* Bright variants */
#define ANSI_BGREEN     "\033[92m"
#define ANSI_BYELLOW    "\033[93m"
#define ANSI_BCYAN      "\033[96m"
#define ANSI_BWHITE     "\033[97m"
#define ANSI_BRED       "\033[91m"

/* ── Box Drawing Characters (UTF-8) ─────────────────────────── */
#define BOX_TL  "╔"
#define BOX_TR  "╗"
#define BOX_BL  "╚"
#define BOX_BR  "╝"
#define BOX_H   "═"
#define BOX_V   "║"
#define BOX_ML  "╠"
#define BOX_MR  "╣"
#define BOX_HM  "═"   /* horizontal fill in middle divider */

#define TBL_TL  "┌"
#define TBL_TR  "┐"
#define TBL_BL  "└"
#define TBL_BR  "┘"
#define TBL_H   "─"
#define TBL_V   "│"
#define TBL_ML  "├"
#define TBL_MR  "┤"
#define TBL_TM  "┬"
#define TBL_BM  "┴"
#define TBL_X   "┼"

/* ── Box Width ───────────────────────────────────────────────── */
#define BOX_WIDTH   66    /* total inner width (between ║ chars) */

/* ── Primitives ─────────────────────────────────────────────── */

/* Clear screen and move cursor to top-left */
static inline void tui_clear(void) {
    fputs(ANSI_CLEAR, stdout);
    fflush(stdout);
}

/* Print a full-width horizontal divider line  ╠══...══╣ */
static inline void tui_divider(void) {
    printf(BOX_ML);
    for (int i = 0; i < BOX_WIDTH; i++) printf(BOX_HM);
    printf(BOX_MR "\n");
}

/* Print top border  ╔══...══╗ */
static inline void tui_top(void) {
    printf(BOX_TL);
    for (int i = 0; i < BOX_WIDTH; i++) printf(BOX_H);
    printf(BOX_TR "\n");
}

/* Print bottom border  ╚══...══╝ */
static inline void tui_bottom(void) {
    printf(BOX_BL);
    for (int i = 0; i < BOX_WIDTH; i++) printf(BOX_H);
    printf(BOX_BR "\n");
}

/* Print a centered title row  ║    TITLE    ║ */
static inline void tui_title(const char *color, const char *text) {
    int len = (int)strlen(text);
    int pad_total = BOX_WIDTH - len;
    int pad_left  = pad_total / 2;
    int pad_right = pad_total - pad_left;
    printf(BOX_V "%s%s%*s%s%*s" BOX_V "\n",
           color, ANSI_BOLD,
           pad_left + len, text,
           ANSI_RESET,
           pad_right, "");
}

/* Print a key-value row  ║  key : value            ║
 * value_color: ANSI color string for the value, or "" for default */
static inline void tui_kv(const char *key, const char *value_color,
                           const char *value) {
    char buf[BOX_WIDTH + 1];
    snprintf(buf, sizeof(buf), "  %-18s %s%-40s" ANSI_RESET,
             key, value_color, value);
    /* pad/truncate to BOX_WIDTH */
    int visible_len = (int)strlen(key) + 2 + 1 + 18 + (int)strlen(value);
    (void)visible_len; /* suppress warning; printf handles truncation */
    printf(BOX_V "  " ANSI_BOLD "%-18s" ANSI_RESET " %s%-40s" ANSI_RESET " " BOX_V "\n",
           key, value_color, value);
}

/* Print an empty row  ║                               ║ */
static inline void tui_blank(void) {
    printf(BOX_V "%*s" BOX_V "\n", BOX_WIDTH, "");
}

/* Print a raw row with exact content (caller responsible for width)
 * content must be exactly BOX_WIDTH visible chars wide */
static inline void tui_raw(const char *content) {
    printf(BOX_V "%s" BOX_V "\n", content);
}

/* ── Progress Bar ────────────────────────────────────────────── */
/* Render a progress bar of given width with percent 0–100 */
static inline void tui_progress(int percent, int bar_width) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int filled = (bar_width * percent) / 100;
    printf(ANSI_GREEN);
    for (int i = 0; i < filled; i++) printf("█");
    printf(ANSI_DIM);
    for (int i = filled; i < bar_width; i++) printf("░");
    printf(ANSI_RESET);
}

/* ── Time Formatting ─────────────────────────────────────────── */
/* Format milliseconds as HH:MM:SS into buf (at least 9 chars) */
static inline void tui_fmt_time(char *buf, long ms) {
    long s  = ms / 1000;
    long h  = s / 3600; s %= 3600;
    long m  = s / 60;   s %= 60;
    snprintf(buf, 12, "%02ld:%02ld:%02ld", h, m, s);
}

/* Format hash rate  e.g. "48.23 MH/s" into buf (at least 16 chars) */
static inline void tui_fmt_rate(char *buf, long hps) {
    if (hps >= 1000000000L)
        snprintf(buf, 20, "%.2f GH/s", hps / 1e9);
    else if (hps >= 1000000L)
        snprintf(buf, 20, "%.2f MH/s", hps / 1e6);
    else if (hps >= 1000L)
        snprintf(buf, 20, "%.2f KH/s", hps / 1e3);
    else
        snprintf(buf, 20, "%ld H/s", hps);
}

/* ── Status Color ────────────────────────────────────────────── */
static inline const char *tui_state_color(const char *status) {
    if (strncmp(status, "MINING", 6) == 0) return ANSI_BGREEN;
    if (strncmp(status, "WAIT",   4) == 0) return ANSI_BYELLOW;
    if (strncmp(status, "IDLE",   4) == 0) return ANSI_DIM;
    if (strncmp(status, "DEAD",   4) == 0) return ANSI_BRED;
    return ANSI_BWHITE;
}

#endif /* TUI_H */
