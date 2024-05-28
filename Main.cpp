#ifndef _DEFAULT_SOURCE
#    define _DEFAULT_SOURCE
#endif
#ifndef _BSD_SOURCE
#    define _BSD_SOURCE
#endif
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include <cctype>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

int EditorReadKey();

#define TEXT_EDITOR_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1F)
#define TAB_STOP 8

enum EditorKey
{
    ARROW_LEFT = 'h',
    ARROW_RIGHT = 'l',
    ARROW_UP = 'k',
    ARROW_DOWN = 'j',
    PAGE_UP = 1000,
    PAGE_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
};

typedef struct EditorRow
{
    int size;
    int rsize;
    char* chars;
    char* render;
} EditorRow;

struct EditorConfig
{
    int cursorX;
    int cursorY;
    int rowOff;
    int colOff;
    int screenRow;
    int screenCol;
    int rowNum;
    EditorRow* row;
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

void EditorUpdateRow(EditorRow* row)
{
    int tabs = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            tabs++;
        }
    }
    ::free(row->render);
    row->render = reinterpret_cast<char*>(
        ::malloc(row->size + tabs * (TAB_STOP - 1) + 1));

    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while ((idx % TAB_STOP) != 0) {
                row->render[idx++] = ' ';
            }
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void EditorAppendRow(char* s, size_t len)
{
    config.row = reinterpret_cast<EditorRow*>(
        ::realloc(config.row, sizeof(EditorRow) * (config.rowNum + 1)));

    int at = config.rowNum;
    config.row[at].size = len;
    config.row[at].chars = reinterpret_cast<char*>(::malloc(len + 1));
    ::memcpy(config.row[at].chars, s, len);
    config.row[at].chars[len] = '\0';

    config.row[at].rsize = 0;
    config.row[at].render = nullptr;
    EditorUpdateRow(&config.row[at]);

    config.rowNum++;
}

void EditorOpen(char* filename)
{
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        die("fopen");
    }

    char* line = nullptr;
    size_t lineCap = 0;
    ssize_t lineLen;
    while ((lineLen = ::getline(&line, &lineCap, fp)) != -1) {
        while (lineLen > 0 &&
               (line[lineLen - 1] == '\n' || line[lineLen - 1] == '\r')) {
            lineLen--;
        }
        EditorAppendRow(line, lineLen);
    }
    free(line);
    fclose(fp);
}

void EditorMoveCursor(int key)
{
    EditorRow* row = (config.cursorY >= config.rowNum)
                         ? nullptr
                         : &config.row[config.cursorY];
    switch (key) {
    case ARROW_LEFT:
    {
        if (config.cursorX != 0) {
            config.cursorX--;
        } else if (config.cursorY > 0) {
            config.cursorY--;
            config.cursorX = config.row[config.cursorY].size;
        }
        break;
    }
    case ARROW_RIGHT:
    {
        if (row && config.cursorX < row->size) {
            config.cursorX++;
        } else if (row && config.cursorX == row->size) {
            config.cursorY++;
            config.cursorX = 0;
        }
        break;
    }
    case ARROW_UP:
    {
        if (config.cursorY != 0) {
            config.cursorY--;
        }
        break;
    }
    case ARROW_DOWN:
    {
        if (config.cursorY < config.rowNum) {
            config.cursorY++;
        }
        break;
    }
    }

    row = (config.cursorY >= config.rowNum) ? nullptr
                                            : &config.row[config.cursorY];
    int rowLen = row ? row->size : 0;
    if (config.cursorX > rowLen) {
        config.cursorX = rowLen;
    }
}

void EditorScroll()
{
    if (config.cursorY < config.rowOff) {
        config.rowOff = config.cursorY;
    }
    if (config.cursorY >= config.rowOff + config.screenRow) {
        config.rowOff = config.cursorY - config.screenRow + 1;
    }
    if (config.cursorX < config.colOff) {
        config.colOff = config.cursorX;
    }
    if (config.cursorX >= config.colOff + config.screenCol) {
        config.colOff = config.cursorX - config.screenCol + 1;
    }
}

void EditorDrawRows(struct abuf* ab)
{
    for (int y = 0; y < config.screenRow; y++) {
        int fileRow = y + config.rowOff;
        if (fileRow >= config.rowNum) {
            if (config.rowNum == 0 && y == config.screenRow / 3) {
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
        } else {
            int len = config.row[fileRow].rsize - config.colOff;
            if (len < 0) {
                len = 0;
            }
            if (len > config.screenCol) {
                len = config.screenCol;
            }
            abAppend(ab, &config.row[fileRow].render[config.colOff], len);
        }

        abAppend(ab, "\x1b[K", 3);
        if (y < config.screenRow - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void EditorRefreshScreen()
{
    EditorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    EditorDrawRows(&ab);

    char buf[32];
    snprintf(buf,
             sizeof(buf),
             "\x1b[%d;%dH",
             (config.cursorY - config.rowOff) + 1,
             (config.cursorX - config.colOff) + 1);
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
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY;
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
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
    case HOME_KEY:
    {
        config.cursorX = 0;
        break;
    }
    case END_KEY:
    {
        config.cursorX = config.screenCol - 1;
        break;
    }
    case PAGE_UP:
    case PAGE_DOWN:
    {
        int times = config.screenRow;
        while (times--) {
            EditorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;
    }
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
    {
        EditorMoveCursor(c);
        break;
    }
    }
}

void InitEditor()
{
    config.cursorX = 0;
    config.cursorY = 0;
    config.rowOff = 0;
    config.colOff = 0;
    config.rowNum = 0;
    config.row = nullptr;
    if (GetWindowSize(config.screenRow, config.screenCol) == -1) {
        die("GetWindowSize");
    }
}

int main(int argc, char* argv[])
{
    EnableRawMode();
    InitEditor();
    if (argc >= 2) {
        EditorOpen(argv[1]);
    }

    while (true) {
        EditorRefreshScreen();
        EditorProcessKeyProcess();
    }
    return 0;
}
