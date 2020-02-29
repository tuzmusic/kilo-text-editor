/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
// takes the k char and sets the first 3 bytes to 0,
// which is what the ctrl key does.
#define CTRL_KEY(k) ((k)&0x1f)

// H = reposition cursor (default 1, 1)
#define CURSOR_TO_START() (write(STDOUT_FILENO, "\x1b[H", 3))

/*** data ***/
struct editorConfig
{
  int screenrows;
  int screencols;
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

char editorReadKey()
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
  return c;
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
  if (buf[0] != '\x1b' || buf[1] != '[')
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
void editorProcessKeypress()
{
  char c = editorReadKey();
  switch (c)
  {
  case CTRL_KEY('x'):
    // quit on 'ctrl+x'
    // original tutorial has this as ctrl+q
    // but vscode is using that and it doesn't work!!!
    quit();
    break;
  }
}

/*** output ***/
// @param a pointer to the buffer we're working with
void editorDrawRows(struct abuf *ab)
{
  int y;
  for (y = 0; y < E.screenrows; y++)
  {
    abAppend(ab, "~", 1); // start the new row (in the buffer) with ~

    if (y < E.screenrows - 1)
    {
      abAppend(ab, "\r\n", 2); // go to the next line
    }
  }
}
void editorRefreshScreen()
{
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[2J", 4);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  abAppend(&ab, "\x1b'H", 3);

  write(STDOUT_FILENO, ab.b, ab.len);

  abFree(&ab);
}

/*** init ***/

void initEditor()
{
  // get window size, store in editor config, crash on error
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
}

int main()
{
  enableRawMode();
  initEditor();

  while (1) // loop forever
  {
    editorRefreshScreen();
    editorProcessKeypress();
  };
  return 0;
}
