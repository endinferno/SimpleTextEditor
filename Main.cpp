#include <cctype>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1F)

struct termios origTermios;

void die(const char* s)
{
    ::write(STDOUT_FILENO, "\x1b[2J", 4);
    ::write(STDOUT_FILENO, "\x1b[H", 3);
    ::perror(s);
    ::exit(1);
}

void DisableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios) == -1) {
        die("tcsetattr");
    }
}

void EnableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &origTermios) == -1) {
        die("tcgetattr");
    }
    atexit(DisableRawMode);

    struct termios raw = origTermios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

void EditorRefreshScreen()
{
    ::write(STDOUT_FILENO, "\x1b[2J", 4);
    ::write(STDOUT_FILENO, "\x1b[H", 3);
}

char EditorReadKey()
{
    int nread = 0;
    char c;
    while ((nread == ::read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }
    return c;
}

void EditorProcessKeyProcess()
{
    char c = EditorReadKey();

    switch (c) {
    case CTRL_KEY('q'): {
        ::write(STDOUT_FILENO, "\x1b[2J", 4);
        ::write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    }
    }
}

int main(int argc, char* argv[])
{
    EnableRawMode();

    while (true) {
        EditorRefreshScreen();
        EditorProcessKeyProcess();
    }
    return 0;
}
