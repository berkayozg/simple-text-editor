/*** includes ***/
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
/*** data ***/
struct editorConfig {
    int screen_rows;
    int screen_cols;
    struct termios orig_termios;
};

struct abuf {
    char *b;
    int len;
};

struct editorConfig E;

/*** terminal ***/
void die (const char *s) {
    write (STDOUT_FILENO, "\x1b[2J", 4);
    write (STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disable_raw_mode(void) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
} 

void enable_raw_mode(void) { // Default mode is called 'Cooked' mode.
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
        die ("tcsetattr");
    }
}

char editor_read_key (void) {
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("editor_read_key");
        }
    }
    return c;
}

int get_cursor_position (int *rows, int *cols) {
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

int get_window_size (int *rows, int *cols) {
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

/*** append buffer ***/

void abuf_append (struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) {
        return;
    }
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
} 

void abuf_free (struct abuf *ab) {
    free(ab->b);
}

/*** output ***/
void editor_draw_rows (struct abuf *ab) {
    for (int i = 0; i < E.screen_rows; i++) {
       abuf_append(ab, "~", 1);

        if (i < E.screen_rows - 1) {
            abuf_append(ab, "\r\n", 2);
        }
    }
}

void editor_refresh_screen (void) {
    struct abuf ab = ABUF_INIT;
    
    abuf_append(&ab, "\x1b[2J", 4);
    abuf_append(&ab, "\x1b[H", 3);
    editor_draw_rows(&ab);
     
    abuf_append(&ab, "\x1b[H", 3);
    write (STDOUT_FILENO, ab.b, ab.len);
    abuf_free(&ab);
}

/*** input ***/
void editor_process_keypress (void) {
    char c = editor_read_key();

    switch (c) {
        case CTRL_KEY('d'):
            write (STDOUT_FILENO, "\x1b[2J", 4);
            write (STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** init ***/
void init_editor (void) {
    if (get_window_size(&E.screen_rows, &E.screen_cols) == -1) {
        die("init");
    }
} 
int main(void) {
    enable_raw_mode();
    init_editor();

    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }

    return 0;
}
