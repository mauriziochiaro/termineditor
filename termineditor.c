/**
 * termineditor.c - A terminal-based markdown editor with live preview
 * Author: Maurizio Chiaro
 * Date: 2025-03-13
 *
 * A simple markdown editor with real-time preview in a split terminal window.
 * Windows compatible version.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

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

/* Defines */
#define MAX_LINES 1000
#define MAX_LINE_LENGTH 1000
#define TAB_SIZE 4
#define CTRL_KEY(k) ((k) & 0x1F)  // Control key combinations
#define ESC "\x1b"
#define WELCOME_MESSAGE                                                     \
    "Terminal Markdown Editor - ^S Save | ^Q Quit | ^X Cut | ^C Copy | ^V " \
    "Paste | ^F Find"
#define VERSION "1.0.0"
#define SAVE_DIRECTORY "md_files"
#define DEFAULT_FILENAME "untitled.md"

/* Data structures */
typedef struct {
    char *chars;
    char *render;  // Rendered version of the line (with tabs expanded)
    int size;
    int rsize;  // Size of the rendered line
    int capacity;
} EditorRow;

typedef struct {
    int cx, cy;             // Cursor position
    int rx;                 // Rendered cursor position (accounting for tabs)
    int rowoff;             // Row offset for scrolling
    int coloff;             // Column offset for scrolling
    int screenrows;         // Number of rows in terminal
    int screencols;         // Number of columns in terminal
    int numrows;            // Number of rows in file
    EditorRow *rows;        // Array of text rows
    char *filename;         // Current filename
    char statusmsg[80];     // Status message
    time_t statusmsg_time;  // When the status message was set
    int dirty;              // File modified flag
    int preview_mode;       // 0: edit only, 1: split view, 2: preview only
    DWORD orig_mode;        // Original console mode
    HANDLE hStdin;          // Console input handle
    HANDLE hStdout;         // Console output handle
} EditorConfig;

/* Buffer handling for screen rendering */
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

/* Global editor state */
EditorConfig E;

/* Prototypes */

/* Terminal handling */
void die(const char *s);
void disableRawMode();
void enableRawMode();
int editorReadKey();
int getWindowSize(int *rows, int *cols);

/* Buffer handling */
void editorAppendRow(char *s, size_t len);
void editorFreeRow(EditorRow *row);
void editorDelRow(int at);
void editorUpdateRow(EditorRow *row);
void editorRowInsertChar(EditorRow *row, int at, int c);
void editorRowAppendString(EditorRow *row, char *s, size_t len);
void editorRowDelChar(EditorRow *row, int at);
int editorRowCxToRx(EditorRow *row, int cx);

/* Editor operations */
void editorInsertChar(int c);
void editorInsertRow(int at, const char *s, size_t len);
void editorInsertNewline();
void editorDelChar();

/* File I/O */
char *editorRowsToString(int *buflen);
void editorOpen(char *filename);
void ensureDirectoryExists(const char *path);
void editorSave();

/* Markdown parsing */
int isHeaderLine(char *line);
int isListItem(char *line);
int isCodeBlock(char *line);
int isHorizontalRule(char *line);
void renderMarkdown(EditorRow *row, int width, char *buffer, int buffer_size);

/* Output */
void editorScroll();
void editorDrawRows(struct abuf *ab);
void editorDrawStatusBar(struct abuf *ab);
void editorDrawMessageBar(struct abuf *ab);
void editorRefreshScreen();
void editorSetStatusMessage(const char *fmt, ...);

/* Input */
void editorMoveCursor(int key);
void editorProcessKeypress();

/* Buffer utility */
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);

/* Init */
void editorInit();

/*** Terminal handling ***/

void die(const char *s) {
    DWORD written;
    WriteConsole(E.hStdout, ESC "[2J", 4, &written, NULL);  // Clear screen
    WriteConsole(E.hStdout, ESC "[H", 3, &written,
                 NULL);  // Position cursor at top-left

    perror(s);
    exit(1);
}

void disableRawMode() {
    if (!SetConsoleMode(E.hStdin, E.orig_mode)) die("SetConsoleMode");
}

void enableRawMode() {
    E.hStdin = GetStdHandle(STD_INPUT_HANDLE);
    E.hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (E.hStdin == INVALID_HANDLE_VALUE || E.hStdout == INVALID_HANDLE_VALUE)
        die("GetStdHandle");

    if (!GetConsoleMode(E.hStdin, &E.orig_mode)) die("GetConsoleMode");

    atexit(disableRawMode);

    DWORD mode = E.orig_mode;
    mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    mode |= (ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT);
    if (!SetConsoleMode(E.hStdin, mode)) die("SetConsoleMode (input)");

    // Enable ANSI escape sequences on the output
    DWORD outMode = 0;
    if (!GetConsoleMode(E.hStdout, &outMode)) die("GetConsoleMode (output)");
    outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(E.hStdout, outMode)) die("SetConsoleMode (output)");
}

int editorReadKey() {
    INPUT_RECORD ir;
    DWORD read;

    while (1) {
        if (!ReadConsoleInput(E.hStdin, &ir, 1, &read) || read != 1) continue;

        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
            CHAR ch = ir.Event.KeyEvent.uChar.AsciiChar;

            // Handle virtual key codes first
            switch (vk) {
                case VK_LEFT:
                    return ARROW_LEFT;  // 37
                case VK_UP:
                    return ARROW_UP;  // 38
                case VK_RIGHT:
                    return ARROW_RIGHT;  // 39
                case VK_DOWN:
                    return ARROW_DOWN;  // 40
                case VK_HOME:
                    return HOME_KEY;
                case VK_END:
                    return END_KEY;
                case VK_DELETE:
                    return DEL_KEY;
                case VK_PRIOR:
                    return PAGE_UP;
                case VK_NEXT:
                    return PAGE_DOWN;
                case VK_BACK:
                    return 127;  // Backspace as ASCII DEL
            }

            // If there's an actual ASCII char, return it directly
            if (ch != 0) {
                return ch;
            }
        }
    }
}

int getWindowSize(int *rows, int *cols) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (!GetConsoleScreenBufferInfo(E.hStdout, &csbi)) return -1;

    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    return 0;
}

/*** Buffer handling ***/
void editorAppendRow(char *s, size_t len) {
    E.rows = realloc(E.rows, sizeof(EditorRow) * (E.numrows + 1));
    if (E.rows == NULL) die("realloc");

    int at = E.numrows;

    E.rows[at].size = len;
    E.rows[at].capacity = len + 1;  // Initialize capacity
    E.rows[at].chars = malloc(E.rows[at].capacity);
    if (E.rows[at].chars == NULL) die("malloc");

    memcpy(E.rows[at].chars, s, len);
    E.rows[at].chars[len] = '\0';
    E.rows[at].render = NULL;
    E.rows[at].rsize = 0;

    editorUpdateRow(&E.rows[at]);
    E.numrows++;
    E.dirty = 1;
}

void editorFreeRow(EditorRow *row) {
    free(row->chars);
    free(row->render);  // Add this line
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    editorFreeRow(&E.rows[at]);
    memmove(&E.rows[at], &E.rows[at + 1],
            sizeof(EditorRow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty = 1;
}

void editorUpdateRow(EditorRow *row) {
    // Count tabs for proper rendering
    int tabs = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') tabs++;
    }

    int render_size = row->size + tabs * (TAB_SIZE - 1) + 1;
    char *render = malloc(render_size);
    if (!render) die("malloc for render");

    // Render text, expanding tabs to spaces
    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            render[idx++] = ' ';
            while (idx % TAB_SIZE != 0) render[idx++] = ' ';
        } else {
            render[idx++] = row->chars[j];
        }
    }
    render[idx] = '\0';

    // Store the rendered row
    row->render = render;
    row->rsize = idx;
}

void editorRowInsertChar(EditorRow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;

    // Ensure we have enough capacity before inserting
    if (row->size + 2 >
        row->capacity) {  // +2 for the new char and null terminator
        int new_capacity = row->capacity * 2;
        if (new_capacity == 0) new_capacity = 4;  // Minimum capacity

        char *new_chars = realloc(row->chars, new_capacity);
        if (new_chars == NULL) return;  // Handle allocation failure gracefully

        row->chars = new_chars;
        row->capacity = new_capacity;
    }

    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty = 1;
}

void editorRowAppendString(EditorRow *row, char *s, size_t len) {
    int new_size = row->size + len;

    if (new_size > row->capacity) {
        while (row->capacity < new_size) row->capacity *= 2;
        row->chars = realloc(row->chars, row->capacity);
    }

    memcpy(&row->chars[row->size], s, len);
    row->size = new_size;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty = 1;
}

void editorRowDelChar(EditorRow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty = 1;
}

/*** Editor operations ***/

void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        editorAppendRow("", 0);
    }
    editorRowInsertChar(&E.rows[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertRow(int at, const char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;

    E.rows = realloc(E.rows, sizeof(EditorRow) * (E.numrows + 1));
    if (!E.rows) die("realloc E.rows in editorInsertRow");

    // Move everything below 'at' down by one, making room for our new row
    memmove(&E.rows[at + 1], &E.rows[at], sizeof(EditorRow) * (E.numrows - at));

    E.rows[at].size = len;
    E.rows[at].chars = malloc(len + 1);
    if (!E.rows[at].chars) die("malloc in editorInsertRow");

    memcpy(E.rows[at].chars, s, len);
    E.rows[at].chars[len] = '\0';
    E.rows[at].capacity = len + 1;
    E.rows[at].render = NULL;
    E.rows[at].rsize = 0;

    editorUpdateRow(&E.rows[at]);
    E.numrows++;
    E.dirty = 1;
}

void editorInsertNewline() {
    // If the cursor is at the very top-left, insert a blank line at E.cy
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        // Otherwise, split the current row at E.cx
        EditorRow *row = &E.rows[E.cy];
        // Insert a new row below the current one, containing the text
        // that was to the right of the cursor
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);

        // Now shrink the current row to just the left part
        row = &E.rows[E.cy];  // reacquire pointer in case of realloc
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

void editorDelChar() {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    EditorRow *row = &E.rows[E.cy];
    if (E.cx > 0) {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.rows[E.cy - 1].size;
        editorRowAppendString(&E.rows[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

/*** File I/O ***/

char *editorRowsToString(int *buflen) {
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++) totlen += E.rows[j].size + 1;
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numrows; j++) {
        memcpy(p, E.rows[j].chars, E.rows[j].size);
        p += E.rows[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);
    if (E.filename == NULL) die("strdup");

    char fullPath[MAX_PATH];
    snprintf(fullPath, sizeof(fullPath), "%s\\%s", SAVE_DIRECTORY, filename);

    FILE *fp = fopen(fullPath, "r");
    if (!fp) {
        // Try opening from current directory as fallback
        fp = fopen(filename, "r");
        if (!fp) {
            // New file
            editorSetStatusMessage("New file: %s", filename);
            return;
        }
    }

    char linebuf[MAX_LINE_LENGTH];

    while (fgets(linebuf, sizeof(linebuf), fp) != NULL) {
        size_t linelen = strlen(linebuf);

        // Check if we likely hit the buffer limit (missing part of the line)
        if (linelen == MAX_LINE_LENGTH - 1 && linebuf[linelen - 1] != '\n') {
            // Read and discard the rest of the line
            int c;
            while ((c = fgetc(fp)) != EOF && c != '\n');
            editorSetStatusMessage("Warning: Line truncated (too long)");
        }

        // Remove trailing newline characters
        while (linelen > 0 &&
               (linebuf[linelen - 1] == '\n' || linebuf[linelen - 1] == '\r'))
            linelen--;

        editorAppendRow(linebuf, linelen);
    }

    fclose(fp);
    E.dirty = 0;
}

void ensureDirectoryExists(const char *path) {
    if (CreateDirectory(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return;  // Success or directory already exists
    }
    editorSetStatusMessage("Warning: Could not create directory %s", path);
}

void editorSave() {
    ensureDirectoryExists(SAVE_DIRECTORY);

    if (E.filename == NULL) {
        E.filename = strdup(DEFAULT_FILENAME);
        editorSetStatusMessage("Saving as %s", E.filename);
    }

    char fullPath[MAX_PATH];
    snprintf(fullPath, sizeof(fullPath), "%s\\%s", SAVE_DIRECTORY, E.filename);

    int len;
    char *buf = editorRowsToString(&len);

    FILE *fp = fopen(fullPath, "w");
    if (fp != NULL) {
        if (fwrite(buf, 1, len, fp) == len) {
            fclose(fp);
            free(buf);
            E.dirty = 0;
            editorSetStatusMessage("%d bytes written to disk", len);
            return;
        }
        fclose(fp);
    }

    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** Markdown parsing and rendering ***/

int isHeaderLine(char *line) {
    int i = 0;
    while (line[i] == '#') i++;
    if (i > 0 && (line[i] == ' ' || line[i] == '\t')) return i;
    return 0;
}

int isListItem(char *line) {
    int i = 0;
    while (line[i] == ' ' || line[i] == '\t') i++;
    return (line[i] == '-' || line[i] == '*' || line[i] == '+') &&
           (line[i + 1] == ' ' || line[i + 1] == '\t');
}

int isCodeBlock(char *line) { return strncmp(line, "```", 3) == 0; }

int isHorizontalRule(char *line) {
    int i = 0, count = 0;
    char c = 0;

    // Skip leading whitespace
    while (line[i] == ' ' || line[i] == '\t') i++;

    if (line[i] != '-' && line[i] != '*' && line[i] != '_') return 0;
    c = line[i];

    // Count consecutive symbols
    while (line[i] == c || line[i] == ' ' || line[i] == '\t') {
        if (line[i] == c) count++;
        i++;
    }

    return count >= 3 && line[i] == '\0';
}

void renderMarkdown(EditorRow *row, int width, char *buffer, int buffer_size) {
    // Always begin with an empty string
    buffer[0] = '\0';
    int pos = 0;  // Current write index in "buffer"

    int headerLevel = isHeaderLine(row->chars);
    if (headerLevel) {
        // Adjust style based on header level (1-6)
        if (headerLevel >= 1 && headerLevel <= 6) {
            // Use different sizes/styles for different header levels
            int fontSize =
                7 - headerLevel;  // Larger value for h1, smaller for h6
            pos += snprintf(&buffer[pos], buffer_size - pos, ESC "[%d;1m",
                            fontSize);
        }

        // Write the # marks for the header level
        for (int i = 0; i < headerLevel && pos < buffer_size - 2; i++) {
            buffer[pos++] = '#';
        }
        // Then space after them, if there's room
        if (pos < buffer_size - 2) {
            buffer[pos++] = ' ';
        }

        // Copy the text after the # + space in the original line
        pos += snprintf(&buffer[pos], buffer_size - pos, "%s",
                        &row->chars[headerLevel + 1]);

        // Reset formatting
        pos += snprintf(&buffer[pos], buffer_size - pos, ESC "[0m");
    } else if (isListItem(row->chars)) {
        // Find the bullet character
        int i = 0;
        while (row->chars[i] == ' ' || row->chars[i] == '\t') {
            if (pos < buffer_size - 2) buffer[pos++] = row->chars[i];
            i++;
        }

        // Bold the bullet
        pos += snprintf(&buffer[pos], buffer_size - pos, ESC "[1m%c" ESC "[0m ",
                        row->chars[i]);
        i++;  // skip the bullet
        i++;  // skip the space after bullet (assuming there's at least 1 space)

        // Now copy the remainder
        pos += snprintf(&buffer[pos], buffer_size - pos, "%s", &row->chars[i]);
    } else if (isHorizontalRule(row->chars)) {
        // A horizontal line across the full width
        for (int i = 0; i < width - 1 && pos < buffer_size - 2; i++) {
            buffer[pos++] = '-';
        }
        if (pos < buffer_size) buffer[pos] = '\0';
    } else if (isCodeBlock(row->chars)) {
        // Show the triple backticks in cyan
        pos += snprintf(&buffer[pos], buffer_size - pos,
                        ESC "[36m```%s" ESC "[0m", &row->chars[3]);
    } else {
        // Normal text, but parse inline **bold**, *italic*, `code`
        int i = 0;
        int in_bold = 0, in_italic = 0, in_code = 0;

        while (i < row->size && pos < buffer_size - 2) {
            // Check for bold "**"
            if (row->chars[i] == '*' && i + 1 < row->size &&
                row->chars[i + 1] == '*') {
                if (!in_bold) {
                    // Turn bold on
                    pos += snprintf(&buffer[pos], buffer_size - pos, ESC "[1m");
                } else {
                    // Turn bold off
                    pos += snprintf(&buffer[pos], buffer_size - pos, ESC "[0m");
                }
                in_bold = !in_bold;
                i += 2;
            }
            // Check for italic "*"
            else if (row->chars[i] == '*') {
                if (!in_italic) {
                    pos += snprintf(&buffer[pos], buffer_size - pos,
                                    ESC "[3m");  // Italic on
                } else {
                    pos += snprintf(&buffer[pos], buffer_size - pos,
                                    ESC "[0m");  // Italic off
                }
                in_italic = !in_italic;
                i++;
            }
            // Check for inline code "`"
            else if (row->chars[i] == '`') {
                if (!in_code) {
                    pos += snprintf(&buffer[pos], buffer_size - pos,
                                    ESC "[36m");  // code color
                } else {
                    pos += snprintf(&buffer[pos], buffer_size - pos,
                                    ESC "[0m");  // reset
                }
                in_code = !in_code;
                i++;
            } else {
                // Normal character
                if (pos < buffer_size - 2) buffer[pos++] = row->chars[i];
                i++;
            }
        }

        // Close any unclosed formatting
        if ((in_bold || in_italic || in_code) && pos < buffer_size - 5) {
            pos += snprintf(&buffer[pos], buffer_size - pos, ESC "[0m");
        }
    }

    // Make sure it's null-terminated
    buffer[buffer_size - 1] = '\0';
}

/*** Output ***/

void editorScroll() {
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.rows[E.cy], E.cx);
    }

    // If cursor is above the visible window, scroll up to cursor
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }

    // If cursor is below the visible window, scroll down to cursor
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }

    // If cursor is left of the visible window, scroll left to cursor
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }

    // If cursor is right of the visible window, scroll right to cursor
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab) {
    int y;
    int screen_width = E.screencols;
    int edit_width = E.preview_mode == 1
                         ? E.screencols / 2 - 2
                         : E.screencols;  // Adjust for separator

    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;

        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welcomelen =
                    snprintf(welcome, sizeof(welcome),
                             "Markdown Editor -- version %s", VERSION);
                if (welcomelen > edit_width) welcomelen = edit_width;

                int padding = (edit_width - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);

                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            // Se siamo in modalità preview-only, mostro solo il markdown
            if (E.preview_mode == 2) {
                char rendered[MAX_LINE_LENGTH * 3];
                renderMarkdown(&E.rows[filerow], E.screencols, rendered, sizeof(rendered));
                abAppend(ab, rendered, strlen(rendered));
            } else {
                // Codice normale per edit-only o il lato sinistro del
                // split-view
                int len = E.rows[filerow].rsize - E.coloff;
                if (len < 0) len = 0;
                if (len > edit_width) len = edit_width;

                if (len > 0) {
                    if (filerow < E.numrows) {
                        abAppend(ab, &E.rows[filerow].render[E.coloff], len);
                    }
                }
            }
        }

        abAppend(ab, ESC "[K", 3);  // Clear to end of line

        // If in split view, draw preview
        if (E.preview_mode == 1) {
            abAppend(ab, " | ", 3);  // Separator

            if (filerow < E.numrows) {
                char rendered[MAX_LINE_LENGTH * 3];  // Extra space for formatting sequences
                renderMarkdown(&E.rows[filerow], edit_width, rendered, sizeof(rendered));
                abAppend(ab, rendered, strlen(rendered));
            }

            abAppend(ab, ESC "[K", 3);  // Clear to end of line
        }

        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, ESC "[7m", 4);  // Inverted colors

    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s %s",
                       E.filename ? E.filename : "[No Name]",
                       E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

    if (len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);

    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }

    abAppend(ab, ESC "[m", 3);  // Reset formatting
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, ESC "[K", 3);  // Clear the message bar

    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);

    if (E.statusmsg[0] == '\0') {
        // Show help information if no status message
        abAppend(ab, WELCOME_MESSAGE, strlen(WELCOME_MESSAGE));
    }
}

void editorRefreshScreen() {
    DWORD written;
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, ESC "[?25l", 6);  // Hide cursor
    abAppend(&ab, ESC "[H", 3);     // Position cursor at top-left

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), ESC "[%d;%dH", (E.cy - E.rowoff) + 1,
             (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, ESC "[?25h", 6);  // Show cursor

    WriteConsole(E.hStdout, ab.b, ab.len, &written, NULL);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** Input ***/
void editorMoveCursor(int key) {
    EditorRow *row = (E.cy >= E.numrows || E.cy < 0) ? NULL : &E.rows[E.cy];

    switch (key) {
        case ARROW_LEFT:
            if (E.cx > 0) {
                E.cx--;
            } else if (E.cy > 0) {
                // Move to end of previous line
                E.cy--;
                if (E.cy < E.numrows)  // Make sure we don't go out of bounds
                    E.cx = E.rows[E.cy].size;
                else
                    E.cx = 0;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows - 1) E.cy++;
            break;
        case ARROW_UP:
            if (E.cy > 0) E.cy--;
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size && E.cy < E.numrows - 1) {
                // Move to beginning of next line
                E.cy++;
                E.cx = 0;
            }
            break;
    }

    // Adjust cursor if the line it's now on is shorter than previous line
    row = (E.cy >= E.numrows || E.cy < 0) ? NULL : &E.rows[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

void editorProcessKeypress() {
    static int quit_times = 2;
    int c = editorReadKey();

    // Se siamo in modalità preview-only, gestiamo solo alcuni tasti
    if (E.preview_mode == 2) {
        switch (c) {
            case CTRL_KEY('q'):
                if (E.dirty && quit_times > 0) {
                    editorSetStatusMessage(
                        "WARNING! File has unsaved changes. "
                        "Press Ctrl-Q %d more times to quit.",
                        quit_times);
                    quit_times--;
                    return;
                }
                {
                    DWORD written;
                    WriteConsole(E.hStdout, ESC "[2J", 4, &written, NULL);
                    WriteConsole(E.hStdout, ESC "[H", 3, &written, NULL);
                    exit(0);
                }
                break;
            case CTRL_KEY('p'):
                E.preview_mode = (E.preview_mode + 1) % 3;
                editorSetStatusMessage("Preview mode: %s",
                                       (E.preview_mode == 0) ? "edit only"
                                       : (E.preview_mode == 1)
                                           ? "split view"
                                           : "preview only");
                break;
            case ARROW_UP:
            case ARROW_DOWN:
            case PAGE_UP:
            case PAGE_DOWN:
                editorMoveCursor(c);
                break;
            default:
                // Ignora altri tasti in modalità preview-only
                return;
        }
    } else {
        switch (c) {
            case '\r':
                editorInsertNewline();
                break;

            case CTRL_KEY('q'):
                if (E.dirty && quit_times > 0) {
                    editorSetStatusMessage(
                        "WARNING! File has unsaved changes. "
                        "Press Ctrl-Q %d more times to quit.",
                        quit_times);
                    quit_times--;
                    return;
                }
                {
                    DWORD written;
                    WriteConsole(E.hStdout, ESC "[2J", 4, &written, NULL);
                    WriteConsole(E.hStdout, ESC "[H", 3, &written, NULL);
                    exit(0);
                }
                break;

            case CTRL_KEY('s'):
                editorSave();
                break;

            case CTRL_KEY('p'):
                E.preview_mode = (E.preview_mode + 1) % 3;
                editorSetStatusMessage("Preview mode: %s",
                                       (E.preview_mode == 0) ? "edit only"
                                       : (E.preview_mode == 1)
                                           ? "split view"
                                           : "preview only");
                break;

            // Backspace
            case 127:
            case CTRL_KEY('h'):
                editorDelChar();
                break;

            case CTRL_KEY('f'):
                editorSetStatusMessage("Search function not implemented yet");
                break;

            // Handle special keys from our enum
            case ARROW_LEFT:
            case ARROW_RIGHT:
            case ARROW_UP:
            case ARROW_DOWN:
                editorMoveCursor(c);
                break;

            case HOME_KEY:
                E.cx = 0;
                break;
            case END_KEY:
                if (E.cy < E.numrows) {
                    E.cx = E.rows[E.cy].size;
                }
                break;
            case PAGE_UP:
                E.cy = E.rowoff;
                {
                    int times = E.screenrows;
                    while (times--) editorMoveCursor(ARROW_UP);
                }
                break;
            case PAGE_DOWN:
                E.cy = E.rowoff + E.screenrows - 1;
                if (E.cy > E.numrows) E.cy = E.numrows;
                {
                    int times = E.screenrows;
                    while (times--) editorMoveCursor(ARROW_DOWN);
                }
                break;
            case DEL_KEY:
                editorMoveCursor(ARROW_RIGHT);
                editorDelChar();
                break;

            case '\x1b':  // Escape
                // ignore
                break;

            default:
                editorInsertChar(c);
                break;
        }
    }
    quit_times = 2;
}

void abAppend(struct abuf *ab, const char *s, int len) {
    if (len <= 0) return;

    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

int editorRowCxToRx(EditorRow *row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t') rx += (TAB_SIZE - 1) - (rx % TAB_SIZE);
        rx++;
    }
    return rx;
}

/*** Init ***/

void editorInit() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.rows = NULL;
    E.filename = NULL;
    E.dirty = 0;
    E.preview_mode = 0;
    E.statusmsg[0] = '\0';

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");

    // Reserve two rows for the status and message bars.
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    editorInit();

    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage(
        "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-P = preview mode");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}