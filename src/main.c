/*** includes ***/
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
/*** data ***/
struct termios orig_termios;

/*** terminal ***/
void die (const char *s) {
    write (STDOUT_FILENO, "\x1b[2J", 4);
    write (STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disable_raw_mode(void) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        die("tcsetattr");
    }
} 

void enable_raw_mode(void) { // Default mode is called 'Cooked' mode.
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        die("tcgetattr");
    }
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
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


/*** output ***/
void editor_draw_rows (void) {
    for (int i = 0; i < 24; i++) {
        write (STDOUT_FILENO, "~\r\n", 3);
    }
}

void editor_refresh_screen (void) {
    write (STDOUT_FILENO, "\x1b[2J", 4);
    write (STDOUT_FILENO, "\x1b[H", 3);
    editor_draw_rows();
     
    write (STDOUT_FILENO, "\x1b[H", 3);
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
int main(void) {
    enable_raw_mode();
    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }

    return 0;
}
