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

#define TEXT_EDITOR_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1F)
#define TAB_STOP 8
#define QUIT_TIMES 3

enum EditorKey
{
    ARROW_LEFT = 'h',
    ARROW_RIGHT = 'l',
    ARROW_UP = 'k',
    ARROW_DOWN = 'j',
    BACKSPACE = 127,
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
    int renderX;
    int rowOff;
    int colOff;
    int screenRow;
    int screenCol;
    int rowNum;
    EditorRow* row;
    int dirty;
    char statusMsg[80];
    time_t statusMsgTime;
    char* fileName;
    struct termios origTermios;
};

struct EditorConfig config;

int EditorReadKey();
void EditorSetStatusMessage(const char* fmt, ...);
void EditorRowAppendString(EditorRow* row, char* s, size_t len);
void EditorInsertRow(int at, char* s, size_t len);
char* EditorPrompt(char* prompt, void (*callback)(char*, int));

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

int EditorRowCxToRx(EditorRow* row, int cursorX)
{
    int renderX = 0;
    for (int j = 0; j < cursorX; j++) {
        if (row->chars[j] == '\t') {
            renderX += (TAB_STOP - 1) - (renderX % TAB_STOP);
        }
        renderX++;
    }
    return renderX;
}

int EditorRowRxToCx(EditorRow* row, int rx)
{
    int curCursorX = 0;
    int cursorX = 0;
    for (; cursorX < row->size; cursorX++) {
        if (row->chars[cursorX] == '\t') {
            curCursorX += (TAB_STOP - 1) - (curCursorX % TAB_STOP);
        }
        curCursorX++;

        if (curCursorX > cursorX) {
            return cursorX;
        }
    }
    return cursorX;
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

void EditorInsertRow(int at, char* s, size_t len)
{
    if (at < 0 || at > config.rowNum)
        return;
    config.row = reinterpret_cast<EditorRow*>(
        realloc(config.row, sizeof(EditorRow) * (config.rowNum + 1)));
    memmove(&config.row[at + 1],
            &config.row[at],
            sizeof(EditorRow) * (config.rowNum - at));

    config.row = reinterpret_cast<EditorRow*>(
        ::realloc(config.row, sizeof(EditorRow) * (config.rowNum + 1)));

    config.row[at].size = len;
    config.row[at].chars = reinterpret_cast<char*>(::malloc(len + 1));
    ::memcpy(config.row[at].chars, s, len);
    config.row[at].chars[len] = '\0';

    config.row[at].rsize = 0;
    config.row[at].render = nullptr;
    EditorUpdateRow(&config.row[at]);

    config.rowNum++;
    config.dirty++;
}

void EditorFreeRow(EditorRow* row)
{
    free(row->render);
    free(row->chars);
}

void EditorDelRow(int at)
{
    if (at < 0 || at >= config.rowNum) {
        return;
    }
    EditorFreeRow(&config.row[at]);
    memmove(&config.row[at],
            &config.row[at + 1],
            sizeof(EditorRow) * (config.rowNum - at - 1));
    config.rowNum--;
    config.dirty++;
}

void EditorInsertNewLine()
{
    if (config.cursorX == 0) {
        EditorInsertRow(config.cursorY, "", 0);
    } else {
        EditorRow* row = &config.row[config.cursorY];
        EditorInsertRow(config.cursorY + 1,
                        &row->chars[config.cursorX],
                        row->size - config.cursorX);
        row = &config.row[config.cursorY];
        row->size = config.cursorX;
        row->chars[row->size] = '\0';
        EditorUpdateRow(row);
    }
    config.cursorY++;
    config.cursorX = 0;
}

void EditorRowDelChar(EditorRow* row, int at)
{
    if (at < 0 || at >= row->size) {
        return;
    }
    ::memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    EditorUpdateRow(row);
    config.dirty++;
}

void EditorDelChar()
{
    if (config.cursorY == config.rowNum) {
        return;
    }
    if (config.cursorX == 0 && config.cursorY == 0) {
        return;
    }
    EditorRow* row = &config.row[config.cursorY];
    if (config.cursorX > 0) {
        EditorRowDelChar(row, config.cursorX - 1);
        config.cursorX--;
    } else {
        config.cursorX = config.row[config.cursorY - 1].size;
        EditorRowAppendString(
            &config.row[config.cursorY - 1], row->chars, row->size);
        EditorDelRow(config.cursorY);
        config.cursorY--;
    }
}

void EditorRowInsertChar(EditorRow* row, int at, int c)
{
    if (at < 0 || at > row->size) {
        at = row->size;
    }
    row->chars = reinterpret_cast<char*>(::realloc(row->chars, row->size + 2));
    ::memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    EditorUpdateRow(row);
    config.dirty++;
}

void EditorRowAppendString(EditorRow* row, char* s, size_t len)
{
    row->chars =
        reinterpret_cast<char*>(realloc(row->chars, row->size + len + 1));
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    EditorUpdateRow(row);
    config.dirty++;
}

void EditorInsertChar(int c)
{
    if (config.cursorY == config.rowNum) {
        EditorInsertRow(config.rowNum, "", 0);
    }
    EditorRowInsertChar(&config.row[config.cursorY], config.cursorX, c);
    config.cursorX++;
}

char* editorRowsToString(int* buflen)
{
    int totlen = 0;
    int j;
    for (j = 0; j < config.rowNum; j++) {
        totlen += config.row[j].size + 1;
    }
    *buflen = totlen;
    char* buf = reinterpret_cast<char*>(::malloc(totlen));
    char* p = buf;
    for (j = 0; j < config.rowNum; j++) {
        memcpy(p, config.row[j].chars, config.row[j].size);
        p += config.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void EditorOpen(char* filename)
{
    free(config.fileName);
    config.fileName = strdup(filename);
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
        EditorInsertRow(config.rowNum, line, lineLen);
    }
    free(line);
    fclose(fp);
    config.dirty = 0;
}

void EditorSave()
{
    if (config.fileName == nullptr) {
        config.fileName = EditorPrompt("Save as: %s (ESC to cancel)", nullptr);
        if (config.fileName == nullptr) {
            EditorSetStatusMessage("Saved aborted");
            return;
        }
    }

    int len;
    char* buf = editorRowsToString(&len);

    int fd = open(config.fileName, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                config.dirty = 0;
                EditorSetStatusMessage("%d bytes writte to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    EditorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

void EditorFindCallback(char* query, int key)
{
    static int lastMatch = -1;
    static int direction = 1;

    if (key == '\r' || key == '\x1b') {
        lastMatch = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        lastMatch = -1;
        direction = 1;
    }

    if (lastMatch == -1) {
        direction = 1;
    }
    int current = lastMatch;
    int i;
    for (i = 0; i < config.rowNum; i++) {
        current += direction;
        if (current == -1) {
            current = config.rowNum - 1;
        } else if (current == config.rowNum) {
            current = 0;
        }
        EditorRow* row = &config.row[i];
        char* match = strstr(row->render, query);
        if (match) {
            lastMatch = current;
            config.cursorY = current;
            config.cursorY = i;
            config.cursorX = EditorRowRxToCx(row, match - row->render);
            config.rowOff = config.rowNum;
            break;
        }
    }
}

void EditorFind()
{
    int savedCx = config.cursorX;
    int savedCy = config.cursorY;
    int savedColoff = config.colOff;
    int savedRowoff = config.rowOff;

    char* query =
        EditorPrompt("Search: %s (Use ESC/Arrows/Enter)", EditorFindCallback);
    if (query) {
        free(query);
    } else {
        config.cursorX = savedCx;
        config.cursorY = savedCy;
        config.colOff = savedColoff;
        config.rowOff = savedRowoff;
    }
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
    config.renderX = 0;
    if (config.cursorY < config.rowNum) {
        config.renderX =
            EditorRowCxToRx(&config.row[config.cursorY], config.cursorX);
    }

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
        abAppend(ab, "\r\n", 2);
    }
}

void EditorDrawStatusBar(struct abuf* ab)
{
    abAppend(ab, "\x1b[7m", 4);
    char status[80], renderStatus[80];
    int len = snprintf(status,
                       sizeof(status),
                       "%.20s - %d lines %s",
                       config.fileName ? config.fileName : "[No Name]",
                       config.rowNum,
                       config.dirty ? "(modified)" : "");
    int renderLen = snprintf(renderStatus,
                             sizeof(renderStatus),
                             "%d/%d",
                             config.cursorY + 1,
                             config.rowNum);
    if (len > config.screenCol) {
        len = config.screenCol;
    }
    abAppend(ab, status, len);
    while (len < config.screenCol) {
        if (config.screenCol - len == renderLen) {
            abAppend(ab, renderStatus, renderLen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void EditorDrawMessageBar(struct abuf* ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msgLen = strlen(config.statusMsg);
    if (msgLen > config.screenCol) {
        msgLen = config.screenCol;
    }
    if (msgLen && time(nullptr) - config.statusMsgTime < 5) {
        abAppend(ab, config.statusMsg, msgLen);
    }
}

void EditorRefreshScreen()
{
    EditorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    EditorDrawRows(&ab);
    EditorDrawStatusBar(&ab);
    EditorDrawMessageBar(&ab);

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

char* EditorPrompt(char* prompt, void (*callback)(char*, int))
{
    size_t bufSize = 128;
    char* buf = reinterpret_cast<char*>(::malloc(bufSize));

    size_t bufLen = 0;
    buf[0] = '\0';

    while (true) {
        EditorSetStatusMessage(prompt, buf);
        EditorRefreshScreen();

        int c = EditorReadKey();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (bufLen != 0) {
                buf[--bufLen] = '\0';
            }
        } else if (c == '\x1b') {
            EditorSetStatusMessage("");
            if (callback) {
                callback(buf, c);
            }
            free(buf);
            return nullptr;
        } else if (c == '\r') {
            if (bufLen != 0) {
                EditorSetStatusMessage("");
                if (callback) {
                    callback(buf, c);
                }
                return buf;
            }
        } else if (!::iscntrl(c) && c < 128) {
            if (bufLen == bufSize - 1) {
                bufSize *= 2;
                buf = reinterpret_cast<char*>(::realloc(buf, bufSize));
            }
            buf[bufLen++] = c;
            buf[bufLen++] = '\0';
        }
        if (callback) {
            callback(buf, c);
        }
    }
}

void EditorSetStatusMessage(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(config.statusMsg, sizeof(config.statusMsg), fmt, ap);
    va_end(ap);
    config.statusMsgTime = time(nullptr);
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
    static int quit_times = QUIT_TIMES;

    int c = EditorReadKey();

    switch (c) {
    case '\r':
    {
        EditorInsertNewLine();
        break;
    }
    case CTRL_KEY('q'):
    {
        if (config.dirty && quit_times > 0) {
            EditorSetStatusMessage("WARNING!!! File has unsaved changes. "
                                   "Press Ctrl-Q %d more times to quit.",
                                   quit_times);
            quit_times--;
            return;
        }
        ::write(STDOUT_FILENO, "\x1b[2J", 4);
        ::write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    }
    case CTRL_KEY('s'):
    {
        EditorSave();
        break;
    }
    case HOME_KEY:
    {
        config.cursorX = 0;
        break;
    }
    case END_KEY:
    {
        if (config.cursorY < config.rowNum) {
            config.cursorX = config.row[config.cursorY].size;
        }
        break;
    }
    case CTRL_KEY('f'):
    {
        EditorFind();
    }
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
    {
        if (c == DEL_KEY) {
            EditorMoveCursor(ARROW_RIGHT);
        }
        EditorDelChar();
        break;
    }
    case PAGE_UP:
    case PAGE_DOWN:
    {
        if (c == PAGE_UP) {
            config.cursorY = config.rowOff;
        } else if (c == PAGE_DOWN) {
            config.cursorY = config.rowOff + config.screenRow - 1;
            if (config.cursorY > config.rowNum) {
                config.cursorY = config.rowNum;
            }
        }

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
    case CTRL_KEY('l'):
    case '\x1b': break;
    default:
    {
        EditorInsertChar(c);
        break;
    }
    }

    quit_times = QUIT_TIMES;
}

void InitEditor()
{
    config.cursorX = 0;
    config.cursorY = 0;
    config.renderX = 0;
    config.rowOff = 0;
    config.colOff = 0;
    config.rowNum = 0;
    config.row = nullptr;
    config.dirty = 0;
    config.fileName = nullptr;
    config.statusMsg[0] = '\0';
    config.statusMsgTime = 0;

    if (GetWindowSize(config.screenRow, config.screenCol) == -1) {
        die("GetWindowSize");
    }
    config.screenRow -= 2;
}

int main(int argc, char* argv[])
{
    EnableRawMode();
    InitEditor();
    if (argc >= 2) {
        EditorOpen(argv[1]);
    }

    EditorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit Ctrl-F = find");
    while (true) {
        EditorRefreshScreen();
        EditorProcessKeyProcess();
    }
    return 0;
}
