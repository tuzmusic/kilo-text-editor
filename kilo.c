/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
// takes the k char and sets the first 3 bytes to 0,
// which is what the ctrl key does.
#define CTRL_KEY(k) ((k)&0x1f)

/*** data ***/
struct termios orig_termios;

/*** escape sequences ***/

/*** terminal ***/
void clearScreen()
{
  // \x1b[ = escape sequence prefix
  // J = erase in display
  // 2 = entire screen
  write(STDOUT_FILENO, "\x1b[2J", 4);
  // H = reposition cursor (default 1, 1)
  write(STDOUT_FILENO, "\x1b[H", 3);
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
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode()
{
  // store the current state of the terminal in orig_termios
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    die("tcgetattr");

  atexit(disableRawMode); // restore it when we exit

  struct termios raw = orig_termios;

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

void editorDrawRows()
{
  int y;
  for (y = 0; y < 24; y++)
  {
    write(STDOUT_FILENO, "~\r\n", 3);
  }

  write(STDOUT_FILENO, "\x1b[H", 3);
}

void editorRefreshScreen()
{
  clearScreen();
  editorDrawRows();

  write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** init ***/
int main()
{
  enableRawMode();

  while (1) // loop forever
  {
    editorRefreshScreen();
    editorProcessKeypress();
  };
  return 0;
}
