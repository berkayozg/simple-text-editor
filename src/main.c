/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#define EDITOR_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}

enum EditorKey {
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
    char *chars;
} e_row;

struct editorConfig {
    int cursor_x, cursor_y;
    int row_offset;
    int screen_rows;
    int screen_cols;
    int num_rows;
    e_row *row;
    struct termios orig_termios;
};

struct abuf {
    char *b;
    int len;
};

struct editorConfig E;

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

/*** row operations  ***/
void editor_append_row(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(e_row) * (E.num_rows + 1));

    int at = E.num_rows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.num_rows++;
}
/*** file i/o ***/
void editor_open(char *file_name) {
    FILE *fp = fopen(file_name, "r");
    if (!fp) {
        die("fopen");
    }
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    while ((line_len = getline(&line, &line_cap, fp)) != -1) {
        while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
            line_len--;
        }

        editor_append_row(line, line_len);
    }
    free(line);
    fclose(fp);
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
    if (E.cursor_y < E.row_offset) {
        E.row_offset = E.cursor_y;
    }
    if (E.cursor_y >= E.row_offset + E.screen_rows) {
        E.row_offset = E.cursor_y - E.screen_rows + 1;
    }
}

void editor_draw_rows(struct abuf *ab) {
    for (int i = 0; i < E.screen_rows; i++) {
        int file_row = i + E.row_offset;
        if (file_row >= E.num_rows) {
            if (E.num_rows == 0 && i == E.screen_rows / 3) {
                char welcome[80];
                int welcome_len = snprintf(welcome, sizeof(welcome),
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
            int len = E.row[file_row].size;
            if (len > E.screen_cols) {
                len = E.screen_cols;
            }
            abuf_append(ab, E.row[file_row].chars, len);
        }

        abuf_append(ab, "\x1b[K", 3);
        if (i < E.screen_rows - 1) {
            abuf_append(ab, "\r\n", 2);
        }
    }
}

void editor_refresh_screen(void) {
    editor_scroll();

    struct abuf ab = ABUF_INIT;

    abuf_append(&ab, "\x1b[?25l", 6);
    abuf_append(&ab, "\x1b[H", 3);
    editor_draw_rows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cursor_y + 1, E.cursor_x + 1);
    abuf_append(&ab, buf, strlen(buf));

    abuf_append(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abuf_free(&ab);
}

/*** input ***/
void editor_move_cursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (E.cursor_x != 0) {
                E.cursor_x--;
            }
            break;
        case ARROW_RIGHT:
            if (E.cursor_x != E.screen_cols - 1) {
                E.cursor_x++;
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
}

void editor_process_keypress(void) {
    int c = editor_read_key();

    switch (c) {
        case CTRL_KEY('d'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case HOME_KEY:
            E.cursor_x = 0;
            break;

        case END_KEY:
            E.cursor_x = E.screen_cols - 1;
            break;

        case PAGE_UP:
        case PAGE_DOWN: {
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
    }
}

/*** init ***/
void init_editor(void) {
    E.cursor_x = 0;
    E.cursor_y = 0;
    E.row_offset = 0;
    E.num_rows = 0;
    E.row = NULL;
    if (get_window_size(&E.screen_rows, &E.screen_cols) == -1) {
        die("init");
    }
}
int main(int argc, char *argv[]) {
    enable_raw_mode();
    init_editor();
    if (argc >= 2) {
        editor_open(argv[1]);
    }
    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }

    return 0;
}
