/*
 * Copyright (c) 2005-2007 Chris Kuethe <chris.kuethe@gmail.com>
 * Copyright (c) 2005-2007 Eric S. Raymond <esr@thyrsus.com>
 * Copyright (c) 2011-2012 Alexey Illarionov <littlesavage@rambler.ru>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>

#include "flashutils.h"
#include "arm/include/mdproto.h"

const char *progname = "sirfmemdump";
const char *revision = "$Revision: 0.2 $";
static int verbosity = 3;

void gpsd_report(int errlevel, const char *fmt, ... )
/* assemble command in printf(3) style, use stderr or syslog */
{
   char buf[BUFSIZ];
   va_list ap;

   if (errlevel > verbosity)
      return;

   (void)strlcpy(buf, progname, BUFSIZ);
   (void)strlcat(buf, ": ", BUFSIZ);
   va_start(ap, fmt) ;
   (void)vsnprintf(buf + strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
   va_end(ap);
   (void)fputs(buf, stderr);
}

static void
usage(void){
   fprintf(stderr, "Usage: %s [-v d] [-l <loader_file>] [ -p tty ] [-n] command\n", progname);
}

static void version(void)
{
   fprintf(stdout,"%s %s\n",progname,revision);
}

static void help(void)
{

 printf("%s - Sirf memory dumper \t\t%s\n",
       progname, revision);
 usage();
 printf(
   "\nOptions:\n"
   "    -p  <tty>,     Serial port, default: " DEFAULT_PORT "\n"
   "    -l, <loader>   Injected loader, default: " DEFAULT_LOADER "\n"
   "    -n,            Do not inject loader\n"
   "    -i,            Do not switch from sirf to internal boot mode\n"
   "    -v,            Verbosity level \n"
   "    -h,            Help\n"
   "    -V,            Show version\n"
   "\nCommands:\n"
   "    ping                                 Ping loader\n"
   "    dump {src_addr} {dst_addr}           Dump memory\n"
   "    exec {f_addr} {R0} {R1} {R2} {R4}    Execute function f_addr\n"
   "    flash-info                           Print flash info\n"
   "    erase-sector {flash_addr}            Erase flash sector\n"
   "    program-word {flash_addr} {word}     Program one word\n"
   "    program {file}                       Program flash\n"
   "\n"
 );
 return;
}


int inject_loader(int pfd, struct termios *term, const char *lname, int switch_from_sirf)
{
   const char wait_result[]="+++";
   int lfd;
   size_t ls;
   void *loader;
   sigset_t sigset;
   struct stat sb;

  /* there may be a type-specific setup method */
  if(sirfSetProto(pfd, term, PROTO_SIRF, 38400) == -1) {
     gpsd_report(LOG_ERROR, "port_setup()\n");
     return 1;
  }

  gpsd_report(LOG_PROG, "port set up...\n");

  /* Open the loader file */
  if((lfd = open(lname, O_RDONLY)) == -1) {
      gpsd_report(LOG_ERROR, "open(%s): %s\n", lname, strerror(errno));
      return 1;
  }

  /* fstat() its file descriptor. Need the size, and avoid races */
  if(fstat(lfd, &sb) == -1) {
      gpsd_report(LOG_ERROR, "fstat(%s): %s\n", lname, strerror(errno));
      return 1;
  }

  gpsd_report(LOG_PROG, "passed sanity checks...\n");

  ls = (size_t)sb.st_size;

  /* malloc a loader buffer */
  if ((loader = malloc(ls)) == NULL) {
      gpsd_report(LOG_ERROR, "malloc(%zd)\n", ls);
      return 1;
  }

  if (read_full(lfd, loader, ls) != (ssize_t)ls) {
      (void)free(loader);
      gpsd_report(LOG_ERROR, "read(%zd)\n", ls);
      return 1;
  }

  /* don't care if close fails - kernel will force close on exit() */
  (void)close(lfd);

  gpsd_report(LOG_PROG, "loader read in...\n");

  gpsd_report(LOG_PROG, "blocking signals...\n");

  /* once we get here, we are uninterruptable. handle signals */
  (void)sigemptyset(&sigset);
  (void)sigaddset(&sigset, SIGINT);
  (void)sigaddset(&sigset, SIGHUP);
  (void)sigaddset(&sigset, SIGQUIT);
  (void)sigaddset(&sigset, SIGTSTP);
  (void)sigaddset(&sigset, SIGSTOP);
  (void)sigaddset(&sigset, SIGKILL);

  if(sigprocmask(SIG_BLOCK, &sigset, NULL) == -1) {
	  (void)free(loader);
	  gpsd_report(LOG_ERROR,"sigprocmask\n");
	  return 1;
  }

  if (switch_from_sirf) {
     gpsd_report(LOG_PROG, "Switching to internal boot mode...\n");
     if (sirfEnterInternalBootMode(pfd) == -1) {
	(void)free(loader);
	gpsd_report(LOG_ERROR, "sirfEnterInternalBootmode() error \n");
	return 1;
     }
  }

  gpsd_report(LOG_PROG, "Sending loader...\n");

  /* send the bootstrap/flash programmer */
  if (sirfSendLoader(pfd, term, loader, ls) == -1) {
     (void)free(loader);
     gpsd_report(LOG_ERROR, "Loader send\n");
     return 1;
  }
  (void)free(loader);

  gpsd_report(LOG_PROG, "Unblocking signals...\n");

  if(sigprocmask(SIG_UNBLOCK, &sigset, NULL) == -1) {
	  gpsd_report(LOG_ERROR,"sigprocmask\n");
	  return 1;
  }

  /* sirfSetProto(pfd, &term, PROTO_NMEA, 4800); */
  gpsd_report(LOG_PROG, "Finished.\n");

  if (expect(pfd, wait_result, strlen(wait_result), 30) != 0) {
     gpsd_report(LOG_PROG, "Loader successfully launched\n");
  }else {
     gpsd_report(LOG_PROG, "No response from loader\n");
     return 1;
  }

  return 0;
}


int cmd_ping(int pfd)
{
  unsigned read_status;
  int write_size;
  struct mdproto_cmd_buf_t cmd;

  write_size = mdproto_pkt_init(&cmd, MDPROTO_CMD_PING, NULL, 0);
  gpsd_report(LOG_PROG, "PING...\n");

  tcflush(pfd, TCIOFLUSH);
  usleep(10000);
  if (write(pfd, (void *)&cmd, write_size) < write_size) {
     gpsd_report(LOG_PROG, "write() error\n");
     return 1;
  }

  read_status = read_mdproto_pkt(pfd, &cmd);
  if (read_status != MDPROTO_STATUS_OK) {
     gpsd_report(LOG_PROG, "read_mdproto_pkt() error `%c`\n", read_status);
     return 1;
  }

  if (cmd.data.id != MDPROTO_CMD_PING_RESPONSE) {
     gpsd_report(LOG_PROG, "received wrong response code `0x%x`\n", cmd.data.id);
     return 1;
  }

  gpsd_report(LOG_PROG, "PONG...\n");
  return 0;
}

int cmd_exec(int pfd, unsigned f_addr, unsigned r0, unsigned r1, unsigned r2, unsigned r3)
{
  unsigned read_status;
  int write_size;
  struct mdproto_cmd_buf_t cmd;
  struct {
     uint32_t f_addr;
     uint32_t r0;
     uint32_t r1;
     uint32_t r2;
     uint32_t r3;
  } __attribute__((packed)) req;

  struct resp_t {
     uint32_t r0;
     uint32_t r1;
     uint32_t r2;
     uint32_t r3;
  } *resp;

  req.f_addr = htonl(f_addr);
  /* XXX: byteorder */
  req.r0 = r0;
  req.r1 = r1;
  req.r2 = r2;
  req.r3 = r3;

  write_size = mdproto_pkt_init(&cmd, MDPROTO_CMD_EXEC_CODE, &req, sizeof(req));
  gpsd_report(LOG_PROG, "EXECUTE...\n");

  tcflush(pfd, TCIOFLUSH);
  if (write(pfd, (void *)&cmd, write_size) < write_size) {
     gpsd_report(LOG_PROG, "write() error\n");
     return 1;
  }

  read_status = read_mdproto_pkt(pfd, &cmd);
  if (read_status != MDPROTO_STATUS_OK) {
     gpsd_report(LOG_PROG, "read_mdproto_pkt() error `%c`\n", read_status);
     return 1;
  }

  if (cmd.data.id != MDPROTO_CMD_EXEC_CODE_RESPONSE) {
     gpsd_report(LOG_PROG, "received wrong response code `0x%x`\n", cmd.data.id);
     return 1;
  }
  if (ntohs(cmd.size) != 4*4+1) {
     gpsd_report(LOG_PROG, "received wrong response size `0x%x`\n", ntohs(cmd.size));
     return 1;
  }
  resp = (struct resp_t *)&cmd.data.p[1];

  /* XXX: byteorder  */
  gpsd_report(LOG_PROG, "R0: %08x R1: %08x R2: %08x R3: %08x\n",
	(unsigned)resp->r0,
	(unsigned)resp->r1,
	(unsigned)resp->r2,
	(unsigned)resp->r3
	);
  return 0;
}


int cmd_dump(int pfd, unsigned src_addr, unsigned dst_addr)
{
  unsigned read_status;
  int write_size;
  int cur_size;
  struct mdproto_cmd_buf_t cmd;
  struct {
     uint32_t src;
     uint32_t dst;
  } __attribute__((packed)) req;


  req.src = htonl(src_addr);
  req.dst = htonl(dst_addr);
  write_size = mdproto_pkt_init(&cmd, MDPROTO_CMD_MEM_READ, &req, sizeof(req));
  gpsd_report(LOG_PROG, "MEM_READ...\n");

  tcflush(pfd, TCIOFLUSH);
  usleep(10000);
  if (write(pfd, (void *)&cmd, write_size) < write_size) {
     gpsd_report(LOG_PROG, "write() error\n");
     return 1;
  }

  while (src_addr <= dst_addr) {
     gpsd_report(LOG_RAW, "0x%x...\n", src_addr);
     read_status = read_mdproto_pkt(pfd, &cmd);
     if (read_status != MDPROTO_STATUS_OK) {
	gpsd_report(LOG_PROG, "read_mdproto_pkt() error `%c`\n", read_status);
	return 1;
     }
     if (cmd.data.id != MDPROTO_CMD_MEM_READ_RESPONSE) {
	gpsd_report(LOG_PROG, "received wrong response code `0x%x`\n", cmd.data.id);
	return 1;
     }
     cur_size = ntohs(cmd.size) - 1;
     if (write (STDOUT_FILENO, &cmd.data.p[1], cur_size) < cur_size) {
	gpsd_report(LOG_PROG, "write() to stdout error\n");
	return 1;
     }
     src_addr += cur_size;
  }

  gpsd_report(LOG_PROG, "DONE\n");
  return 0;
}


int
main(int argc, char **argv){

	int ch;
	int pfd;
	int do_inject_loader = 1;
	int do_switch_from_sirf = 1;
	int res = 0;
	int argnum;
	char *lname = DEFAULT_LOADER;
	char *port = DEFAULT_PORT;
	struct termios term;

	progname = argv[0];

	while ((ch = getopt(argc, argv, "l:Vv:p:ni")) != -1)
		switch (ch) {
		case 'l':
			lname = optarg;
			break;
		case 'p':
			port = optarg;
			break;
		case 'v':
			verbosity = atoi(optarg);
			break;
	        case 'n':
			do_inject_loader = 0;
			break;
	        case 'i':
			do_switch_from_sirf = 0;
			break;
		case 'V':
			version();
			exit(0);
		default:
			help();
			exit(0);
			/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;

	/* Open the serial port, blocking is OK */
	if((pfd = open(port, O_RDWR | O_NOCTTY , 0600)) == -1) {
		gpsd_report(LOG_ERROR, "open(%s) failed: %s\n", port, strerror(errno));
		return 1;
	}

	memset(&term, 0, sizeof(term));

	if (do_inject_loader) {
	   res = inject_loader(pfd, &term, lname, do_switch_from_sirf);
	   if (res != 0)
	      goto end;
	}

	argnum=0;
	while (argnum < argc) {
	   if (strcasecmp(argv[argnum], "ping") == 0) {
	      argnum++;
	      res = cmd_ping(pfd);
	      if (res != 0)
		 break;
	   }else if (strcasecmp(argv[argnum], "dump") == 0) {
	      unsigned long src_addr, dst_addr;
	      char *endptr;

	      if ((argc < 3)
		    || (*argv[argnum+1]=='\0')
		    || (*argv[argnum+2]=='\0') ) {
		 gpsd_report(LOG_ERROR, "src_addr/dst_addr not defined\n");
		 break;
	      }

	      src_addr = strtoul(argv[argnum+1], &endptr, 0);
	      if (*endptr != '\0') {
		 gpsd_report(LOG_ERROR, "malformed %s `%s`\n", "src_addr", argv[argc+1]);
		 break;
	      }
	      dst_addr = strtoul(argv[argnum+2], &endptr, 0);
	      if (*endptr != '\0') {
		 gpsd_report(LOG_ERROR, "malformed %s `%s`\n", "dst_addr", argv[argc+2]);
		 break;
	      }
	      if (dst_addr < src_addr) {
		 gpsd_report(LOG_ERROR, "dst_addr < src_addr\n");
		 break;
	      }
	      res = cmd_dump(pfd, src_addr, dst_addr);
	      if (res != 0)
		 break;
	      argnum += 3;
	   }else if (strcasecmp(argv[argnum], "exec") == 0) {
	      unsigned i;
	      unsigned f_addr;
	      unsigned r[4];
	      char *endptr;
	      unsigned long tmp;
	      unsigned i_err;

	      if ((argc < 6)
		    || (*argv[argnum+1]=='\0')
		    || (*argv[argnum+2]=='\0')
		    || (*argv[argnum+3]=='\0')
		    || (*argv[argnum+4]=='\0')
		    || (*argv[argnum+5]=='\0') ) {
		 gpsd_report(LOG_ERROR, "f_addr/r0/r1/r2/r3 not defined\n");
		 break;
	      }

	      tmp = strtoul(argv[argnum+1], &endptr, 0);
	      if (*endptr != '\0') {
		 gpsd_report(LOG_ERROR, "malformed %s `%s`\n", "f_addr", argv[argc+1]);
		 break;
	      }
	      f_addr = (unsigned)tmp;

	      i_err=0;
	      for (i=0; i<4; i++) {
		 tmp = strtoul(argv[argnum+2+i], &endptr, 0);
		 if (*endptr != '\0') {
		    gpsd_report(LOG_ERROR, "malformed r%u `%s`\n", i, argv[argc+2+i]);
		    i_err=1;
		    break;
		 }
		 r[i] = (unsigned)tmp;
	      }
	      if (i_err)
		 break;

	      res = cmd_exec(pfd, f_addr, r[0], r[1], r[2], r[3]);
	      if (res != 0)
		 break;

	      argnum += 6;
	   }else if (strcasecmp(argv[argnum], "flash-info") == 0) {
	      argnum++;
	      res = cmd_flash_info(pfd);
	      if (res != 0)
		 break;
	   }else if (strcasecmp(argv[argnum], "erase-sector") == 0) {
	      unsigned addr;
	      char *endptr;

	      if ((argc < 2)
		    || (*argv[argnum+1]=='\0')) {
		 gpsd_report(LOG_ERROR, "address not defined\n");
		 break;
	      }

	      addr = strtoul(argv[argnum+1], &endptr, 0);
	      if (*endptr != '\0') {
		 gpsd_report(LOG_ERROR, "malformed %s `%s`\n", "addr", argv[argc+1]);
		 break;
	      }
	      res = cmd_erase_sector(pfd, addr);
	      if (res != 0)
		 break;
	      argnum += 2;
	   }else if (strcasecmp(argv[argnum], "program-word") == 0) {
	      unsigned addr, word;
	      char *endptr;

	      if ((argc < 1+2)
		    || (*argv[argnum+1]=='\0')
		    ) {
		 gpsd_report(LOG_ERROR, "address not defined\n");
		 break;
	      }

	      addr = strtoul(argv[argnum+1], &endptr, 0);
	      if (*endptr != '\0') {
		 gpsd_report(LOG_ERROR, "malformed %s `%s`\n", "addr", argv[argc+1]);
		 break;
	      }
	      word = strtoul(argv[argnum+2], &endptr, 0);
	      if (*endptr != '\0') {
		 gpsd_report(LOG_ERROR, "malformed %s `%s`\n", "word", argv[argc+1]);
		 break;
	      }

	      res = cmd_program_word(pfd, addr, word&0xffff);
	      if (res != 0)
		 break;
	      argnum += 1+2;
	   }else if (strcasecmp(argv[argnum], "program") == 0) {
	      if ((argc < 1+1)
		    || (*argv[argnum+1]=='\0')
		    ) {
		 gpsd_report(LOG_ERROR, "filename not defined\n");
		 break;
	      }
	      res = cmd_program_flash(pfd, argv[argnum+1]);
	      if (res != 0)
		 break;
	      argnum += 1+1;
	   }else {
	      gpsd_report(LOG_ERROR, "unknown command `%s`\n", argv[argnum]);
	      break;
	   }
	}

end:
	close (pfd);
	/* return() from main(), to take advantage of SSP compilers */
	return res;
}

