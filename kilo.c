/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** data ***/
struct termios orig_termios;

/*** terminal ***/
void die(const char *s)
{
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

/*** init ***/
int main()
{
  enableRawMode();

  while (1) // loop forever
  {
    char c = '\0';

    // read input one character at a time, store in c var
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
      die("read");

    // deal with c var
    if (iscntrl(c))
    {
      // print just the number for control characters
      printf("%d\r\n", c);
    }
    else
    {
      // print number and the character for printed characters
      printf("%d ('%c')\r\n", c, c);
    }
    if (c == 'q') // quit on 'q'
      break;
  };
  return 0;
}
