/*** defines ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define EDITOR_VERSION "0.0.1"
#define EDITOR_TAB_STOP 8
#define EDITOR_QUIT_TIMES 3
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}

enum EditorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN

};

/*** data ***/
typedef struct e_row {
    int size;
    int rsize;
    char *chars;
    char *render;
} e_row;

struct editorConfig {
    int cursor_x, cursor_y;
    int row_x;
    int row_offset;
    int col_offset;
    int screen_rows;
    int screen_cols;
    int num_rows;
    e_row *row;
    int dirty;
    char *file_name;
    char status_msg[80];
    time_t status_msg_time;
    struct termios orig_termios;
};

struct abuf {
    char *b;
    int len;
};

struct editorConfig E;

/*** prototypes ***/
void editor_set_status_message(const char *fmt, ...);

/*** terminal ***/
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disable_raw_mode(void) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enable_raw_mode(void) {  // Default mode is called 'Cooked' mode.
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("tcgetattr");
    }
    atexit(disable_raw_mode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(INPCK | ICRNL | BRKINT | ISTRIP | IXON);
    raw.c_cflag |= (CS8);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

int editor_read_key(void) {
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("editor_read_key");
        }
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                    return '\x1b';
                }
                if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '1':
                        return HOME_KEY;
                    case '3':
                        return DEL_KEY;
                    case '4':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        } else if (seq[0] == '0') {
            switch (seq[1]) {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}

int get_cursor_position(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            break;
        }
        if (buf[i] == 'R') {
            break;
        }
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') {
        return -1;
    }
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }

    return -1;
}

int get_window_size(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -1;
        }
        return get_cursor_position(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void editor_update_row(e_row *row) {
    int tabs = 0;
    for (int i = 0; i < row->size; i++) {
        if (row->chars[i] == '\t') {
            tabs++;
        }
    }

    free(row->render);
    row->render = malloc(row->size + (tabs * (EDITOR_TAB_STOP - 1)) + 1);

    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % EDITOR_TAB_STOP != 0) {
                row->render[idx++] = ' ';
            }
        } else {
            row->render[idx++] = row->chars[j];
        }
    }

    row->render[idx] = '\0';
    row->rsize = idx;
}

/*** row operations  ***/

int editor_row_cursorx_to_rowx(e_row *row, int cx) {
    int rx = 0;
    for (int i = 0; i < cx; i++) {
        if (row->chars[i] == '\t') {
            rx += (EDITOR_TAB_STOP - 1) - (rx % EDITOR_TAB_STOP);
        }
        rx++;
    }
    return rx;
}

void editor_append_row(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(e_row) * (E.num_rows + 1));

    int at = E.num_rows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editor_update_row(&E.row[at]);

    E.num_rows++;
    E.dirty++;
}

void editor_free_row(e_row *row) {
    free(row->render);
    free(row->chars);
}

void editor_delete_row(int at) {
    if (at < 0 || at >= E.num_rows) {
        return;
    }
    editor_free_row(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(e_row) * (E.num_rows - at - 1));
    E.num_rows--;
    E.dirty++;
}

void editor_row_insert_char(e_row *row, int at, int c) {
    if (at < 0 || at > row->size) {
        at = row->size;
    }
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editor_update_row(row);
    E.dirty++;
}

void editor_row_append_string(e_row *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    mempcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_update_row(row);
    E.dirty++;
}

void editor_row_delete_char(e_row *row, int at) {
    if (at < 0 || at >= row->size) {
        return;
    }

    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editor_update_row(row);
    E.dirty++;
}

/*** editor operations ***/

void editor_insert_char(int c) {
    if (E.cursor_y == E.num_rows) {
        editor_append_row("", 0);
    }

    editor_row_insert_char(&E.row[E.cursor_y], E.cursor_x, c);
    E.cursor_x++;
}

void editor_delete_char(void) {
    if (E.cursor_y == E.num_rows) {
        return;
    }

    if (E.cursor_x == 0 && E.cursor_y == 0) {
        return;
    }

    e_row *row = &E.row[E.cursor_y];
    if (E.cursor_x > 0) {
        editor_row_delete_char(row, E.cursor_x - 1);
        E.cursor_x--;
    } else {
        E.cursor_x = E.row[E.cursor_y - 1].size;
        editor_row_append_string(&E.row[E.cursor_y - 1], row->chars, row->size);
        editor_delete_row(E.cursor_y);
        E.cursor_y--;
    }
}

/*** file i/o ***/

char *editor_rows_to_string(int *buffer_length) {
    int total_length = 0;
    for (int i = 0; i < E.num_rows; i++) {
        total_length += E.row[i].size + 1;
    }

    *buffer_length = total_length;

    char *buf = malloc(total_length);
    char *p = buf;

    for (int i = 0; i < E.num_rows; i++) {
        memcpy(p, E.row[i].chars, E.row[i].size);
        p += E.row[i].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editor_open(char *file_name) {
    free(E.file_name);
    E.file_name = strdup(file_name);

    FILE *fp = fopen(file_name, "r");
    if (!fp) {
        die("fopen");
    }
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    while ((line_len = getline(&line, &line_cap, fp)) != -1) {
        while (line_len > 0 &&
               (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
            line_len--;
        }

        editor_append_row(line, line_len);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

void editor_save(void) {
    if (E.file_name == NULL) {
        return;
    }

    int len;
    char *buf = editor_rows_to_string(&len);

    int fd = open(E.file_name, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                E.dirty = 0;
                editor_set_status_message("%d bytes written on disk.", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editor_set_status_message("Can't save! I/O error: %s.", strerror(errno));
}

/*** append buffer ***/

void abuf_append(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) {
        return;
    }
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abuf_free(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/

void editor_scroll(void) {
    E.row_x = 0;

    if (E.cursor_y < E.num_rows) {
        E.row_x = editor_row_cursorx_to_rowx(&E.row[E.cursor_y], E.cursor_x);
    }
    if (E.cursor_y >= E.row_offset + E.screen_rows) {
        E.row_offset = E.cursor_y - E.screen_rows + 1;
    }
    if (E.row_x < E.col_offset) {
        E.col_offset = E.row_x;
    }
    if (E.row_x >= E.col_offset + E.screen_cols) {
        E.row_offset = E.row_x - E.screen_cols + 1;
    }
}

void editor_draw_rows(struct abuf *ab) {
    for (int i = 0; i < E.screen_rows; i++) {
        int file_row = i + E.row_offset;
        if (file_row >= E.num_rows) {
            if (E.num_rows == 0 && i == E.screen_rows / 3) {
                char welcome[80];
                int welcome_len =
                    snprintf(welcome, sizeof(welcome),
                             "Text editor -- version %s", EDITOR_VERSION);
                if (welcome_len > E.screen_cols) {
                    welcome_len = E.screen_cols;
                }
                int padding = (E.screen_cols - welcome_len) / 2;
                if (padding) {
                    abuf_append(ab, "~", 1);
                    padding--;
                }
                while (padding--) {
                    abuf_append(ab, " ", 1);
                }
                abuf_append(ab, welcome, welcome_len);
            } else {
                abuf_append(ab, "~", 1);
            }
        } else {
            int len = E.row[file_row].size - E.col_offset;
            if (len < 0) {
                len = 0;
            }
            if (len > E.screen_cols) {
                len = E.screen_cols;
            }
            abuf_append(ab, &E.row[file_row].chars[E.col_offset], len);
        }

        abuf_append(ab, "\x1b[K", 3);
        abuf_append(ab, "\r\n", 2);
    }
}

void editor_draw_status_bar(struct abuf *ab) {
    abuf_append(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                       E.file_name ? E.file_name : "[No Name]", E.num_rows,
                       E.dirty ? "(modified)" : "");
    int rlen =
        snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cursor_y + 1, E.num_rows);
    if (len > E.screen_cols) {
        len = E.screen_cols;
    }
    abuf_append(ab, status, len);
    while (len < E.screen_cols) {
        if (E.screen_cols - len == rlen) {
            abuf_append(ab, rstatus, rlen);
            break;
        } else {
            abuf_append(ab, " ", 1);
            len++;
        }
    }
    abuf_append(ab, "\x1b[m", 3);
    abuf_append(ab, "\r\n", 2);
}

void editor_draw_message_bar(struct abuf *ab) {
    abuf_append(ab, "\x1b[K", 3);
    int msglen = strlen(E.status_msg);
    if (msglen > E.screen_cols) {
        msglen = E.screen_cols;
    }

    if (msglen && time(NULL) - E.status_msg_time < 5) {
        abuf_append(ab, E.status_msg, msglen);
    }
}

void editor_refresh_screen(void) {
    editor_scroll();

    struct abuf ab = ABUF_INIT;

    abuf_append(&ab, "\x1b[?25l", 6);
    abuf_append(&ab, "\x1b[H", 3);

    editor_draw_rows(&ab);
    editor_draw_status_bar(&ab);
    editor_draw_message_bar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cursor_y - E.row_offset) + 1,
             (E.row_x - E.col_offset) + 1);
    abuf_append(&ab, buf, strlen(buf));

    abuf_append(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abuf_free(&ab);
}

void editor_set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, ap);
    va_end(ap);
    E.status_msg_time = time(NULL);
}

/*** input ***/
void editor_move_cursor(int key) {
    e_row *row = (E.cursor_y >= E.num_rows) ? NULL : &E.row[E.cursor_y];
    switch (key) {
    case ARROW_LEFT:
        if (E.cursor_x != 0) {
            E.cursor_x--;
        } else if (E.cursor_y > 0) {
            E.cursor_y--;
            E.cursor_x = E.row[E.cursor_y].size;
        }
        break;
    case ARROW_RIGHT:
        if (row && E.cursor_x < row->size) {
            E.cursor_x++;
        } else if (row && E.cursor_x == row->size) {
            E.cursor_y++;
            E.cursor_x = 0;
        }
        break;
    case ARROW_UP:
        if (E.cursor_y != 0) {
            E.cursor_y--;
        }
        break;
    case ARROW_DOWN:
        if (E.cursor_y != E.num_rows) {
            E.cursor_y++;
        }
        break;
    }

    row = (E.cursor_y >= E.num_rows) ? NULL : &E.row[E.cursor_y];
    int row_len = row ? row->size : 0;
    if (E.cursor_x > row_len) {
        E.cursor_x = row_len;
    }
}

void editor_process_keypress(void) {
    static int quit_times = EDITOR_QUIT_TIMES;
    int c = editor_read_key();

    switch (c) {
    case '\r':
        /* TODO */
        break;

    case CTRL_KEY('d'):
        if (E.dirty && quit_times > 0) {
            editor_set_status_message("Warning! File has unsaved changes. "
                                      "Press Ctrl+D %d more times to quit.",
                                      quit_times);
            quit_times--;
            return;
        }
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case CTRL_KEY('s'):
        editor_save();
        break;

    case HOME_KEY:
        E.cursor_x = 0;
        break;

    case END_KEY:
        if (E.cursor_y < E.num_rows) {
            E.cursor_x = E.row[E.cursor_y].size;
        }
        break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        if (c == DEL_KEY) {
            editor_move_cursor(ARROW_RIGHT);
        }

        editor_delete_char();
        break;

    case PAGE_UP:
    case PAGE_DOWN: {
        if (c == PAGE_UP) {
            E.cursor_y = E.row_offset;
        } else if (c == PAGE_DOWN) {
            E.cursor_y = E.row_offset + E.screen_rows - 1;
            if (E.cursor_y > E.num_rows) {
                E.cursor_y = E.num_rows;
            }
        }

        int times = E.screen_rows;
        while (times--) {
            editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
    } break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editor_move_cursor(c);
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
        editor_insert_char(c);
        break;
    }

    quit_times = EDITOR_QUIT_TIMES;
}

/*** init ***/
void init_editor(void) {
    E.cursor_x = 0;
    E.cursor_y = 0;
    E.row_x = 0;
    E.row_offset = 0;
    E.col_offset = 0;
    E.num_rows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.file_name = NULL;
    E.status_msg[0] = '\0';
    E.status_msg_time = 0;
    if (get_window_size(&E.screen_rows, &E.screen_cols) == -1) {
        die("init");
    }
    E.screen_rows -= 2;
}

int main(int argc, char *argv[]) {
    enable_raw_mode();
    init_editor();
    if (argc >= 2) {
        editor_open(argv[1]);
    }

    editor_set_status_message("HELP: Ctrl-S = save | Ctrl-D = quit");

    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }

    return 0;
}
