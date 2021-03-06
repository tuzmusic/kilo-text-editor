/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

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
#define KILO_VERSION "0.0.1"

// takes the k char and sets the first 3 bytes to 0,
// which is what the ctrl key does.
#define CTRL_KEY(k) ((k)&0x1f)

// H = reposition cursor (default 1, 1)
#define CURSOR_TO_START() (write(STDOUT_FILENO, "\x1b[H", 3))

enum escapes
{
  ESC_SEQ = '\x1b',
  ARR_UP_SEQ = 'A',
  ARR_DOWN_SEQ = 'B',
  ARR_RIGHT_SEQ = 'C',
  ARR_LEFT_SEQ = 'D',
  PAGE_UP_SEQ = '5',
  PAGE_DOWN_SEQ = '6',
  HOME_KEY_SEQ_H = 'H',
  END_KEY_SEQ_F = 'F',
};

// use numbers outside the character range
// to stand in for special keys
enum editorKey
{
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  DELETE_KEY
};

/*** data ***/

typedef struct erow
{
  int size;
  char *chars;
} erow;

struct editorConfig
{
  int cx, cy; // cursor position
  int screenrows;
  int screencols;
  int numrows;
  erow row;
  struct termios orig_termios;
};

struct editorConfig E;

//region *** terminal ***/
void clearScreen()
{
  // \x1b[ = escape sequence prefix
  // J = erase in display
  // 2 = entire screen
  write(STDOUT_FILENO, "\x1b[2J", 4);
  CURSOR_TO_START();
}

void quit()
{
  clearScreen();
  exit(0);
}

void die(const char *s)
{
  clearScreen();
  perror(s);
  exit(1);
}

void disableRawMode()
{
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode()
{
  // store the current state of the terminal in orig_termios
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");

  atexit(disableRawMode); // restore it when we exit

  struct termios raw = E.orig_termios;

  // ICRNL:  disable ctrl+m & enter (carriage return, new line)
  // IXON: disable flow control (ctrl+s/q)
  raw.c_lflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

  // disable output processing (adding carriage return to newline)
  raw.c_oflag &= ~(OPOST);

  // disable some other stuff
  raw.c_cflag |= (CS8);

  // disable echo
  // disable canonical mode (which reads by line, so now we read by char)
  // disable ctrl+v/o
  // disable signals (ctrl+c/z)
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  // set timeout values
  raw.c_cc[VMIN] = 0;  // read returns with ≥ 0 bytes input
  raw.c_cc[VTIME] = 1; // return from read in 100ms

  // write to raw
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey()
{
  int nread;
  char c;
  // read input one character at a time, store in c var
  /* read() gets the character and returns the length of what it got.
  != 1 means that as soon as it gets any character it ends the loop
  and returns the character */
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
  {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  if (c == ESC_SEQ) // if an escape char was typed
  {
    char seq[3];
    // read next char into seq[0]
    // if there's no next char, then it's just the actual escape key
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return ESC_SEQ;
    // store the remaining chars to seq?
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return ESC_SEQ;

    if (seq[0] == '[') // escape seqs start with [
    {
      // if next char is a number
      if (seq[1] >= '0' && seq[1] <= '9')
      {
        // read the next char into seq[2]
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return ESC_SEQ;
        if (seq[2] == '~') // if the 3rd char of the escape is ~
        {
          switch (seq[1])
          {
          case '1': // <esc>[1~
            return HOME_KEY;
          case '3': // <esc>[3~
            return DELETE_KEY;
          case '4': // <esc>[4~
            return END_KEY;
          case '5': // <esc>[5~
            return PAGE_UP;
          case '6': // <esc>[6~
            return PAGE_DOWN;
          case '7': // <esc>[7~
            return HOME_KEY;
          case '8': // <esc>[8~
            return END_KEY;
          }
        }
      }
      else if (seq[0] == 'O')
      {
        switch (seq[1])
        {
        case HOME_KEY_SEQ_H:
          return HOME_KEY;
        case END_KEY_SEQ_F:
          return END_KEY;
        }
      }
      else
      {
        // if the escape char is an arrow key, return the corresponding
        // arrow key keynum value
        switch (seq[1])
        {
        case ARR_UP_SEQ:
          return ARROW_UP;
        case ARR_DOWN_SEQ:
          return ARROW_DOWN;
        case ARR_RIGHT_SEQ:
          return ARROW_RIGHT;
        case ARR_LEFT_SEQ:
          return ARROW_LEFT;
        }
      }
    }
    return ESC_SEQ;
  }
  else
  {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols)
{
  // initialize a buffer and its index
  char buf[32];
  unsigned int i = 0;

  // get the cursor position (6n) and write it to STDIN
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1)
  {
    // write a char from stdin (the cursor position)
    // to the buffer, up to the size of the buffer
    // if there's nothing more in the buffer, exit
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    // if it's R, exit
    if (buf[i] == 'R')
      break;
    i++;
  }

  // close the buffer
  buf[i] = '\0';

  // safety check
  if (buf[0] != ESC_SEQ || buf[1] != '[')
    return -1;

  // print the cursor position, from the buffer
  // start at index 2, skipping escape and bracket
  // and pass the rest (a string in the format rows;cols) to sscanf
  // parse the string and put the values into rows and cols
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols)
{
  struct winsize ws;

  if (
      // get window size, store in ws
      ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 ||
      // safety check
      ws.ws_col == 0)
  {
    // if the above didn't work, we'll have to get the window size ourselves.

    // move cursor to the right and then down, both by 999
    // the C & B escape commands don't let the cursor go off the screen,
    // so this will move the cursor to the bottom right of the screen.
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;

    // get the position (passing the pointers)
    return getCursorPosition(rows, cols);
  }
  else
  {
    // if we're here we got a window size and we can pass it back to the pointers
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** file i/o ***/

void editorOpen(char *filename)
{
  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecapacity = 0;
  ssize_t linelen;
  // read line at the line pointer
  // set linecapacity to the (previously unknown) size of the line (?)
  // return the line's length
  linelen = getline(&line, &linecapacity, fp);

  if (linelen != -1)
  {
    // remove line breaks from the end of the line
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;

    E.row.size = linelen;
    E.row.chars = malloc(linelen + 1);  // allocate memory for the line
    memcpy(E.row.chars, line, linelen); // store the line
    E.row.chars[linelen] = '\0';        // close at the end of the line
    E.numrows = 1;
  }
  free(line);
  fclose(fp);
}

/*** append buffer ***/

struct abuf
{
  char *b; // a pointer to the buffer (the first char in the buffer)
  int len;
};

#define ABUF_INIT \
  {               \
    NULL, 0       \
  }

// takes a buffer to append to
// and a pointer to the string to append,
// and its length
void abAppend(struct abuf *ab, const char *s, int len)
{
  // allocate memory
  char *new = realloc(
      ab->b,          // starting at the start of the buffer (containing its orig contents?)
      ab->len + len); // and equal to the size it will be with the new string
  // add the new characters to the end of the buffer
  memcpy(&new[ab->len], s, len);
  // store the new buffer
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab)
{
  free(ab->b);
}

/*** input ***/

void editorMoveCursor(int32_t key)
{
  switch (key)
  {
  case ARROW_LEFT:
    if (E.cx != 0)
      E.cx--;
    break;
  case ARROW_RIGHT:
    if (E.cx != E.screencols - 1)
      E.cx++;
    break;
  case ARROW_UP:
    if (E.cy != 0)
      E.cy--;
    break;
  case ARROW_DOWN:
    if (E.cy != E.screenrows - 1)
      E.cy++;
    break;
  }
}

void editorProcessKeypress()
{
  int c = editorReadKey();

  switch (c)
  {
  case CTRL_KEY('x'): // QUIT
    // original tutorial has this as ctrl+q
    // but vscode is using that and it doesn't work!!!
    quit();
    break;

  case HOME_KEY:
    E.cx = 0;
    break;

  case END_KEY:
    E.cx = E.screencols - 1;
    break;

  case PAGE_UP:
  case PAGE_DOWN:
  { // create block scope for the times variable
    // move until we get to the top or bottom of the screen
    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  }
  break;

  case ARROW_LEFT:
  case ARROW_RIGHT:
  case ARROW_UP:
  case ARROW_DOWN:
    editorMoveCursor(c);
    break;
  }
}

/*** output ***/
void printWelcome(struct abuf *ab)
{
  char welcome[80]; // allocate a string for the welcome message
  int welcomelen = snprintf(welcome, sizeof(welcome),
                            "Kilo editor -- version %s", KILO_VERSION);
  if (welcomelen > E.screencols)
    welcomelen = E.screencols; // truncate if not enough room

  int padding = (E.screencols - welcomelen) / 2;
  if (padding) // start the welcome line with a tilde
  {
    abAppend(ab, "~", 1);
    padding--;
  }
  // add the left padding
  while (padding--)
    abAppend(ab, " ", 1);

  // add welcome to ab in the length of welcomelen
  abAppend(ab, welcome, welcomelen);
}

void editorDrawRows(struct abuf *ab)
{
  int y;
  for (y = 0; y < E.screenrows; y++)
  {
    if (y >= E.numrows)
    {
      if (E.numrows == 0 && y == E.screenrows / 3) // print the welcome message 1/3 down the screen, if we haven't loaded a file
      {
        printWelcome(ab);
      }
      else
      {
        abAppend(ab, "~", 1); // start the new row (in the buffer) with ~
      }
    }
    else
    {
      int len = E.row.size;
      if (len > E.screencols)
        len = E.screencols;           // limit line length to screen width
      abAppend(ab, E.row.chars, len); // add line to the buffer
    }

    abAppend(ab, "\x1b[K", 3); // clear the rest of the row

    if (y < E.screenrows - 1)
    {
      abAppend(ab, "\r\n", 2); // go to the next line
    }
  }
}

void editorRefreshScreen()
{
  struct abuf ab = ABUF_INIT;

  // TODO: replace these with clearScreen?
  abAppend(&ab, "\x1b[?25l", 6); // remove cursor from the screen
  abAppend(&ab, "\x1b[H", 3);    // move cursor to start

  editorDrawRows(&ab); // draw the tildes

  char buf[32];

  // move the cursor to the stored position
  // terminal uses 1-indexed values
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6); // add cursor back to screen

  write(STDOUT_FILENO, ab.b, ab.len); // write from the buffer

  abFree(&ab); // free up the buffer
}

/*** init ***/

void initEditor()
{
  E.cx = 0;
  E.cy = 0;
  E.numrows = 0;

  // get window size, store in editor config, crash on error
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
}

int main(int argc, char *argv[])
{
  enableRawMode();
  initEditor();

  // if we have a filename, open the file.
  // (else, we'll have a blank file)
  if (argc >= 2)
    editorOpen(argv[1]);

  while (1) // loop forever
  {
    editorRefreshScreen();
    editorProcessKeypress();
  };
  return 0;
}
