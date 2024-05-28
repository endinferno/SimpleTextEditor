#include <cctype>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1F)

struct EditorConfig {
    int screenRow;
    int screenCol;
    struct termios origTermios;
};

struct EditorConfig config;

void die(const char* s)
{
    ::write(STDOUT_FILENO, "\x1b[2J", 4);
    ::write(STDOUT_FILENO, "\x1b[H", 3);
    ::perror(s);
    ::exit(1);
}

void DisableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.origTermios) == -1) {
        die("tcsetattr");
    }
}

void EnableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &config.origTermios) == -1) {
        die("tcgetattr");
    }
    atexit(DisableRawMode);

    struct termios raw = config.origTermios;
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

int GetWindowSize(int& row, int& col)
{
    struct winsize ws;
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    } else {
        row = ws.ws_row;
        col = ws.ws_col;
        return 0;
    }
}

void EditorDrawRows()
{
    for (int y = 0; y < config.screenRow; y++) {
        ::write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void EditorRefreshScreen()
{
    ::write(STDOUT_FILENO, "\x1b[2J", 4);
    ::write(STDOUT_FILENO, "\x1b[H", 3);

    EditorDrawRows();
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

void InitEditor()
{
    if (GetWindowSize(config.screenRow, config.screenCol) == -1) {
        die("GetWindowSize");
    }
}

int main(int argc, char* argv[])
{
    EnableRawMode();
    InitEditor();

    while (true) {
        EditorRefreshScreen();
        EditorProcessKeyProcess();
    }
    return 0;
}
