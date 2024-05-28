#include <cctype>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

int EditorReadKey();

#define TEXT_EDITOR_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1F)

enum EditorKey
{
    ARROW_LEFT = 'h',
    ARROW_RIGHT = 'l',
    ARROW_UP = 'k',
    ARROW_DOWN = 'j',
    PAGE_UP = 1000,
    PAGE_DOWN,
};

struct EditorConfig
{
    int cursorX;
    int cursorY;
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

int GetCursorPosition(int& row, int& col)
{
    char buf[32];
    unsigned int i = 0;

    if (::write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }
    while (i < sizeof(buf) - 1) {
        if (::read(STDIN_FILENO, &buf[i], 1) != 0) {
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
    if (::sscanf(&buf[2], "%d;%d", &row, &col) != 2) {
        return -1;
    }
    return 0;
}

int GetWindowSize(int& row, int& col)
{
    struct winsize ws;
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (::write(STDOUT_FILENO, "\x1b[999c\x1b[999B", 12) != 12) {
            return -1;
        }
        EditorReadKey();
        return GetCursorPosition(row, col);
    } else {
        row = ws.ws_row;
        col = ws.ws_col;
        return 0;
    }
}

struct abuf
{
    char* buf;
    int len;
};

#define ABUF_INIT { nullptr, 0 };

void abAppend(struct abuf* ab, const char* s, int len)
{
    char* newBuf = reinterpret_cast<char*>(::realloc(ab->buf, ab->len + len));
    if (newBuf == nullptr) {
        return;
    }
    ::memcpy(&newBuf[ab->len], s, len);
    ab->buf = newBuf;
    ab->len += len;
}

void abFree(struct abuf* ab)
{
    ::free(ab->buf);
}

void EditorMoveCursor(int key)
{
    switch (key) {
    case ARROW_LEFT:
        if (config.cursorX != 0) {
            config.cursorX--;
        }
        break;
    case ARROW_RIGHT:
        if (config.cursorX != config.screenCol - 1) {
            config.cursorX++;
        }
        break;
    case ARROW_UP:
        if (config.cursorY != 0) {
            config.cursorY--;
        }
        break;
    case ARROW_DOWN:
        if (config.cursorY != config.screenRow - 1) {
            config.cursorY++;
        }
        break;
    }
}

void EditorDrawRows(struct abuf* ab)
{
    for (int y = 0; y < config.screenRow; y++) {
        if (y == config.screenRow / 3) {
            char welcome[80];
            int welcomeLen = snprintf(welcome,
                                      sizeof(welcome),
                                      "TextEditor -- version %s",
                                      TEXT_EDITOR_VERSION);
            if (welcomeLen > config.screenCol) {
                welcomeLen = config.screenCol;
            }
            int padding = (config.screenCol - welcomeLen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding--) {
                abAppend(ab, " ", 1);
            }
            abAppend(ab, welcome, welcomeLen);
        } else {
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);
        if (y < config.screenRow - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void EditorRefreshScreen()
{
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    EditorDrawRows(&ab);

    char buf[32];
    snprintf(buf,
             sizeof(buf),
             "\x1b[%d;%dH",
             config.cursorY + 1,
             config.cursorX + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    ::write(STDOUT_FILENO, ab.buf, ab.len);
    abFree(&ab);
}

int EditorReadKey()
{
    int nread = 0;
    char c;
    while ((nread = ::read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }
    if (c == '\x1b') {
        char seq[3];
        if (::read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }
        if (::read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (::read(STDIN_FILENO, &seq[2], 1) != 1) {
                    return '\x1b';
                }
                if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                }
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}

void EditorProcessKeyProcess()
{
    int c = EditorReadKey();

    switch (c) {
    case CTRL_KEY('q'):
    {
        ::write(STDOUT_FILENO, "\x1b[2J", 4);
        ::write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    }
    case PAGE_UP:
    case PAGE_DOWN:
    {
        int times = config.screenRow;
        while (times--) {
            EditorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
    }
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT: EditorMoveCursor(c); break;
    }
}

void InitEditor()
{
    config.cursorX = 0;
    config.cursorY = 0;
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
