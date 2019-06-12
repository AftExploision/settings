#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <memory.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#define STAT_GOOD 0
#define STAT_BADFLAG -1
#define STAT_BADTIME -2
#define STAT_BADEXEC -3
#define STAT_BADWAIT -4
#define STAT_OVERMEM -5

#define STAT_NOLOCKDIR 1
#define STAT_LOCKNOTDIR 2
#define STAT_LOCKEXISTS 3
#define STAT_NOLOGDIR 4
#define STAT_LOGNOTDIR 5
#define STAT_BADRLIMIT 6
#define STAT_BADFIRSTFORK 7
#define STAT_BADSECONDFORK 8
#define STAT_BADSID 9
#define STAT_BADOPEN 10
#define STAT_BADCREATE 11
#define STAT_BADDUP 12
#define STAT_NOPROP 13
#define STAT_NULLXDATA 14
#define STAT_BADINTACT 15
#define STAT_BADADDSET 16
#define STAT_BADEMPTYSET 17
#define STAT_BADPROC 18
#define STAT_BADQUITACT 19
#define STAT_BADTERMACT 20
#define STAT_BADABRTACT 21
#define STAT_BADPCLOSE 22
#define STAT_NULLPIPE 23
#define STAT_BADFSCANF 24
#define STAT_BADLENGTH 25
#define STAT_NULLDISP 26
#define STAT_NULLBUF 27
#define STAT_BADSCRIPT 28

const char *SEPERATOR = " | ";

const char *TIMEFMT = "%a %d %b %y, %H:%M";

const char *CMDS[] = {
  "@TIME@",
  "$HOME/.scripts/getbattery.sh",
  "$HOME/.scripts/getvol.sh",
  NULL
};

const char *PREFIXES[] = {
  " ",
  "Bat: ",
  "Vol: ",
  NULL
};

const char *SUFFIXES[] = {
  "",
  "",
  "",
  NULL
};

const char *LOCKDIR = "/var/run/dwmbar";
const char *LOCKFILE = "/var/run/dwmbar/bar.pid";
const char *FDDIR = "/proc/self/fd/";
const char *LOGDIR = "/var/log/dwmbar";
const char *LOGFILE = "/var/log/dwmbar/bar.log";

struct XData {
  Display *disp;
  Window root;
};

int logfd = -1;
atomic_bool run = true;
sigset_t sset;

int close_fds();
int connect_X(struct XData *xd);
int daemonize();
void disconnect_X(struct XData *xd);
int get_script(char *buf, size_t maxlen, unsigned int num);
int get_time(char *buf, size_t maxlen);
int main(int argc, char **argv);
int make_signals();
void reset_sigs();
int set_text(char *txt);
void sighandler(int num);

int close_fds()
{
  struct stat st_info;
  if (stat(LOCKDIR, &st_info) == -1) {
    return STAT_NOLOCKDIR;
  }

  if ((st_info.st_mode & 0770000) != 0040000) {
    return STAT_LOCKNOTDIR;
  }

  if (stat(LOGDIR, &st_info) == -1) {
    return STAT_NOLOGDIR;
  }

  if ((st_info.st_mode & 0770000) != 0040000) {
    return STAT_LOGNOTDIR;
  }

  if (stat(LOCKFILE, &st_info) == 0) {
    return STAT_LOCKEXISTS;
  }

  DIR *d = NULL;
  if (stat(FDDIR, &st_info) == 0) {
    d = opendir(FDDIR);
  }

  if (d != NULL) {
    struct dirent *entry = NULL;
    while ((entry = readdir(d)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }
      int fd = atoi(entry->d_name);
      if (fd > 2) {
        close(fd);
      }
    }

    closedir(d);
    return STAT_GOOD;
  }

  struct rlimit lim;
  int status = getrlimit(RLIMIT_NOFILE, &lim);
  if (status != 0) {
    return STAT_BADRLIMIT;
  }

  for (rlim_t i=3; i < lim.rlim_cur; ++i) {
    close(i);
  }

  return STAT_GOOD;
}

int connect_X(struct XData *xd)
{
  if (xd == NULL) {
    return STAT_NULLXDATA;
  }

  xd->disp = XOpenDisplay(NULL);
  if (xd->disp == NULL) {
    return STAT_NULLDISP;
  }

  xd->root = XDefaultRootWindow(xd->disp);

  return STAT_GOOD;
}

int daemonize()
{
  int status = close_fds();
  if (status != 0) {
    return status;
  }
  reset_sigs();

  pid_t fres = fork();

  if (fres == (pid_t)-1) {
    return STAT_BADFIRSTFORK;
  }

  if (fres > 0) {
    exit(0);
  }

  fres = setsid();
  if (fres == (pid_t)-1) {
    return STAT_BADSID;
  }

  fres = fork();
  if (fres == (pid_t)-1) {
    return STAT_BADSECONDFORK;
  }

  if (fres != 0) {
    exit(0);
  }

  umask(0);

  close(STDIN_FILENO);

  FILE *lockfile = fopen(LOCKFILE, "w");
  if (lockfile == NULL) {
    return STAT_BADCREATE;
  }

  fprintf(lockfile, "%d", getpid());
  fclose(lockfile);

  struct stat st_info;
  if (stat(LOGFILE, &st_info) == -1) {
    creat(LOGFILE, 0777);
  }

  logfd = open(LOGFILE, O_WRONLY|O_TRUNC);
  if (logfd == -1) {
    perror("open log");
    return STAT_BADOPEN;
  }

  int mydup = dup2(logfd, STDOUT_FILENO);
  if (mydup == -1) {
    return STAT_BADDUP;
  }

  mydup = dup2(logfd, STDERR_FILENO);
  if (mydup == -1) {
    return STAT_BADDUP;
  }

  printf("Starting daemon...\n");
  printf("PID: %d\nUID: %d\nGID: %d\n", getpid(), getuid(), getgid());
  printf("EUID: %d\nEGID: %d\n", geteuid(), getegid());

  printf("Changing to /\n");
  chdir("/");
  fflush(stdout);
  return STAT_GOOD;
}

void disconnect_X(struct XData *xd)
{
  if (xd == NULL) {
    return;
  }

  if (xd->disp == NULL) {
    return;
  }

  XCloseDisplay(xd->disp);
  xd->disp = NULL;
}

int get_script(char *buf, size_t maxlen, unsigned int num)
{
  if (buf == NULL) {
    return STAT_NULLBUF;
  }

  if (num > (sizeof(CMDS) / sizeof(CMDS[0])) - 1) {
    return STAT_BADSCRIPT;
  }

  FILE *scr = popen(CMDS[num], "r");
  if (scr == NULL) {
    return STAT_NULLPIPE;
  }

  size_t bufcnt = strlen(PREFIXES[num]);
  if (maxlen < bufcnt) {
    return STAT_BADLENGTH;
  }

  strcpy(buf, PREFIXES[num]);

  char buffer[30];
  memset(buffer, 0, 30);
  int fret = 0;
  int cont = 0;

  fscanf(scr, "%s", &buffer[0]);
  while (fret != EOF) {
    bufcnt += strlen(buffer) + cont;
    if (bufcnt > maxlen) {
      return STAT_BADLENGTH;
    }

    if (cont) {
      strcat(buf, " ");
    } else {
      cont = 1;
    }
    strcat(buf, buffer);

    memset(buffer, 0, 30);
    fret = fscanf(scr, "%s", &buffer[0]);
  }

  bufcnt += strlen(SUFFIXES[num]);
  if (bufcnt > maxlen) {
    return STAT_BADLENGTH;
  }

  strcat(buf, SUFFIXES[num]);

  int status = pclose(scr);
  if (status == -1) {
    return STAT_BADPCLOSE;
  }

  return STAT_GOOD;
}

int get_time(char *buf, size_t maxlen)
{
  if (buf == NULL) {
    return STAT_BADFLAG;
  }

  time_t curtime = time(NULL);
  struct tm *now = localtime(&curtime);
  size_t ret = strftime(buf, maxlen, TIMEFMT, now);

  if (ret == 0) {
    return 60;
  }

  return 60 - now->tm_sec;
}

int main(int argc, char **argv)
{
  int status = daemonize();
  if (status == STAT_NOLOCKDIR) {
    fprintf(stderr, "Unable to access %s. Exiting.\n", LOCKDIR);
    return STAT_NOLOCKDIR;
  } else if (status == STAT_LOCKNOTDIR) {
    fprintf(stderr, "%s is not a directory. Exiting.\n", LOCKDIR);
    return STAT_LOCKNOTDIR;
  } else if (status == STAT_LOCKEXISTS) {
    fprintf(stderr, "Lock file %s already exists. Exiting.\n", LOCKFILE);
    return STAT_LOCKEXISTS;
  } else if (status == STAT_NOLOGDIR) {
    fprintf(stderr, "Unable to access %s. Exiting.\n", LOGDIR);
    return STAT_NOLOGDIR;
  } else if (status == STAT_LOGNOTDIR) {
    fprintf(stderr, "%s is not a directory. Exiting.\n", LOGDIR);
    return STAT_LOGNOTDIR;
  } else if (status != STAT_GOOD) {
    fprintf(stderr, "Error daemonizing. %d. Exiting.\n", status);
    return status;
  }

  if (logfd == -1) {
    return STAT_GOOD;
  }

  printf("Making signals\n");
  status = make_signals();
  if (status != 0) {
    perror("Error making signal handlers. Exiting.");
    return status;
  }

  int sig;
  char *timebuf = malloc(100);
  char *scriptbuf = malloc(100);
  char *barbuf = malloc(4096);

  int retstatus = STAT_GOOD;

  while (run) {
    memset(timebuf, 0, 100);
    memset(barbuf, 0, 4096);

    size_t barlen = 1;

    int towait = get_time(&timebuf[0], 100);
    if (towait < 0) {
      printf("Error getting time. Exiting.\n");
      retstatus = STAT_BADTIME;
      goto quit;
    }

    unsigned int pos = 0;
    for (const char **ptr = CMDS; *ptr != NULL; ++ptr, ++pos) {
      memset(scriptbuf, 0, 100);

      if (strcmp(*ptr, "@TIME@") != 0) {
      status = get_script(scriptbuf, 100, pos);
        if (status != 0) {
          retstatus = STAT_BADEXEC;
          printf("Error getting script %s, %d. Exiting.\n", *ptr, status);
          goto quit;
        }
      } else {
        strcpy(scriptbuf, timebuf);
      }

      if (pos == 0) {
        barlen += strlen(scriptbuf) + strlen(SEPERATOR);
      } else {
        barlen += strlen(scriptbuf);
      }

      if (barlen > 4096) {
        retstatus = STAT_OVERMEM;
        printf("Error making bar text. Exiting.\n");
        goto quit;
      }

      if (pos != 0) {
        strcat(barbuf, SEPERATOR);
        strcat(barbuf, scriptbuf);
      } else {
        strcpy(barbuf, scriptbuf);
      }
    }

    printf("%s\n", barbuf);
    set_text(barbuf);

    fflush(stdout);
    alarm(towait);
    status = sigwait(&sset, &sig);
    if (status != 0) {
      retstatus = STAT_BADWAIT;
      perror("Error in sigwait. Exiting.");
      goto quit;
    }

    fflush(stdout);
  }

quit:
  free(scriptbuf);
  free(timebuf);
  free(barbuf);
  printf("Exiting with status %d.\n", retstatus);
  fflush(stdout);
  close(logfd);
  unlink(LOCKFILE);
  return retstatus;
}

int make_signals()
{
  int status = 0;
  struct sigaction sa;

  sa.sa_handler = sighandler;
  sigemptyset(&sa.sa_mask);

  if (sigaction(SIGINT, &sa, NULL) == -1) {
    return STAT_BADINTACT;
  }

  if (sigaction(SIGTERM, &sa, NULL) == -1) {
    return STAT_BADTERMACT;
  }

  if (sigaction(SIGQUIT, &sa, NULL) == -1) {
    return STAT_BADQUITACT;
  }

  if (sigaction(SIGABRT, &sa, NULL) == -1) {
    return STAT_BADABRTACT;
  }

  status = sigemptyset(&sset);
  if (status == -1) {
    return STAT_BADEMPTYSET;
  }
  status = sigaddset(&sset, SIGALRM);
  if (status == -1) {
    return STAT_BADADDSET;
  }
  status = sigprocmask(SIG_BLOCK, &sset, NULL);
  if (status == -1) {
    return STAT_BADPROC;
  }

  return STAT_GOOD;
}

void reset_sigs()
{
  struct sigaction handler;
  handler.sa_handler = SIG_DFL;
  sigemptyset(&handler.sa_mask);

  for (int sig=0; sig < _NSIG; ++sig) {
    sigaction(sig, &handler, NULL);
  }

  sigset_t mask;
  sigemptyset(&mask);
  sigprocmask(SIG_SETMASK, &mask, NULL);
}

int set_text(char *t)
{
  struct XData xd;

  int status = connect_X(&xd);
  if (status != STAT_GOOD) {
    return status;
  }

  XTextProperty xprop;
  Status stat = XStringListToTextProperty(&t, 1, &xprop);
  if (stat == 0) {
    return STAT_NOPROP;
  }

  XSetWMName(xd.disp, xd.root, &xprop);

  XFree(xprop.value);

  disconnect_X(&xd);


  return STAT_GOOD;
}

void sighandler(int num)
{
  switch (num) {
    case SIGINT:
    case SIGTERM:
    case SIGQUIT:
    case SIGABRT:
      run = false;
      raise(SIGALRM);
      break;
    default:
      break;
  }
}
