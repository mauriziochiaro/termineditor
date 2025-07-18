/**
 * termineditor.c - A terminal-based C editor with live preview
 * Author: Maurizio Chiaro
 * Date: 2025-03-13
 *
 * A simple C editor with real-time preview in a terminal window.
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
// #include "leak_tracker.h"

/* Defines */
#define MAX_LINES 1000
#define MAX_LINE_LENGTH 1000
#define TAB_SIZE 4
#define CTRL_KEY(k) ((k) & 0x1F)  // Control key combinations
#define ESC "\x1b"
#define VERSION "1.0.0"
#define SAVE_DIRECTORY "c_projects"
#define DEFAULT_FILENAME "untitled.c"
#define WELCOME_MESSAGE "HELP: Ctrl-S = Save | Ctrl-O = Open | Ctrl-F = Find | Ctrl-Q = Quit | Ctrl+] = Match Brace"

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
void editorFreeBuffer();
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
void editorOpenFilePrompt();
void ensureDirectoryExists(const char *path);
void editorSave();

/* Editor Navigation */
void editorFindMatchingBrace();

/* Output */
void editorScroll();
void editorDrawRows(struct abuf *ab);
void editorDrawStatusBar(struct abuf *ab);
void editorDrawMessageBar(struct abuf *ab);
void editorRefreshScreen();
void editorSetStatusMessage(const char *fmt, ...);

/* Search */
void editorFind();

/* Input */
char *editorPrompt(const char *prompt, void (*callback)(char *, int));
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
                    return ARROW_LEFT;
                case VK_UP:
                    return ARROW_UP;
                case VK_RIGHT:
                    return ARROW_RIGHT;
                case VK_DOWN:
                    return ARROW_DOWN;
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
    free(row->render);
}

// Libera tutte le righe e resetta lo stato del buffer
void editorFreeBuffer() {
    if (E.rows == NULL) return;

    for (int i = 0; i < E.numrows; i++) {
        editorFreeRow(&E.rows[i]);
    }
    free(E.rows);
    E.rows = NULL;
    E.numrows = 0;

    free(E.filename);
    E.filename = NULL;

    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.dirty = 0;
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    editorFreeRow(&E.rows[at]);
    memmove(&E.rows[at], &E.rows[at + 1],
            sizeof(EditorRow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty = 1;
}

// Sostituisci completamente la vecchia funzione
void editorUpdateRow(EditorRow *row) {
    free(row->render);
    int tabs = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') tabs++;
    }

    // Alloca abbastanza memoria per tab, testo e codici colore
    row->render = malloc(row->size + tabs * (TAB_SIZE - 1) + 1);
    if (row->render == NULL) die("malloc");

    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TAB_SIZE != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    // A questo punto, row->render contiene il testo con i tab espansi.
    // Questa è una semplificazione per evitare di gestire l'offset dei colori
    // durante il calcolo di rx. La soluzione più robusta è più complessa,
    // ma questa risolve il problema del posizionamento del cursore.
}

void editorRowInsertChar(EditorRow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;

    // Ensure we have enough capacity before inserting
    if (row->size + 2 > row->capacity) {  // +2 for new char + null terminator
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

    if (new_size >= row->capacity) {
        while (row->capacity < new_size + 1) row->capacity *= 2;
        row->chars = realloc(row->chars, row->capacity);
        if (!row->chars) die("realloc in editorRowAppendString");
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
    for (int j = 0; j < E.numrows; j++) {
        totlen += E.rows[j].size + 1;
    }
    *buflen = totlen;

    char *buf = malloc(totlen);
    if (!buf) return NULL;

    char *p = buf;
    for (int j = 0; j < E.numrows; j++) {
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

// Gestisce il prompt e l'apertura di un nuovo file
void editorOpenFilePrompt() {
    // Impedisce di aprire un nuovo file se ci sono modifiche non salvate
    if (E.dirty) {
        editorSetStatusMessage("WARNING! File has unsaved changes. Save first (Ctrl-S).");
        return;
    }

    // Chiede all'utente il nome del file da aprire
    char *filename = editorPrompt("Open File: %s (ESC to cancel)", NULL);
    if (filename == NULL) {
        editorSetStatusMessage("Open aborted.");
        return;
    }

    // Pulisce il buffer corrente prima di caricarne uno nuovo
    editorFreeBuffer();

    // La funzione editorOpen si occupa di caricare il file e impostare il nome
    editorOpen(filename);

    // La stringa del nome del file è stata duplicata da editorOpen, quindi possiamo liberarla
    free(filename);
}

void editorSave() {
    // Se il file non ha nome o ha il nome di default, chiedine uno nuovo.
    if (E.filename == NULL || strcmp(E.filename, DEFAULT_FILENAME) == 0) {
        // Chiama editorPrompt per ottenere il nome del file dall'utente.
        char *new_filename = editorPrompt("Save As: %s (ESC to cancel)", NULL);
        if (new_filename == NULL) {
            editorSetStatusMessage("Save aborted.");
            return;
        }
        // Libera il vecchio nome (se presente) e assegna quello nuovo.
        if (E.filename) free(E.filename);
        E.filename = new_filename;
    }

    ensureDirectoryExists(SAVE_DIRECTORY);

    char fullPath[MAX_PATH];
    snprintf(fullPath, sizeof(fullPath), "%s\\%s", SAVE_DIRECTORY, E.filename);

    int len;
    char *buf = editorRowsToString(&len);

    FILE *fp = fopen(fullPath, "w");
    if (fp != NULL) {
        if (fwrite(buf, 1, len, fp) == (size_t)len) {
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

/*** Editor Navigation ***/
void editorFindMatchingBrace() {
    if (E.cy >= E.numrows) return; // Cursore fuori dal testo

    char char_under_cursor = E.rows[E.cy].chars[E.cx];
    char open_brace = '{', close_brace = '}';
    int direction = 0; // 1 per avanti, -1 per indietro

    if (char_under_cursor == open_brace) direction = 1;
    if (char_under_cursor == close_brace) direction = -1;
    if (direction == 0) return; // Non siamo su una graffa

    int y = E.cy;
    int x = E.cx;
    int level = 1;

    while (y >= 0 && y < E.numrows) {
        x += direction;
        if (x < 0) {
            y += direction;
            if (y < 0 || y >= E.numrows) break;
            x = (direction == 1) ? 0 : E.rows[y].size - 1;
        }
        if (x >= E.rows[y].size) {
            y += direction;
            if (y >= E.numrows) break;
            x = (direction == 1) ? 0 : E.rows[y].size - 1;
        }

        if (y < 0 || y >= E.numrows) break;

        char current_char = E.rows[y].chars[x];
        if (current_char == open_brace) {
            level += (direction == 1) ? 1 : -1;
        } else if (current_char == close_brace) {
            level += (direction == 1) ? -1 : 1;
        }

        if (level == 0) {
            E.cy = y;
            E.cx = x;
            return;
        }
    }

    editorSetStatusMessage("No matching brace found");
}

/*** Search ***/

void editorFindCallback(char *query, int key) {
    static int last_match = -1; // Riga dell'ultima corrispondenza trovata (-1 se nessuna)
    static int direction = 1;   // 1 = avanti, -1 = indietro

    // Se un tasto freccia viene premuto, imposta la direzione della ricerca
    if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else if (key == '\r' || key == '\x1b') {
        // All'uscita dalla ricerca (Invio o ESC), resetta lo stato
        last_match = -1;
        direction = 1;
        return;
    } else {
        // Se l'utente digita un nuovo carattere, la ricerca riparte dall'inizio
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1) direction = 1;
    int current = last_match;

    // Cicla attraverso tutte le righe per trovare una corrispondenza
    for (int i = 0; i < E.numrows; i++) {
        current += direction;
        // Gestione del "wrap-around" (se arrivi alla fine, ricomincia dall'inizio)
        if (current == -1) current = E.numrows - 1;
        else if (current == E.numrows) current = 0;

        EditorRow *row = &E.rows[current];
        char *match = strstr(row->chars, query); // Cerca la sottostringa
        if (match) {
            last_match = current;
            E.cy = current;
            E.cx = match - row->chars; // Posiziona il cursore all'inizio della corrispondenza
            E.rowoff = E.numrows; // Forza lo scroll per rendere visibile la riga
            break;
        }
    }
}

void editorFind() {
    // Salva la posizione corrente del cursore per ripristinarla in caso di annullamento
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;

    // Avvia il prompt di ricerca, passando il callback per la logica interattiva
    char *query = editorPrompt(
        "Search: %s (ESC=Cancel | Arrows=Navigate | Enter=Confirm)",
        editorFindCallback);

    if (query == NULL) { // L'utente ha premuto ESC
        // Ripristina la posizione originale del cursore
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
    }
    free(query);
}

/*** C Syntax Highlighting ***/

// Definiamo i colori ANSI che useremo
#define COLOR_RESET   "\x1b[0m"
#define COLOR_KEYWORD "\x1b[33m" // Giallo
#define COLOR_TYPE    "\x1b[36m" // Ciano
#define COLOR_COMMENT "\x1b[38;5;70m" // Verde scuro
#define COLOR_STRING  "\x1b[33m" // Arancione
#define COLOR_NUMBER  "\x1b[31m" // Rosso
#define COLOR_PREPROC "\x1b[35m" // Magenta
#define COLOR_CONSTANT "\x1b[38;5;208m" // Arancione

// Lista di keyword e tipi del C
const char *C_KEYWORDS[] = {
    "auto", "break", "case", "const", "continue", "default", "do", "else",
    "enum", "extern", "for", "goto", "if", "register", "return", "sizeof",
    "static", "struct", "switch", "typedef", "union", "volatile", "while", NULL
};

const char *C_TYPES[] = {
    "char", "double", "float", "int", "long", "short", "signed", "unsigned",
    "void", "size_t", "FILE", "HANDLE", "DWORD", "BOOL", "boolean", NULL
};

const char *C_CONSTANTS[] = {
    "true", "false", "NULL", "BOOL", "boolean", NULL
};

// Funzione per renderizzare una riga con la sintassi C
void renderCSyntax(char *rendered_text, char *buffer, int buffer_size) {
    buffer[0] = '\0';
    int len = strlen(rendered_text);
    int pos = 0;
    int i = 0;

    // Gestione direttive preprocessore all'inizio della riga
    if (i == 0 && rendered_text[i] == '#') {
        pos += snprintf(&buffer[pos], buffer_size - pos, "%s%s%s", COLOR_PREPROC, rendered_text, COLOR_RESET);
        return;
    }

    while (i < len) {
        // Gestione dei commenti (//)
        if (i + 1 < len && rendered_text[i] == '/' && rendered_text[i+1] == '/') {
            pos += snprintf(&buffer[pos], buffer_size - pos, "%s%s%s", COLOR_COMMENT, &rendered_text[i], COLOR_RESET);
            return; // Fine della riga
        }

        // Gestione delle stringhe ("...")
        if (rendered_text[i] == '"') {
            pos += snprintf(&buffer[pos], buffer_size - pos, "%s\"", COLOR_STRING);
            i++;
            while (i < len && rendered_text[i] != '"') {
                if (pos < buffer_size - 2) buffer[pos++] = rendered_text[i++];
            }
            if (i < len) {
                if (pos < buffer_size - 2) buffer[pos++] = rendered_text[i++];
            }
            pos += snprintf(&buffer[pos], buffer_size - pos, "%s", COLOR_RESET);
            buffer[pos] = '\0';
            continue;
        }

        // Gestione dei numeri
        if (isdigit(rendered_text[i])) {
            pos += snprintf(&buffer[pos], buffer_size - pos, "%s", COLOR_NUMBER);
            while (i < len && isdigit(rendered_text[i])) {
                if (pos < buffer_size - 2) buffer[pos++] = rendered_text[i++];
            }
            pos += snprintf(&buffer[pos], buffer_size - pos, "%s", COLOR_RESET);
            buffer[pos] = '\0';
            continue;
        }

        // Gestione di keyword, tipi e costanti
        if (isalpha(rendered_text[i]) || rendered_text[i] == '_') {
            char word[256];
            int j = 0;
            while (i < len && (isalnum(rendered_text[i]) || rendered_text[i] == '_')) {
                if (j < 255) word[j++] = rendered_text[i++];
            }
            word[j] = '\0';

            int is_keyword = 0, is_type = 0, is_constant = 0;
            if ((i == len || !isalnum(rendered_text[i])) && (i-j == 0 || !isalnum(rendered_text[i-j-1]))) {
                for (int k = 0; C_KEYWORDS[k]; k++) {
                    if (strcmp(word, C_KEYWORDS[k]) == 0) { is_keyword = 1; break; }
                }
                if (!is_keyword) {
                    for (int k = 0; C_TYPES[k]; k++) {
                        if (strcmp(word, C_TYPES[k]) == 0) { is_type = 1; break; }
                    }
                }
                if (!is_keyword && !is_type) {
                    for (int k = 0; C_CONSTANTS[k]; k++) {
                        if (strcmp(word, C_CONSTANTS[k]) == 0) { is_constant = 1; break; }
                    }
                }
            }

            if (is_keyword) {
                pos += snprintf(&buffer[pos], buffer_size - pos, "%s%s%s", COLOR_KEYWORD, word, COLOR_RESET);
            } else if (is_type) {
                pos += snprintf(&buffer[pos], buffer_size - pos, "%s%s%s", COLOR_TYPE, word, COLOR_RESET);
            } else if (is_constant) {
                pos += snprintf(&buffer[pos], buffer_size - pos, "%s%s%s", COLOR_CONSTANT, word, COLOR_RESET);
            } else {
                pos += snprintf(&buffer[pos], buffer_size - pos, "%s", word);
            }
            continue;
        }

        // Carattere normale
        if (pos < buffer_size - 2) {
            buffer[pos++] = rendered_text[i++];
            buffer[pos] = '\0';
        } else {
            i++;
        }
    }
}

/*** Output ***/

void editorScroll() {
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.rows[E.cy], E.cx);
    }

    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

/* La sua unica responsabilità e' quella di disegnare il testo */
void editorDrawRows(struct abuf *ab) {
    for (int y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            // Mostra il messaggio di benvenuto solo se il file è vuoto
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                                          "C Editor -- Versione %s", VERSION);
                if (welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
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
            // Creiamo un buffer per la riga colorata
            char colored_line[MAX_LINE_LENGTH * 4];
            // Passiamo E.rows[filerow].render (con tab espansi!) alla funzione di highlighting
            renderCSyntax(E.rows[filerow].render, colored_line, sizeof(colored_line));

            int len = strlen(colored_line);
            if (len > E.coloff) {
                // Disegniamo la stringa renderizzata
                // NOTA: lo scrolling orizzontale con i colori è complesso.
                // Questa implementazione è una semplificazione.
                abAppend(ab, &colored_line[E.coloff], len - E.coloff);
            }
        }

        abAppend(ab, ESC "[K", 3); // Pulisce il resto della riga
        abAppend(ab, "\r\n", 2);
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
        // Show help if no status message
        abAppend(ab, WELCOME_MESSAGE, strlen(WELCOME_MESSAGE));
    }
}

void editorRefreshScreen() {
    // Re-check actual console dimensions each time
    int rows, cols;
    if (getWindowSize(&rows, &cols) != -1) {
        E.screenrows = rows - 2;  // leave 2 lines for status + message bars
        E.screencols = cols;
    }

    // -- CLAMPING LOGIC --
    if (E.numrows == 0) {
        E.cy = 0;
        E.cx = 0;
    } else {
        if (E.cy >= E.numrows) {
            E.cy = E.numrows - 1;
            E.cx = E.rows[E.cy].size;
        }
        int rowlen = E.rows[E.cy].size;
        if (E.cx > rowlen) {
            E.cx = rowlen;
        }
    }

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

char *editorPrompt(const char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == 127) { // Backspace
            if (buflen != 0) buf[--buflen] = '\0';
        } else if (c == '\x1b') { // Tasto Escape
            editorSetStatusMessage("");
            if (callback) callback(buf, c);
            free(buf);
            return NULL; // Annulla
        } else if (c == '\r') { // Tasto Invio
            if (buflen != 0) {
                editorSetStatusMessage("");
                if (callback) callback(buf, c);
                return buf; // Conferma
            }
        } else if (c == ARROW_UP || c == ARROW_DOWN || c == ARROW_LEFT || c == ARROW_RIGHT) {
            // Passa i tasti freccia al callback per la navigazione della ricerca
            if (callback) callback(buf, c);
        } else if (!iscntrl(c) && c < 128) {
            // Aggiungi il carattere al buffer
            if (buflen >= bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
                if (buf == NULL) die("realloc in editorPrompt");
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }

        // Esegui il callback (se esiste) ad ogni tasto premuto
        if (callback) callback(buf, c);
    }
}

void editorMoveCursor(int key) {
    EditorRow *row = (E.cy >= E.numrows || E.cy < 0) ? NULL : &E.rows[E.cy];

    switch (key) {
        case ARROW_LEFT:
            if (E.cx > 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                if (E.cy < E.numrows)
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
                E.cy++;
                E.cx = 0;
            }
            break;
    }

    // Adjust cursor if new line is shorter
    row = (E.cy >= E.numrows || E.cy < 0) ? NULL : &E.rows[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

void editorProcessKeypress() {
    static int quit_times = 2;
    int c = editorReadKey();
    // Normal or split-view modes
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

        case CTRL_KEY('o'):
            editorOpenFilePrompt();
            break;

        case 127:  // Backspace
        case CTRL_KEY('h'):
            editorDelChar();
            break;

        case CTRL_KEY('f'):
            editorFind();
            break;

        case CTRL_KEY(']'): // Scorciatoia per il brace matching
            editorFindMatchingBrace();
            break;

        // Movement
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
            E.cy = E.rowoff; // Muovi il cursore all'inizio della schermata
            break;
        case PAGE_DOWN:
            E.cy = E.rowoff + E.screenrows - 1; // Muovi alla fine
            if (E.cy > E.numrows) E.cy = E.numrows;
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
    quit_times = 2;
}

void abAppend(struct abuf *ab, const char *s, int len) {
    if (len <= 0) return;

    char *newbuf = realloc(ab->b, ab->len + len);
    if (!newbuf) return;
    memcpy(&newbuf[ab->len], s, len);
    ab->b = newbuf;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    if (ab->b) {
        free(ab->b);
        ab->b = NULL;
        ab->len = 0;
    }
}

int editorRowCxToRx(EditorRow *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
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
    E.statusmsg[0] = '\0';

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");

    // Reserve two rows for the status and message bars
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    editorInit();

    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage(WELCOME_MESSAGE);

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}

/* 
compile: gcc termineditor.c -o termineditor.exe
run: .\termineditor.exe termineditor.c
*/