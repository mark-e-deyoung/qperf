/*
 * qperf - main.
 * Measure IP and RDMA performance.
 *
 * Copyright (c) 2002-2007 Johann George.  All rights reserved.
 * Copyright (c) 2006-2007 QLogic Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sched.h>
#include <stdio.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/times.h>
#include <sys/select.h>
#include <sys/utsname.h>
#include "qperf.h"


/*
 * Configurable parameters.  If your change makes this version of qperf
 * incompatible with previous versions (usually a change to the Req structure),
 * increment VER_MIN and set VER_INC to 0.  Otherwise, just increment VER_INC.
 * VER_MAJ is reserved for major changes.
 */
#define VER_MAJ  0                      /* Major version */
#define VER_MIN  2                      /* Minor version */
#define VER_INC  0                      /* Incremental version */
#define LISTENQ  5                      /* Size of listen queue */
#define BUFSIZE  1024                   /* Size of buffers */
#define SYNCMESG "SyN"                  /* Synchronize message */
#define SYNCSIZE sizeof(SYNCMESG)       /* Size of synchronize message */


/*
 * For convenience.
 */
#define with(c) |(c<<8)


/*
 * Option list.
 */
typedef struct OPTION {
    char       *name;                   /* Name of option */
    short       server_valid;           /* Option valid on server */
    void      (*func)();                /* Function to call */
    int         arg1;                   /* First argument */
    int         arg2;                   /* Second argument */
} OPTION;


/*
 * Parameter information.
 */
typedef struct PAR_INFO {
    PAR_INDEX   index;                  /* Index into parameter table */
    int         type;                   /* Type */
    void       *ptr;                    /* Pointer to value */
    char       *name;                   /* Option name */
    int         set;                    /* Parameter has been set */
    int         used;                   /* Parameter has been used */
    int         inuse;                  /* Parameter is in use */
} PAR_INFO;


/*
 * Parameter name association.
 */
typedef struct PAR_NAME {
    char       *name;                   /* Name */
    PAR_INDEX   loc_i;                  /* Local index */
    PAR_INDEX   rem_i;                  /* Remote index */
} PAR_NAME;


/*
 * Test prototype.
 */
typedef struct TEST {
    char       *name;                   /* Test name */
    void      (*client)(void);          /* Client function */
    void      (*server)(void);          /* Server function */
} TEST;


/*
 * Used to save output data for formatting.
 */
typedef struct SHOW {
    char    *pref;                      /* Name prefix */
    char    *name;                      /* Name */
    char    *data;                      /* Data */
    char    *unit;                      /* Unit */
    char    *altn;                      /* Alternative value */
} SHOW;


/*
 * Configuration information.
 */
typedef struct CONF {
    char        node[STRSIZE];          /* Node */
    char        cpu[STRSIZE];           /* CPU */
    char        os[STRSIZE];            /* Operating System */
    char        qperf[STRSIZE];         /* Qperf version */
} CONF;


/*
 * Function prototypes.
 */
static void     add_ustat(USTAT *l, USTAT *r);
static long     arg_long(char ***argvp);
static long     arg_size(char ***argvp);
static char    *arg_strn(char ***argvp);
static long     arg_time(char ***argvp);
static void     bug_die(char *fmt, ...);
static void     calc_node(RESN *resn, STAT *stat);
static void     calc_results(void);
static void     client(TEST *test);
static int      cmpsub(char *s2, char *s1);
static char    *commify(char *data);
static void     dec_req(REQ *host);
static void     dec_stat(STAT *host);
static void     dec_ustat(USTAT *host);
static void     do_args(char *args[]);
static void     enc_req(REQ *host);
static void     enc_stat(STAT *host);
static void     enc_ustat(USTAT *host);
static TEST    *find_test(char *name);
static OPTION  *find_option(char *name);
static void     get_conf(CONF *conf);
static void     get_cpu(CONF *conf);
static double   get_seconds(void);
static void     get_times(CLOCK timex[T_N]);
static void     initialize(void);
static void     init_lstat(void);
static void     init_vars(void);
static int      nice_1024(char *pref, char *name, long long value);
static void     opt_help(OPTION *option, char ***argvp);
static void     opt_misc(OPTION *option, char ***argvp);
static void     opt_strn(OPTION *option, char ***argvp);
static void     opt_long(OPTION *option, char ***argvp);
static void     opt_size(OPTION *option, char ***argvp);
static void     opt_time(OPTION *option, char ***argvp);
static void     opt_vers(OPTION *option, char ***argvp);
static PAR_INFO *par_info(PAR_INDEX index);
static PAR_INFO *par_set(char *name, PAR_INDEX index);
static int      par_isset(PAR_INDEX index);
static void     place_any(char *pref, char *name, char *unit, char *data,
                          char *altn);
static void     place_show(void);
static void     place_val(char *pref, char *name, char *unit, double value);
static char    *qasprintf(char *fmt, ...);
static int      recv_sync(void);
static void     run_client_conf(void);
static void     run_client_quit(void);
static void     run_server_conf(void);
static void     run_server_quit(void);
static int      send_recv_mesg(int sr, char *item, int fd, char *buf, int len);
static int      send_sync(void);
static void     server(void);
static void     server_listen(void);
static int      server_recv_request(void);
static void     set_affinity(void);
static int      set_nonblock(int fd);
static void     set_signals(void);
static void     show_debug(void);
static void     show_info(MEASURE measure);
static void     show_rest(void);
static void     show_used(void);
static void     sig_alrm(int signo, siginfo_t *siginfo, void *ucontext);
static char    *skip_colon(char *s);
static void     start_timing(int seconds);
static void     strncopy(char *d, char *s, int n);
static int      verbose(int type, double value);
static void     view_band(int type, char *pref, char *name, double value);
static void     view_cost(int type, char *pref, char *name, double value);
static void     view_cpus(int type, char *pref, char *name, double value);
static void     view_rate(int type, char *pref, char *name, double value);
static void     view_long(int type, char *pref, char *name, long long value);
static void     view_size(int type, char *pref, char *name, long long value);
static void     view_strn(int type, char *pref, char *name, char *value);
static void     view_time(int type, char *pref, char *name, double value);


/*
 * Configurable variables.
 */
static int      ListenPort      = 19765;
static int      Precision       = 3;
static int      ServerTimeout   = 5;


/*
 * Static variables.
 */
static REQ      RReq;
static int      Debug;
static uint8_t *DecodePtr;
static int      ExitStatus;
static uint8_t *EncodePtr;
static STAT     IStat;
static int      ListenFD;
static int      ProcStatFD;
static int      RemoteFD;
static STAT     RStat;
static int      ShowIndex;
static SHOW     ShowTable[256];
static int      UnifyUnits;
static int      UnifyNodes;
static int      VerboseConf;
static int      VerboseStat;
static int      VerboseTime;
static int      VerboseUsed;
static int      Wait;


/*
 * Global variables.
 */
RES             Res;
REQ             Req;
STAT            LStat;
char           *TestName;
char           *ServerName;
int             Successful;
volatile int    Finished;


/*
 * Parameter names.  This is used to print out the names of the parameters that
 * have been set.
 */
PAR_NAME ParName[] ={
    { "access_recv",    L_ACCESS_RECV,    R_ACCESS_RECV   },
    { "affinity",       L_AFFINITY,       R_AFFINITY      },
    { "flip",           L_FLIP,           R_FLIP          },
    { "id",             L_ID,             R_ID            },
    { "msg_size",       L_MSG_SIZE,       R_MSG_SIZE      },
    { "mtu_size",       L_MTU_SIZE,       R_MTU_SIZE      },
    { "no_msgs",        L_NO_MSGS,        R_NO_MSGS       },
    { "poll_mode",      L_POLL_MODE,      R_POLL_MODE     },
    { "port",           L_PORT,           R_PORT          },
    { "rd_atomic",      L_RD_ATOMIC,      R_RD_ATOMIC     },
    { "sock_buf_size",  L_SOCK_BUF_SIZE,  R_SOCK_BUF_SIZE },
    { "time",           L_TIME,           R_TIME          },
    { "timeout",        L_TIMEOUT,        R_TIMEOUT       },
};


/*
 * Parameters.  These must be listed in the same order as the indices are
 * defined.
 */
PAR_INFO ParInfo[P_N] ={
    { P_NULL,                                       },
    { L_ACCESS_RECV,    'l',  &Req.access_recv      },
    { R_ACCESS_RECV,    'l',  &RReq.access_recv     },
    { L_AFFINITY,       'l',  &Req.affinity         },
    { R_AFFINITY,       'l',  &RReq.affinity        },
    { L_FLIP,           'l',  &Req.flip             },
    { R_FLIP,           'l',  &RReq.flip            },
    { L_ID,             'p',  &Req.id               },
    { R_ID,             'p',  &RReq.id              },
    { L_MSG_SIZE,       's',  &Req.msg_size         },
    { R_MSG_SIZE,       's',  &RReq.msg_size        },
    { L_MTU_SIZE,       's',  &Req.mtu_size         },
    { R_MTU_SIZE,       's',  &RReq.mtu_size        },
    { L_NO_MSGS,        'l',  &Req.no_msgs          },
    { R_NO_MSGS,        'l',  &RReq.no_msgs         },
    { L_POLL_MODE,      'l',  &Req.poll_mode        },
    { R_POLL_MODE,      'l',  &RReq.poll_mode       },
    { L_PORT,           'l',  &Req.port             },
    { R_PORT,           'l',  &RReq.port            },
    { L_RATE,           'p',  &Req.rate             },
    { R_RATE,           'p',  &RReq.rate            },
    { L_RD_ATOMIC,      'l',  &Req.rd_atomic        },
    { R_RD_ATOMIC,      'l',  &RReq.rd_atomic       },
    { L_SOCK_BUF_SIZE,  's',  &Req.sock_buf_size    },
    { R_SOCK_BUF_SIZE,  's',  &RReq.sock_buf_size   },
    { L_TIME,           't',  &Req.time             },
    { R_TIME,           't',  &RReq.time            },
    { L_TIMEOUT,        't',  &Req.timeout          },
    { R_TIMEOUT,        't',  &RReq.timeout         },
};


/*
 * Options.
 */
OPTION Options[] ={
    { "--access_recv",        0, &opt_long, L_ACCESS_RECV,   R_ACCESS_RECV   },
    {   "-Ar",                0, &opt_long, L_ACCESS_RECV,   R_ACCESS_RECV   },
    { "--affinity",           0, &opt_long, L_AFFINITY,      R_AFFINITY      },
    {   "-a",                 0, &opt_long, L_AFFINITY,      R_AFFINITY      },
    {  "--loc_affinity",      0, &opt_long, L_AFFINITY,                      },
    {   "-la",                0, &opt_long, L_AFFINITY,                      },
    {  "--rem_affinity",      0, &opt_long, R_AFFINITY                       },
    {   "-ra",                0, &opt_long, R_AFFINITY                       },
    { "--debug",              1, &opt_misc, 'D',                             },
    {   "-D",                 1, &opt_misc, 'D',                             },
    { "--flip",               0, &opt_long, L_FLIP,          R_FLIP          },
    {   "-f",                 0, &opt_long, L_FLIP,          R_FLIP          },
    { "--help",               0, &opt_help                                   },
    {   "-h",                 0, &opt_help                                   },
    { "--host",               0, &opt_misc, 'H',                             },
    {   "-H",                 0, &opt_misc, 'H',                             },
    { "--id",                 0, &opt_strn, L_ID,            R_ID            },
    {   "-i",                 0, &opt_strn, L_ID,            R_ID            },
    {  "--loc_id",            0, &opt_strn, L_ID,                            },
    {   "-li",                0, &opt_strn, L_ID,                            },
    {  "--rem_id",            0, &opt_strn, R_ID                             },
    {   "-ri",                0, &opt_strn, R_ID                             },
    { "--listen_port",        1, &opt_misc, 'l','p'                          },
    {   "-lp",                1, &opt_misc, 'l','p'                          },
    { "--msg_size",           0, &opt_size, L_MSG_SIZE,      R_MSG_SIZE      },
    {   "-m",                 0, &opt_size, L_MSG_SIZE,      R_MSG_SIZE      },
    { "--mtu_size",           0, &opt_size, L_MTU_SIZE,      R_MTU_SIZE      },
    {   "-M",                 0, &opt_size, L_MTU_SIZE,      R_MTU_SIZE      },
    { "--no_msgs",            0, &opt_long, L_NO_MSGS,       R_NO_MSGS       },
    {   "-n",                 0, &opt_long, L_NO_MSGS,       R_NO_MSGS       },
    { "--poll",               0, &opt_long, L_POLL_MODE,     R_POLL_MODE     },
    {   "-P",                 0, &opt_long, L_POLL_MODE,     R_POLL_MODE     },
    {  "--loc_poll",          0, &opt_long, L_POLL_MODE,                     },
    {   "-lP",                0, &opt_long, L_POLL_MODE,                     },
    {  "--rem_poll",          0, &opt_long, R_POLL_MODE                      },
    {   "-rP",                0, &opt_long, R_POLL_MODE                      },
    { "--port",               0, &opt_long, L_PORT,          R_PORT          },
    {   "-p",                 0, &opt_long, L_PORT,          R_PORT          },
    { "--precision",          0, &opt_misc, 'e',                             },
    {   "-e",                 0, &opt_misc, 'e',                             },
    { "--rate",               0, &opt_strn, L_RATE,          R_RATE          },
    {   "-r",                 0, &opt_strn, L_RATE,          R_RATE          },
    {  "--loc_rate",          0, &opt_strn, L_RATE                           },
    {   "-lr",                0, &opt_strn, L_RATE                           },
    {  "--rem_rate",          0, &opt_strn, R_RATE                           },
    {   "-rr",                0, &opt_strn, R_RATE                           },
    { "-rd_atomic",           0, &opt_long, L_RD_ATOMIC,     R_RD_ATOMIC     },
    {   "-R",                 0, &opt_long, L_RD_ATOMIC,     R_RD_ATOMIC     },
    {  "--loc_rd_atomic",     0, &opt_long, L_RD_ATOMIC,                     },
    {   "-lR",                0, &opt_long, L_RD_ATOMIC,                     },
    {  "--rem_rd_atomic",     0, &opt_long, R_RD_ATOMIC                      },
    {   "-rR",                0, &opt_long, R_RD_ATOMIC                      },
    { "--sock_buf_size",      0, &opt_size, L_SOCK_BUF_SIZE, R_SOCK_BUF_SIZE },
    {   "-S",                 0, &opt_size, L_SOCK_BUF_SIZE, R_SOCK_BUF_SIZE },
    {  "--loc_sock_buf_size", 0, &opt_size, L_SOCK_BUF_SIZE                  },
    {   "-lS",                0, &opt_size, L_SOCK_BUF_SIZE                  },
    {  "--rem_sock_buf_size", 0, &opt_size, R_SOCK_BUF_SIZE                  },
    {   "-rS",                0, &opt_size, R_SOCK_BUF_SIZE                  },
    { "--time",               0, &opt_time, L_TIME,          R_TIME          },
    {   "-t",                 0, &opt_time, L_TIME,          R_TIME          },
    { "--timeout",            0, &opt_time, L_TIMEOUT,       R_TIMEOUT       },
    {   "-T",                 0, &opt_time, L_TIMEOUT,       R_TIMEOUT       },
    {  "--loc_timeout",       0, &opt_time, L_TIMEOUT                        },
    {   "-lT",                0, &opt_time, L_TIMEOUT                        },
    {  "--rem_timeout",       0, &opt_time, R_TIMEOUT                        },
    {   "-rT",                0, &opt_time, R_TIMEOUT                        },
    { "--server_timeout",     0, &opt_misc, 's', 't'                         },
    {   "-st",                0, &opt_misc, 's', 't'                         },
    { "--unify_nodes",        0, &opt_misc, 'U'                              },
    {   "-U",                 0, &opt_misc, 'U'                              },
    { "--unify_units",        0, &opt_misc, 'u'                              },
    {   "-u",                 0, &opt_misc, 'u'                              },
    { "--verbose",            0, &opt_misc, 'v'                              },
    {   "-v",                 0, &opt_misc, 'v'                              },
    { "--verbose_conf",       0, &opt_misc, 'v', 'c'                         },
    {   "-vc",                0, &opt_misc, 'v', 'c'                         },
    { "--verbose_stat",       0, &opt_misc, 'v', 's'                         },
    {   "-vs",                0, &opt_misc, 'v', 's'                         },
    { "--verbose_time",       0, &opt_misc, 'v', 't'                         },
    {   "-vt",                0, &opt_misc, 'v', 't'                         },
    { "--verbose_used",       0, &opt_misc, 'v', 'u'                         },
    {   "-vu",                0, &opt_misc, 'v', 'u'                         },
    { "--verbose_more",       0, &opt_misc, 'v', 'v'                         },
    {   "-vv",                0, &opt_misc, 'v', 'v'                         },
    { "--verbose_more_conf",  0, &opt_misc, 'v', 'c'                         },
    {   "-vC",                0, &opt_misc, 'v', 'C'                         },
    { "--verbose_more_stat",  0, &opt_misc, 'v', 's'                         },
    {   "-vS",                0, &opt_misc, 'v', 'S'                         },
    { "--verbose_more_time",  0, &opt_misc, 'v', 't'                         },
    {   "-vT",                0, &opt_misc, 'v', 'T'                         },
    { "--verbose_more_used",  0, &opt_misc, 'v', 'u'                         },
    {   "-vU",                0, &opt_misc, 'v', 'U'                         },
    { "--version",            0, &opt_vers,                                  },
    {   "-V",                 0, &opt_vers,                                  },
    { "--wait",               0, &opt_misc, 'W',                             },
    {   "-W",                 0, &opt_misc, 'W',                             },
};


/*
 * Tests.
 */
#define test(n) { #n, run_client_##n, run_server_##n }
TEST Tests[] ={
    test(conf),
    test(quit),
    test(rds_bw),
    test(rds_lat),
    test(sdp_bw),
    test(sdp_lat),
    test(tcp_bw),
    test(tcp_lat),
    test(udp_bw),
    test(udp_lat),
#ifdef HAVE_LIBIBVERBS
    test(rc_bi_bw),
    test(rc_bw),
    test(rc_compare_swap_mr),
    test(rc_fetch_add_mr),
    test(rc_lat),
    test(rc_rdma_read_bw),
    test(rc_rdma_read_lat),
    test(rc_rdma_write_bw),
    test(rc_rdma_write_lat),
    test(rc_rdma_write_poll_lat),
    test(uc_bi_bw),
    test(uc_bw),
    test(uc_lat),
    test(uc_rdma_write_bw),
    test(uc_rdma_write_lat),
    test(uc_rdma_write_poll_lat),
    test(ud_bi_bw),
    test(ud_bw),
    test(ud_lat),
    test(ver_rc_compare_swap),
    test(ver_rc_fetch_add),
#endif
};


int
main(int argc, char *argv[])
{
    initialize();
    set_signals();
    do_args(&argv[1]);
    return ExitStatus;
}


/*
 * Initialize.
 */
static void
initialize(void)
{
    init_vars();
}


/*
 * Initialize variables.
 */
static void
init_vars(void)
{
    int i;

    for (i = 0; i < P_N; ++i)
        if (ParInfo[i].index != i)
            bug_die("initialize: ParInfo: out of order: %d", i);
    ProcStatFD = open("/proc/stat", 0);
    if (ProcStatFD < 0)
        syserror_die("Cannot open /proc/stat");
    IStat.no_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    IStat.no_ticks = sysconf(_SC_CLK_TCK);
}


/*
 * Look for a colon and skip past it and any spaces.
 */
static char *
skip_colon(char *s)
{
    for (;;) {
        int c = *s++;
        if (c == ':')
            break;
        if (c == '\0')
            return 0;
    }
    while (*s == ' ')
        s++;
    return s;
}


/*
 * A case insensitive string compare.  s2 must at least contain all of s1 but
 * can be longer.
 */
static int
cmpsub(char *s2, char *s1)
{
    for (;;) {
        int c1 = *s1++;
        int c2 = *s2++;
        if (c1 == '\0')
            return 1;
        if (c2 == '\0')
            return 0;
        if (tolower(c1) != tolower(c2))
            return 0;
    }
}


/*
 * Set up signal handlers.
 */
static void
set_signals(void)
{
    struct sigaction alrm ={ .sa_sigaction = sig_alrm };
    sigaction(SIGALRM, &alrm, 0);
    sigaction(SIGPIPE, &alrm, 0);
}


/*
 * Note that time is up.
 */
static void
sig_alrm(int signo, siginfo_t *siginfo, void *ucontext)
{
    set_finished();
}


/*
 * Parse arguments.
 */
static void
do_args(char *args[])
{
    int isClient = 0;
    int testSpecified = 0;

    while (*args) {
        char *arg = *args;
        if (arg[0] == '-') {
            OPTION *option = find_option(arg);
            if (!option)
                error_die("%s: bad option; try qperf --help", arg);
            if (!option->server_valid)
                isClient = 1;
            option->func(option, &args);
        } else {
            isClient = 1;
            if (!ServerName)
                ServerName = arg;
            else {
                TEST *p = find_test(arg);
                if (!p)
                    error_die("%s: bad test; try qperf --help", arg);
                client(p);
                testSpecified = 1;
            }
            ++args;
        }
    }
    if (!isClient)
        server();
    else if (!testSpecified) {
        if (!ServerName)
            error_die("You used a client only option but did not specify the "
                      "server name.\nDo you want to be a client or server?");
        if (find_test(ServerName))
            error_die("Must specify host name first; try qperf --help");
        error_die("Must specify a test type; try qperf --help");
    }
}


/*
 * Given the name of an option, find it.
 */
static OPTION *
find_option(char *name)
{
    int n = cardof(Options);
    OPTION *p = Options;
    for (; n--; ++p)
        if (streq(name, p->name))
            return p;
    return 0;
}


/*
 * Given the name of a test, find it.
 */
static TEST *
find_test(char *name)
{
    int n = cardof(Tests);
    TEST *p = Tests;
    for (; n--; ++p)
        if (streq(name, p->name))
            return p;
    return 0;
}


/*
 * Print out a help message.
 */
static void
opt_help(OPTION *option, char ***argvp)
{
    char **usage;
    char *category = (*argvp)[1];

    if (!category)
        category = "main";
    for (usage = Usage; *usage; usage += 2)
        if (streq(*usage, category))
            break;
    if (!*usage)
        error_die("Cannot find help category %s; try: qperf --help");
    printf("%s", usage[1]);
    exit(0);
}


/*
 * Handle options requiring a long argument.
 */
static void
opt_long(OPTION *option, char ***argvp)
{
    long l = arg_long(argvp);
    setp_u32(option->name, option->arg1, l);
    setp_u32(option->name, option->arg2, l);
}


/*
 * Handle miscellaneous options.
 */
static void
opt_misc(OPTION *option, char ***argvp)
{
    switch (option->arg1 with (option->arg2)) {
    case 'e':
        Precision = arg_long(argvp);
        return;
    case 'u':
        UnifyUnits = 1;
        break;
    case 'v':
        VerboseConf = 1;
        VerboseStat = 1;
        VerboseTime = 1;
        VerboseUsed = 1;
        break;
    case 'D':
        Debug = 1;
        break;
    case 'H':
        ServerName = arg_strn(argvp);
        return;
    case 'U':
        UnifyNodes = 1;
        break;
    case 'W':
        Wait = arg_time(argvp);
        return;
    case ('l') with ('p'):
        ListenPort = arg_long(argvp);
        return;
    case ('s') with ('t'):
        ServerTimeout = arg_time(argvp);
        return;
    case ('v') with ('c'):
        VerboseConf = 1;
        break;
    case ('v') with ('s'):
        VerboseStat = 1;
        break;
    case ('v') with ('t'):
        VerboseTime = 1;
        break;
    case ('v') with ('u'):
        VerboseUsed = 1;
        break;
    case ('v') with ('v'):
        VerboseConf = 2;
        VerboseStat = 2;
        VerboseTime = 2;
        VerboseUsed = 2;
        break;
    case ('v') with ('C'):
        VerboseConf = 2;
        break;
    case ('v') with ('S'):
        VerboseStat = 2;
        break;
    case ('v') with ('T'):
        VerboseTime = 2;
        break;
    case ('v') with ('U'):
        VerboseUsed = 2;
        break;
    default:
        bug_die("opt_misc: unknown argument: %s", option->name);
    }
    *argvp += 1;
}


/*
 * Handle options requiring a size argument.
 */
static void
opt_size(OPTION *option, char ***argvp)
{
    long l = arg_size(argvp);
    setp_u32(option->name, option->arg1, l);
    setp_u32(option->name, option->arg2, l);
}


/*
 * Handle options requiring a string argument.
 */
static void
opt_strn(OPTION *option, char ***argvp)
{
    char *s = arg_strn(argvp);
    setp_str(option->name, option->arg1, s);
    setp_str(option->name, option->arg2, s);
}


/*
 * Handle options requiring a time argument.
 */
static void
opt_time(OPTION *option, char ***argvp)
{
    long l = arg_time(argvp);
    setp_u32(option->name, option->arg1, l);
    setp_u32(option->name, option->arg2, l);
}


/*
 * Print out our current version.
 */
static void
opt_vers(OPTION *option, char ***argvp)
{
    printf("qperf %d.%d.%d\n", VER_MAJ, VER_MIN, VER_INC);
    exit(0);
}


/*
 * If any options were set but were not used, print out a warning message for
 * the user.
 */
void
opt_check(void)
{
    PAR_INFO *p;
    PAR_INFO *q;
    PAR_INFO *r = endof(ParInfo);

    for (p = ParInfo; p < r; ++p) {
        if (p->used || !p->set)
            continue;
        error("warning: %s set but not used in test %s", p->name, TestName);
        for (q = p+1; q < r; ++q)
            if (q->set && q->name == p->name)
                q->set = 0;
    }
}


/*
 * Return the value of a long argument.  It must be non-negative.
 */
static long
arg_long(char ***argvp)
{
    char **argv = *argvp;
    char *p;
    long l;

    if (!argv[1])
        error_die("Missing argument to %s", argv[0]);
    l = strtol(argv[1], &p, 10);
    if (p[0] != '\0')
        error_die("Bad argument: %s", argv[1]);
    if (l < 0)
        error_die("%s requires a non-negative number", argv[0]);
    *argvp += 2;
    return l;
}


/*
 * Return the value of a size argument.
 */
static long
arg_size(char ***argvp)
{
    char *p;
    long double d;
    long l = 0;
    char **argv = *argvp;

    if (!argv[1])
        error_die("Missing argument to %s", argv[0]);
    d = strtold(argv[1], &p);
    if (d < 0)
        error_die("%s requires a non-negative number", argv[0]);

    if (p[0] == '\0')
        l = d;
    else {
        if (streq(p, "kb") || streq(p, "k"))
            l = (long)(d * (1000));
        else if (streq(p, "mb") || streq(p, "m"))
            l = (long)(d * (1000 * 1000));
        else if (streq(p, "gb") || streq(p, "g"))
            l = (long)(d * (1000 * 1000 * 1000));
        else if (streq(p, "kib") || streq(p, "K"))
            l = (long)(d * (1024));
        else if (streq(p, "mib") || streq(p, "M"))
            l = (long)(d * (1024 * 1024));
        else if (streq(p, "gib") || streq(p, "G"))
            l = (long)(d * (1024 * 1024 * 1024));
        else
            error_die("Bad argument: %s", argv[1]);
    }
    *argvp += 2;
    return l;
}


/*
 * Return the value of a string argument.
 */
static char *
arg_strn(char ***argvp)
{
    char **argv = *argvp;
    if (!argv[1])
        error_die("Missing argument to %s", argv[0]);
    *argvp += 2;
    return argv[1];
}


/*
 * Return the value of a size argument.
 */
static long
arg_time(char ***argvp)
{
    char *p;
    long double d;

    long l = 0;
    char **argv = *argvp;
    if (!argv[1])
        error_die("Missing argument to %s", argv[0]);
    d = strtold(argv[1], &p);
    if (d < 0)
        error_die("%s requires a non-negative number", argv[0]);

    if (p[0] == '\0')
        l = (long)d;
    else {
        int u = *p;
        if (p[1] != '\0')
            error_die("Bad argument: %s", argv[1]);
        if (u == 's' || u == 'S')
            l = (long)d;
        else if (u == 'm' || u == 'M')
            l = (long)(d * (60));
        else if (u == 'h' || u == 'H')
            l = (long)(d * (60 * 60));
        else if (u == 'd' || u == 'D')
            l = (long)(d * (60 * 60 * 24));
        else
            error_die("Bad argument: %s", argv[1]);
    }
    *argvp += 2;
    return l;
}


/*
 * Set a value stored in a 32 bit value without letting anyone know we set it.
 */
void
setv_u32(PAR_INDEX index, uint32_t l)
{
    PAR_INFO *p = par_info(index);
    *((uint32_t *)p->ptr) = l;
}


/*
 * Set an option stored in a 32 bit value.
 */
void
setp_u32(char *name, PAR_INDEX index, uint32_t l)
{
    PAR_INFO *p = par_set(name, index);
    if (!p)
        return;
    *((uint32_t *)p->ptr) = l;
}


/*
 * Set an option stored in a string vector.
 */
void
setp_str(char *name, PAR_INDEX index, char *s)
{
    PAR_INFO *p = par_set(name, index);
    if (!p)
        return;
    if (strlen(s) >= STRSIZE)
        error_die("%s: too long", s);
    strcpy(p->ptr, s);
}


/*
 * Note a parameter as being used.
 */
void
par_use(PAR_INDEX index)
{
    PAR_INFO *p = par_info(index);
    p->used = 1;
    p->inuse = 1;
}


/*
 * Set the PAR_INFO.name value.
 */
static PAR_INFO *
par_set(char *name, PAR_INDEX index)
{
    PAR_INFO *p = par_info(index);
    if (index == P_NULL)
        return 0;
    if (name) {
        p->name = name;
        p->set = 1;
    } else {
        p->used = 1;
        p->inuse = 1;
        if (p->name)
            return 0;
    }
    return p;
}


/*
 * Determine if a parameter is set.
 */
static int
par_isset(PAR_INDEX index)
{
    return par_info(index)->name != 0;
}


/*
 * Index the ParInfo table.
 */
static PAR_INFO *
par_info(PAR_INDEX index)
{
    PAR_INFO *p = &ParInfo[index];

    if (index != p->index)
        bug_die("par_info: table out of order: %d != %d", index, p-index);
    return p;
}


/*
 * Server.
 */
static void
server(void)
{
    pid_t pid;

    server_listen();
    for (;;) {
        TEST *test;

        debug("waiting for request");
        if (!server_recv_request())
            continue;
        if (Req.ver_maj != VER_MAJ || Req.ver_min != VER_MIN) {
            int h_maj = Req.ver_maj;
            int h_min = Req.ver_min;
            int h_inc = Req.ver_inc;
            int l_maj = VER_MAJ;
            int l_min = VER_MIN;
            int l_inc = VER_INC;
            char *msg = "upgrade %s from %d.%d.%d to %d.%d.%d";
            char *low = "client";

            if (l_maj > h_maj || (l_maj == h_maj && l_min > h_min)) {
                h_maj = VER_MAJ;
                h_min = VER_MIN;
                h_inc = VER_INC;
                l_maj = Req.ver_maj;
                l_min = Req.ver_min;
                l_inc = Req.ver_inc;
                low   = "server";
            }
            error(msg, low, l_maj, l_min, l_inc, h_maj, h_min, h_inc);
            continue;
        }
        if (Req.req_index >= cardof(Tests)) {
            error("server: bad request index: %d", Req.req_index);
            continue;
        }
        test = &Tests[Req.req_index];
        TestName = test->name;
        debug("request is %s", TestName);
        pid = fork();
        if (pid == 0) {
            init_lstat();
            Finished = 0;
            Successful = 0;
            set_affinity();
            (test->server)();
            stop_timing();
            exit(0);
        } else
            waitpid(pid, 0, 0);
        close(RemoteFD);
    }
    close(ListenFD);
}


/*
 * Listen for any requests.
 */
static void
server_listen(void)
{
    int stat;
    char *service;
    struct addrinfo *r;
    struct addrinfo *res;
    struct addrinfo hints ={
        .ai_flags       = AI_PASSIVE,
        .ai_family      = AF_UNSPEC,
        .ai_socktype    = SOCK_STREAM
    };

    service = qasprintf("%d", ListenPort);
    stat = getaddrinfo(0, service, &hints, &res);
    if (stat != SUCCESS0)
        error_die("getaddrinfo failed: %s", gai_strerror(stat));
    free(service);

    ListenFD = -1;
    for (r = res; r; r = r->ai_next) {
        ListenFD = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (ListenFD >= 0) {
            int one = 1;
            stat = setsockopt(ListenFD, SOL_SOCKET, SO_REUSEADDR,
                                                        &one, sizeof(one));
            if (stat < 0)
                syserror_die("setsockopt failed");
            if (bind(ListenFD, r->ai_addr, r->ai_addrlen) == SUCCESS0)
                break;
            close(ListenFD);
            ListenFD = -1;
        }
    }
    freeaddrinfo(res);
    if (ListenFD < 0)
        error_die("Unable to bind to listen port");

    Req.timeout = ServerTimeout;
    if (listen(ListenFD, LISTENQ) < 0)
        syserror_die("listen failed");
}


/*
 * Accept a request from a client.
 */
static int
server_recv_request(void)
{
    REQ req;
    socklen_t clientLen;
    struct sockaddr_in clientAddr;

    clientLen = sizeof(clientAddr);
    RemoteFD = accept(ListenFD, (struct sockaddr *)&clientAddr, &clientLen);
    if (RemoteFD < 0)
        return syserror("accept failed");
    if (!set_nonblock(RemoteFD))
        goto err;
    if (!recv_mesg(&req, sizeof(req), "request data"))
        goto err;
    dec_init(&req);
    dec_req(&Req);
    return 1;

err:
    close(RemoteFD);
    return 0;
}


/*
 * Client.
 */
static void
client(TEST *test)
{
    int i;

    for (i = 0; i < P_N; ++i)
        ParInfo[i].inuse = 0;
    if (!par_isset(L_NO_MSGS))
        setp_u32(0, L_TIME, 2);
    if (!par_isset(R_NO_MSGS))
        setp_u32(0, R_TIME, 2);
    setp_u32(0, L_TIMEOUT, 5);
    setp_u32(0, R_TIMEOUT, 5);
    par_use(L_AFFINITY);
    par_use(R_AFFINITY);
    par_use(L_TIME);
    par_use(R_TIME);

    set_affinity();
    RReq.ver_maj = VER_MAJ;
    RReq.ver_min = VER_MIN;
    RReq.ver_inc = VER_INC;
    RReq.req_index = test - Tests;
    TestName = test->name;
    debug("sending request %s", TestName);
    init_lstat();
    printf("%s:\n", TestName);
    Finished = 0;
    Successful = 0;
    (*test->client)();
    close(RemoteFD);
    if (!Successful)
        ExitStatus = 1;
    place_show();
}


/*
 * Send a request to the server.
 */
void
client_send_request(void)
{
    REQ req;
    int stat;
    char *service;
    struct addrinfo *r;
    struct addrinfo *res;
    struct addrinfo hints ={
        .ai_family      = AF_UNSPEC,
        .ai_socktype    = SOCK_STREAM
    };

    service = qasprintf("%d", ListenPort);
    stat = getaddrinfo(ServerName, service, &hints, &res);
    if (stat != SUCCESS0)
        error_die("getaddrinfo failed: %s", gai_strerror(stat));
    free(service);

    RemoteFD = -1;
    if (Wait)
        start_timing(Wait);
    for (;;) {
        for (r = res; r; r = r->ai_next) {
            RemoteFD = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
            if (RemoteFD >= 0) {
                if (connect(RemoteFD, r->ai_addr, r->ai_addrlen) == SUCCESS0)
                    break;
                close(RemoteFD);
                RemoteFD = -1;
            }
        }
        if (RemoteFD >= 0 || !Wait || Finished)
            break;
        sleep(1);
    }
    if (Wait)
        stop_timing();
    freeaddrinfo(res);
    if (RemoteFD < 0)
        error_die("Failed to connect");
    if (!set_nonblock(RemoteFD))
        die();
    enc_init(&req);
    enc_req(&RReq);
    if (!send_mesg(&req, sizeof(req), "request data"))
        die();
}


/*
 * Set a file descriptor to non-blocking.
 */
static int
set_nonblock(int fd)
{
    int one = 1;
    if (ioctl(fd, FIONBIO, &one) < 0)
        return syserror("failed to set to non-blocking");
    return 1;
}


/*
 * Synchronize the client and server.
 */
int
synchronize(void)
{
    if (is_client()) {
        if (!send_sync())
            return 0;
        if (!recv_sync())
            return 0;
    } else {
        if (!recv_sync())
            return 0;
        if (!send_sync())
            return 0;
    }
    debug("sync completed");
    start_timing(Req.time);
    return 1;
}


/*
 * Exchange results.  We sync up only to ensure that the client is out of its
 * loop so we can close our socket or whatever communication medium we are
 * using.
 */
void
exchange_results(void)
{
    STAT stat;

    if (!Successful)
        return;
    Successful = 0;
    if (is_client()) {
        if (!recv_mesg(&stat, sizeof(stat), "results"))
            return;
        dec_init(&stat);
        dec_stat(&RStat);
        if (!send_sync())
            return;
    } else {
        enc_init(&stat);
        enc_stat(&LStat);
        if (!send_mesg(&stat, sizeof(stat), "results"))
            return;
        if (!recv_sync())
            return;
    }
    Successful = 1;
}


/*
 * Send a synchronize message.
 */
static int
send_sync(void)
{
    return send_mesg(SYNCMESG, SYNCSIZE, "sync");
}


/*
 * Receive a synchronize message.
 */
static int
recv_sync(void)
{
    char data[SYNCSIZE];

    if (!recv_mesg(data, sizeof(data), "sync"))
        return 0;
    if (memcmp(data, SYNCMESG, SYNCSIZE) != SUCCESS0)
        return error("sync failure: data does not match");
    return 1;
}


/*
 * Send a message to the client.
 */
int
send_mesg(void *ptr, int len, char *item)
{
    debug("sending %s", item);
    return send_recv_mesg('s', item, RemoteFD, ptr, len);
}


/*
 * Receive a response from the server.
 */
int
recv_mesg(void *ptr, int len, char *item)
{
    debug("waiting for %s", item);
    return send_recv_mesg('r', item, RemoteFD, ptr, len);
}


/*
 * Send or receive a message to a file descriptor timing out after a certain
 * amount of time.
 */
static int
send_recv_mesg(int sr, char *item, int fd, char *buf, int len)
{
    typedef ssize_t (IO)(int fd, void *buf, size_t count);
    double  etime;
    fd_set *fdset;
    fd_set  rfdset;
    fd_set  wfdset;
    char   *action;
    IO     *func;

    if (sr == 'r') {
        func = (IO *)read;
        fdset = &rfdset;
        action = "receive";
    } else {
        func = (IO *)write;
        fdset = &wfdset;
        action = "send";
    }

    etime = get_seconds() + Req.timeout;
    while (len) {
        int n;
        double time;
        struct timeval timeval;

        errno = 0;
        time = etime - get_seconds();
        if (time <= 0)
            return error("failed to %s %s: timed out", action, item);
        n = time += 1.0 / (1000*1000);
        timeval.tv_sec  = n;
        timeval.tv_usec = (time-n) * 1000*1000;

        FD_ZERO(&rfdset);
        FD_ZERO(&wfdset);
        FD_SET(fd, fdset);
        if (select(fd+1, &rfdset, &wfdset, 0, &timeval) < 0)
            return syserror("failed to %s %s: select failed", action, item);
        if (!FD_ISSET(fd, fdset))
            continue;
        n = func(fd, buf, len);
        if (n < 0)
            return syserror("failed to %s %s", action, item);
        if (n == 0) {
            char *side = is_client() ? "server" : "client";
            return syserror("failed to %s %s: %s not responding",
                                                        action, item, side);
        }
        len -= n;
    }
    return 1;
}


/*
 * Initialize local status information.
 */
static void
init_lstat(void)
{
    memcpy(&LStat, &IStat, sizeof(LStat));
}


/*
 * Show configuration (client side).
 */
static void
run_client_conf(void)
{
    CONF lconf;
    CONF rconf;

    client_send_request();
    if (!recv_mesg(&rconf, sizeof(rconf), "configuration"))
        return;
    get_conf(&lconf);
    view_strn('a', "", "loc_node",  lconf.node);
    view_strn('a', "", "loc_cpu",   lconf.cpu);
    view_strn('a', "", "loc_os",    lconf.os);
    view_strn('a', "", "loc_qperf", lconf.qperf);
    view_strn('a', "", "rem_node",  rconf.node);
    view_strn('a', "", "rem_cpu",   rconf.cpu);
    view_strn('a', "", "rem_os",    rconf.os);
    view_strn('a', "", "rem_qperf", rconf.qperf);
}


/*
 * Show configuration (server side).
 */
static void
run_server_conf(void)
{
    CONF conf;
    get_conf(&conf);
    send_mesg(&conf, sizeof(conf), "configuration");
}


/*
 * Get configuration.
 */
static void
get_conf(CONF *conf)
{
    struct utsname utsname;

    uname(&utsname);
    strncopy(conf->node, utsname.nodename, sizeof(conf->node));
    snprintf(conf->os, sizeof(conf->os), "%s %s", utsname.sysname,
                                                  utsname.release);
    get_cpu(conf);
    snprintf(conf->qperf, sizeof(conf->qperf), "%d.%d.%d",
                                        VER_MAJ, VER_MIN, VER_INC);
}


/*
 * Get CPU information.
 */
static void
get_cpu(CONF *conf)
{
    char count[STRSIZE];
    char speed[STRSIZE];
    char buf[BUFSIZE];
    char cpu[BUFSIZE];
    char mhz[BUFSIZE];

    int cpus = 0;
    int mixed = 0;
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp)
        error_die("Cannot open /proc/cpuinfo");
    cpu[0] = '\0';
    mhz[0] = '\0';
    while (fgets(buf, sizeof(buf), fp)) {
        int n = strlen(buf);
        if (cmpsub(buf, "model name")) {
            ++cpus;
            if (!mixed) {
                if (cpu[0] == '\0')
                    strncopy(cpu, buf, sizeof(cpu));
                else if (!streq(buf, cpu))
                    mixed = 1;
            }
        } else if (cmpsub(buf, "cpu MHz")) {
            if (!mixed) {
                if (mhz[0] == '\0')
                    strncopy(mhz, buf, sizeof(mhz));
                else if (!streq(buf, mhz))
                    mixed = 1;
            }
        }
        while (n && buf[n-1] != '\n') {
            if (!fgets(buf, sizeof(buf), fp))
                break;
            n = strlen(buf);
        }
    }
    fclose(fp);

    /* CPU name */
    if (mixed)
        strncopy(cpu, "Mixed CPUs", sizeof(cpu));
    else {
        char *p = cpu;
        char *q = skip_colon(cpu);
        if (!q)
            return;
        for (;;) {
            if (*q == '(' && cmpsub(q, "(r)"))
                q += 3;
            else if (*q == '(' && cmpsub(q, "(tm)"))
                q += 4;
            if (tolower(*q) == 'c' && cmpsub(q, "cpu "))
                q += 4;
            if (tolower(*q) == 'p' && cmpsub(q, "processor "))
                q += 10;
            else if (q[0] == ' ' && q[1] == ' ')
                q += 1;
            else if (q[0] == '\n')
                q += 1;
            else if (!(*p++ = *q++))
                break;
        }
    }

    /* CPU speed */
    speed[0] = '\0';
    if (!mixed) {
        int n = strlen(cpu);
        if (n < 3 || cpu[n-2] != 'H' || cpu[n-1] != 'z') {
            char *q = skip_colon(mhz);
            if (q) {
                int freq = atoi(q);
                if (freq < 1000)
                    snprintf(speed, sizeof(speed), " %dMHz", freq);
                else
                    snprintf(speed, sizeof(speed), " %.1fGHz", freq/1000.0);
            }
        }
    }

    /* Number of CPUs */
    if (cpus == 1)
        count[0] = '\0';
    else if (cpus == 2)
        snprintf(count, sizeof(count), "Dual-Core ");
    else if (cpus == 4)
        snprintf(count, sizeof(count), "Quad-Core ");
    else
        snprintf(count, sizeof(count), "%d-Core ", cpus);

    snprintf(conf->cpu, sizeof(conf->cpu), "%s%s%s", count, cpu, speed);
}


/*
 * Quit (client side).
 */
static void
run_client_quit(void)
{
    opt_check();
    client_send_request();
    synchronize();
    exit(0);
}


/*
 * Quit (server side).  The read is to ensure that the client first quits to
 * ensure that everything closes down cleanly.
 */
static void
run_server_quit(void)
{
    char buf[1];

    synchronize();
    read(RemoteFD, buf, sizeof(buf));
    exit(0);
}


/*
 * Start timing.
 */
static void
start_timing(int seconds)
{
    struct itimerval itimerval = {{0}};

    get_times(LStat.time_s);
    setitimer(ITIMER_REAL, &itimerval, 0);
    if (!seconds)
        return;

    debug("starting timer");
    itimerval.it_value.tv_sec = seconds;
    itimerval.it_interval.tv_usec = 1;
    setitimer(ITIMER_REAL, &itimerval, 0);
}


/*
 * Stop timing.  Note that the end time is obtained by the first call to
 * set_finished.  In the tests, usually, when SIGALRM goes off, it is executing
 * a read or write system call which gets interrupted.  If SIGALRM goes off
 * after Finished is checked but before the system call is performed, the
 * system call will be executed and it will take the second SIGALRM call
 * generated by the interval timer to wake it up.  Hence, we save the end times
 * in sig_alrm.  Note that if Finished is set, we reject any packets that are
 * sent or arrive in order not to cheat.
 */
void
stop_timing(void)
{
    struct itimerval itimerval = {{0}};

    set_finished();
    setitimer(ITIMER_REAL, &itimerval, 0);
    debug("stopping timer");
}


/*
 * Establish the current test as finished.
 */
void
set_finished(void)
{
    if (Finished++ == 0)
        get_times(LStat.time_e);
}


/*
 * Show results.
 */
void
show_results(MEASURE measure)
{
    calc_results();
    show_info(measure);
}


/*
 * Calculate results.
 */
static void
calc_results(void)
{
    double no_msgs;
    double locTime;
    double remTime;
    double midTime;
    double gB = 1000 * 1000 * 1000;

    if (!Successful)
        return;

    add_ustat(&LStat.s, &RStat.rem_s);
    add_ustat(&LStat.r, &RStat.rem_r);
    add_ustat(&RStat.s, &LStat.rem_s);
    add_ustat(&RStat.r, &LStat.rem_r);

    memset(&Res, 0, sizeof(Res));
    calc_node(&Res.l, &LStat);
    calc_node(&Res.r, &RStat);
    no_msgs = LStat.r.no_msgs + RStat.r.no_msgs;
    if (no_msgs)
        Res.latency = Res.l.time_real / no_msgs;

    locTime = Res.l.time_real;
    remTime = Res.r.time_real;
    midTime = (locTime + remTime) / 2;

    if (locTime == 0 || remTime == 0)
        return;

    /* Calculate messaging rate */
    if (!RStat.r.no_msgs)
        Res.msg_rate = LStat.r.no_msgs / remTime;
    else if (!LStat.r.no_msgs)
        Res.msg_rate = RStat.r.no_msgs / locTime;
    else
        Res.msg_rate = (LStat.r.no_msgs + RStat.r.no_msgs) / midTime;

    /* Calculate send bandwidth */
    if (!RStat.s.no_bytes)
        Res.send_bw = LStat.s.no_bytes / locTime;
    else if (!LStat.s.no_bytes)
        Res.send_bw = RStat.s.no_bytes / remTime;
    else
        Res.send_bw = (LStat.s.no_bytes + RStat.s.no_bytes) / midTime;

    /* Calculate receive bandwidth. */
    if (!RStat.r.no_bytes)
        Res.recv_bw = LStat.r.no_bytes / locTime;
    else if (!LStat.r.no_bytes)
        Res.recv_bw = RStat.r.no_bytes / remTime;
    else
        Res.recv_bw = (LStat.r.no_bytes + RStat.r.no_bytes) / midTime;

    /* Calculate costs */
    if (LStat.s.no_bytes && !LStat.r.no_bytes && !RStat.s.no_bytes)
        Res.send_cost = Res.l.time_cpu*gB / LStat.s.no_bytes;
    else if (RStat.s.no_bytes && !RStat.r.no_bytes && !LStat.s.no_bytes)
        Res.send_cost = Res.r.time_cpu*gB / RStat.s.no_bytes;
    if (RStat.r.no_bytes && !RStat.s.no_bytes && !LStat.r.no_bytes)
        Res.recv_cost = Res.r.time_cpu*gB / RStat.r.no_bytes;
    else if (LStat.r.no_bytes && !LStat.s.no_bytes && !RStat.r.no_bytes)
        Res.recv_cost = Res.l.time_cpu*gB / LStat.r.no_bytes;
}


/*
 * Determine the number of packets left to send.
 */
int
left_to_send(long *sentp, int room)
{
    int n;

    if (!Req.no_msgs)
        return room;
    n = Req.no_msgs - *sentp;
    if (n <= 0)
        return 0;
    if (n > room)
        return room;
    return n;
}


/*
 * Touch data.
 */
void
touch_data(void *p, int n)
{
    uint64_t a;
    volatile uint64_t *p64 = p;

    while (n >= sizeof(*p64)) {
        a = *p64++;
        n -= sizeof(*p64);
    }
    if (n) {
        volatile uint8_t *p8 = (uint8_t *)p64;
        while (n >= sizeof(*p8)) {
            a = *p8++;
            n -= sizeof(*p8);
        }
    }
}


/*
 * Combine statistics that the remote node kept track of with those that the
 * local node kept.
 */
static void
add_ustat(USTAT *l, USTAT *r)
{
    l->no_bytes += r->no_bytes;
    l->no_msgs  += r->no_msgs;
    l->no_errs  += r->no_errs;
}


/*
 * Calculate time values for a node.
 */
static void
calc_node(RESN *resn, STAT *stat)
{
    int i;
    CLOCK cpu;
    double s = stat->time_e[T_REAL] - stat->time_s[T_REAL];

    memset(resn, 0, sizeof(*resn));
    if (s == 0)
        return;
    if (stat->no_ticks == 0)
        return;

    resn->time_real = s / stat->no_ticks;

    cpu = 0;
    for (i = 0; i < T_N; ++i)
        if (i != T_REAL && i != T_IDLE)
            cpu += stat->time_e[i] - stat->time_s[i];
    resn->time_cpu = (float) cpu / stat->no_ticks;

    resn->cpu_user = (stat->time_e[T_USER] - stat->time_s[T_USER]
                   + stat->time_e[T_NICE] - stat->time_s[T_NICE]) / s;

    resn->cpu_intr = (stat->time_e[T_IRQ] - stat->time_s[T_IRQ]
                   +  stat->time_e[T_SOFTIRQ] - stat->time_s[T_SOFTIRQ]) / s;

    resn->cpu_idle = (stat->time_e[T_IDLE] - stat->time_s[T_IDLE]) / s;

    resn->cpu_kernel = (stat->time_e[T_KERNEL] - stat->time_s[T_KERNEL]
                     +  stat->time_e[T_STEAL] - stat->time_s[T_STEAL]) / s;

    resn->cpu_io_wait = (stat->time_e[T_IOWAIT] - stat->time_s[T_IOWAIT]) / s;

    resn->cpu_total = resn->cpu_user + resn->cpu_intr
                    + resn->cpu_kernel + resn->cpu_io_wait;
}


/*
 * Show relevant values.
 */
static void
show_info(MEASURE measure)
{
    if (!Successful)
        return;
    if (measure == LATENCY) {
        view_time('a', "", "latency", Res.latency);
        view_rate('s', "", "msg_rate", Res.msg_rate);
    } else if (measure == MSG_RATE) {
        view_rate('a', "", "msg_rate", Res.msg_rate);
    } else if (measure == BANDWIDTH) {
        view_band('a', "", "bw", Res.recv_bw);
        view_rate('s', "", "msg_rate", Res.msg_rate);
    } else if (measure == BANDWIDTH_SR) {
        view_band('a', "", "send_bw", Res.send_bw);
        view_band('a', "", "recv_bw", Res.recv_bw);
        view_rate('s', "", "msg_rate", Res.msg_rate);
    }
    show_used();
    view_cost('t', "", "send_cost", Res.send_cost);
    view_cost('t', "", "recv_cost", Res.recv_cost);
    show_rest();
    if (Debug)
        show_debug();
}


/*
 * Show parameters the user set.
 */
static void
show_used(void)
{
    PAR_NAME *p;
    PAR_NAME *q = endof(ParName);

    if (!VerboseUsed)
        return;
    for (p = ParName; p < q; ++p) {
        PAR_INFO *l = par_info(p->loc_i);
        PAR_INFO *r = par_info(p->rem_i);

        if (!l->inuse && !r->inuse)
            continue;
        if (VerboseUsed < 2 && !l->set & !r->set)
            continue;
        if (l->type == 'l') {
            uint32_t lv = *(uint32_t *)l->ptr;
            uint32_t rv = *(uint32_t *)r->ptr;
            if (lv == rv)
                view_long('u', "", p->name, lv);
            else {
                view_long('u', "loc_", p->name, lv);
                view_long('u', "rem_", p->name, rv);
            }
        } else if (l->type == 'p') {
            if (streq(l->ptr, r->ptr))
                view_strn('u', "", p->name, l->ptr);
            else {
                view_strn('u', "loc_", p->name, l->ptr);
                view_strn('u', "rem_", p->name, r->ptr);
            }
        } else if (l->type == 's') {
            uint32_t lv = *(uint32_t *)l->ptr;
            uint32_t rv = *(uint32_t *)r->ptr;
            if (lv == rv)
                view_size('u', "", p->name, lv);
            else {
                view_size('u', "loc_", p->name, lv);
                view_size('u', "rem_", p->name, rv);
            }
        } else if (l->type == 't') {
            uint32_t lv = *(uint32_t *)l->ptr;
            uint32_t rv = *(uint32_t *)r->ptr;
            if (lv == rv)
                view_time('u', "", p->name, lv);
            else {
                view_time('u', "loc_", p->name, lv);
                view_time('u', "rem_", p->name, rv);
            }
        }
    }
}


/*
 * Show the remaining parameters.
 */
static void
show_rest(void)
{
    RESN *resnS;
    RESN *resnR;
    STAT *statS;
    STAT *statR;
    int srmode = 0;

    if (!UnifyNodes) {
        uint64_t ls = LStat.s.no_bytes;
        uint64_t lr = LStat.r.no_bytes;
        uint64_t rs = RStat.s.no_bytes;
        uint64_t rr = RStat.r.no_bytes;
        
        if (ls && !rs && rr && !lr) {
            srmode = 1;
            resnS = &Res.l;
            resnR = &Res.r;
            statS = &LStat;
            statR = &RStat;
        } else if (rs && !ls && lr && !rr) {
            srmode = 1;
            resnS = &Res.r;
            resnR = &Res.l;
            statS = &RStat;
            statR = &LStat;
        }
    }

    if (srmode) {
        view_cpus('t', "", "send_cpus_used",   resnS->cpu_total);
        view_cpus('T', "", "send_cpus_user",   resnS->cpu_user);
        view_cpus('T', "", "send_cpus_intr",   resnS->cpu_intr);
        view_cpus('T', "", "send_cpus_kernel", resnS->cpu_kernel);
        view_cpus('T', "", "send_cpus_iowait", resnS->cpu_io_wait);
        view_time('T', "", "send_real_time",   resnS->time_real);
        view_time('T', "", "send_cpu_time",    resnS->time_cpu);
        view_long('S', "", "send_errors",      statS->s.no_errs);
        view_size('S', "", "send_bytes",       statS->s.no_bytes);
        view_long('S', "", "send_msgs",        statS->s.no_msgs);
        view_long('S', "", "send_max_cqe",     statS->max_cqes);

        view_cpus('t', "", "recv_cpus_used",   resnR->cpu_total);
        view_cpus('T', "", "recv_cpus_user",   resnR->cpu_user);
        view_cpus('T', "", "recv_cpus_intr",   resnR->cpu_intr);
        view_cpus('T', "", "recv_cpus_kernel", resnR->cpu_kernel);
        view_cpus('T', "", "recv_cpus_iowait", resnR->cpu_io_wait);
        view_time('T', "", "recv_real_time",   resnR->time_real);
        view_time('T', "", "recv_cpu_time",    resnR->time_cpu);
        view_long('S', "", "recv_errors",      statR->r.no_errs);
        view_size('S', "", "recv_bytes",       statR->r.no_bytes);
        view_long('S', "", "recv_msgs",        statR->r.no_msgs);
        view_long('S', "", "recv_max_cqe",     statR->max_cqes);
    } else {
        view_cpus('t', "", "loc_cpus_used",    Res.l.cpu_total);
        view_cpus('T', "", "loc_cpus_user",    Res.l.cpu_user);
        view_cpus('T', "", "loc_cpus_intr",    Res.l.cpu_intr);
        view_cpus('T', "", "loc_cpus_kernel",  Res.l.cpu_kernel);
        view_cpus('T', "", "loc_cpus_iowait",  Res.l.cpu_io_wait);
        view_time('T', "", "loc_real_time",    Res.l.time_real);
        view_time('T', "", "loc_cpu_time",     Res.l.time_cpu);
        view_long('S', "", "loc_send_errors",  LStat.s.no_errs);
        view_long('S', "", "loc_recv_errors",  LStat.r.no_errs);
        view_size('S', "", "loc_send_bytes",   LStat.s.no_bytes);
        view_size('S', "", "loc_recv_bytes",   LStat.r.no_bytes);
        view_long('S', "", "loc_send_msgs",    LStat.s.no_msgs);
        view_long('S', "", "loc_recv_msgs",    LStat.r.no_msgs);
        view_long('S', "", "loc_max_cqe",      LStat.max_cqes);

        view_cpus('t', "", "rem_cpus_used",    Res.r.cpu_total);
        view_cpus('T', "", "rem_cpus_user",    Res.r.cpu_user);
        view_cpus('T', "", "rem_cpus_intr",    Res.r.cpu_intr);
        view_cpus('T', "", "rem_cpus_kernel",  Res.r.cpu_kernel);
        view_cpus('T', "", "rem_cpus_iowait",  Res.r.cpu_io_wait);
        view_time('T', "", "rem_real_time",    Res.r.time_real);
        view_time('T', "", "rem_cpu_time",     Res.r.time_cpu);
        view_long('S', "", "rem_send_errors",  RStat.s.no_errs);
        view_long('S', "", "rem_recv_errors",  RStat.r.no_errs);
        view_size('S', "", "rem_send_bytes",   RStat.s.no_bytes);
        view_size('S', "", "rem_recv_bytes",   RStat.r.no_bytes);
        view_long('S', "", "rem_send_msgs",    RStat.s.no_msgs);
        view_long('S', "", "rem_recv_msgs",    RStat.r.no_msgs);
        view_long('S', "", "rem_max_cqe",      RStat.max_cqes);
    }
}


/*
 * Show all values.
 */
static void
show_debug(void)
{
    /* Local node */
    view_long('d', "", "l_no_cpus",  LStat.no_cpus);
    view_long('d', "", "l_no_ticks", LStat.no_ticks);
    view_long('d', "", "l_max_cqes", LStat.max_cqes);

    if (LStat.no_ticks) {
        double t = LStat.no_ticks;
        CLOCK *s = LStat.time_s;
        CLOCK *e = LStat.time_e;
        double real    = (e[T_REAL]    - s[T_REAL])    / t;
        double user    = (e[T_USER]    - s[T_USER])    / t;
        double nice    = (e[T_NICE]    - s[T_NICE])    / t;
        double system  = (e[T_KERNEL]  - s[T_KERNEL])  / t;
        double idle    = (e[T_IDLE]    - s[T_IDLE])    / t;
        double iowait  = (e[T_IOWAIT]  - s[T_IOWAIT])  / t;
        double irq     = (e[T_IRQ]     - s[T_IRQ])     / t;
        double softirq = (e[T_SOFTIRQ] - s[T_SOFTIRQ]) / t;
        double steal   = (e[T_STEAL]   - s[T_STEAL])   / t;

        view_time('d', "", "l_timer_real",    real);
        view_time('d', "", "l_timer_user",    user);
        view_time('d', "", "l_timer_nice",    nice);
        view_time('d', "", "l_timer_system",  system);
        view_time('d', "", "l_timer_idle",    idle);
        view_time('d', "", "l_timer_iowait",  iowait);
        view_time('d', "", "l_timer_irq",     irq);
        view_time('d', "", "l_timer_softirq", softirq);
        view_time('d', "", "l_timer_steal",   steal);
    }

    view_size('d', "", "l_s_no_bytes", LStat.s.no_bytes);
    view_long('d', "", "l_s_no_msgs",  LStat.s.no_msgs);
    view_long('d', "", "l_s_no_errs",  LStat.s.no_errs);

    view_size('d', "", "l_r_no_bytes", LStat.r.no_bytes);
    view_long('d', "", "l_r_no_msgs",  LStat.r.no_msgs);
    view_long('d', "", "l_r_no_errs",  LStat.r.no_errs);

    view_size('d', "", "l_rem_s_no_bytes", LStat.rem_s.no_bytes);
    view_long('d', "", "l_rem_s_no_msgs",  LStat.rem_s.no_msgs);
    view_long('d', "", "l_rem_s_no_errs",  LStat.rem_s.no_errs);

    view_size('d', "", "l_rem_r_no_bytes", LStat.rem_r.no_bytes);
    view_long('d', "", "l_rem_r_no_msgs",  LStat.rem_r.no_msgs);
    view_long('d', "", "l_rem_r_no_errs",  LStat.rem_r.no_errs);

    /* Remote node */
    view_long('d', "", "r_no_cpus",  RStat.no_cpus);
    view_long('d', "", "r_no_ticks", RStat.no_ticks);
    view_long('d', "", "r_max_cqes", RStat.max_cqes);

    if (RStat.no_ticks) {
        double t = RStat.no_ticks;
        CLOCK *s = RStat.time_s;
        CLOCK *e = RStat.time_e;

        double real    = (e[T_REAL]    - s[T_REAL])    / t;
        double user    = (e[T_USER]    - s[T_USER])    / t;
        double nice    = (e[T_NICE]    - s[T_NICE])    / t;
        double system  = (e[T_KERNEL]  - s[T_KERNEL])  / t;
        double idle    = (e[T_IDLE]    - s[T_IDLE])    / t;
        double iowait  = (e[T_IOWAIT]  - s[T_IOWAIT])  / t;
        double irq     = (e[T_IRQ]     - s[T_IRQ])     / t;
        double softirq = (e[T_SOFTIRQ] - s[T_SOFTIRQ]) / t;
        double steal   = (e[T_STEAL]   - s[T_STEAL])   / t;

        view_time('d', "", "r_timer_real",    real);
        view_time('d', "", "r_timer_user",    user);
        view_time('d', "", "r_timer_nice",    nice);
        view_time('d', "", "r_timer_system",  system);
        view_time('d', "", "r_timer_idle",    idle);
        view_time('d', "", "r_timer_iowait",  iowait);
        view_time('d', "", "r_timer_irq",     irq);
        view_time('d', "", "r_timer_softirq", softirq);
        view_time('d', "", "r_timer_steal",   steal);
    }

    view_size('d', "", "r_s_no_bytes", RStat.s.no_bytes);
    view_long('d', "", "r_s_no_msgs",  RStat.s.no_msgs);
    view_long('d', "", "r_s_no_errs",  RStat.s.no_errs);

    view_size('d', "", "r_r_no_bytes", RStat.r.no_bytes);
    view_long('d', "", "r_r_no_msgs",  RStat.r.no_msgs);
    view_long('d', "", "r_r_no_errs",  RStat.r.no_errs);

    view_size('d', "", "r_rem_s_no_bytes", RStat.rem_s.no_bytes);
    view_long('d', "", "r_rem_s_no_msgs",  RStat.rem_s.no_msgs);
    view_long('d', "", "r_rem_s_no_errs",  RStat.rem_s.no_errs);

    view_size('d', "", "r_rem_r_no_bytes", RStat.rem_r.no_bytes);
    view_long('d', "", "r_rem_r_no_msgs",  RStat.rem_r.no_msgs);
    view_long('d', "", "r_rem_r_no_errs",  RStat.rem_r.no_errs);
}


/*
 * Show a cost in terms of seconds per gigabyte.
 */
static void
view_cost(int type, char *pref, char *name, double value)
{
    int n = 0;
    static char *tab[] ={ "ns/GB", "us/GB", "ms/GB", "sec/GB" };

    value *=  1E9;
    if (!verbose(type, value))
        return;
    if (!UnifyUnits) {
        while (value >= 1000 && n < (int)cardof(tab)-1) {
            value /= 1000;
            ++n;
        }
    }
    place_val(pref, name, tab[n], value);
}


/*
 * Show the number of cpus.
 */
static void
view_cpus(int type, char *pref, char *name, double value)
{
    value *= 100;
    if (!verbose(type, value))
        return;
    place_val(pref, name, "% cpus", value);
}


/*
 * Show a messaging rate.
 */
static void
view_rate(int type, char *pref, char *name, double value)
{
    int n = 0;
    static char *tab[] ={ "/sec", "K/sec", "M/sec", "G/sec", "T/sec" };

    if (!verbose(type, value))
        return;
    if (!UnifyUnits) {
        while (value >= 1000 && n < (int)cardof(tab)-1) {
            value /= 1000;
            ++n;
        }
    }
    place_val(pref, name, tab[n], value);
}


/*
 * Show a number.
 */
static void
view_long(int type, char *pref, char *name, long long value)
{
    int n = 0;
    double val = value;
    static char *tab[] ={ "", "thousand", "million", "billion", "trillion" };

    if (!verbose(type, val))
        return;
    if (!UnifyUnits && val >= 1000*1000) {
        while (val >= 1000 && n < (int)cardof(tab)-1) {
            val /= 1000;
            ++n;
        }
    }
    place_val(pref, name, tab[n], val);
}


/*
 * Show a bandwidth value.
 */
static void
view_band(int type, char *pref, char *name, double value)
{
    int n = 0;
    static char *tab[] ={
        "bytes/sec", "KB/sec", "MB/sec", "GB/sec", "TB/sec"
    };

    if (!verbose(type, value))
        return;
    if (!UnifyUnits) {
        while (value >= 1000 && n < (int)cardof(tab)-1) {
            value /= 1000;
            ++n;
        }
    }
    place_val(pref, name, tab[n], value);
}


/*
 * Show a size.
 */
static void
view_size(int type, char *pref, char *name, long long value)
{
    int n = 0;
    double val = value;
    static char *tab[] ={ "bytes", "KB", "MB", "GB", "TB" };

    if (!verbose(type, val))
        return;
    if (!UnifyUnits) {
        if (nice_1024(pref, name, value))
            return;
        while (val >= 1000 && n < (int)cardof(tab)-1) {
            val /= 1000;
            ++n;
        }
    }
    place_val(pref, name, tab[n], val);
}


/*
 * Show a number if it can be expressed as a nice multiple of a power of 1024.
 */
static int
nice_1024(char *pref, char *name, long long value)
{
    char *data;
    char *altn;
    int n = 0;
    long long val = value;
    static char *tab[] ={ "KiB", "MiB", "GiB", "TiB" };

    if (val < 1024 || val % 1024)
        return 0;
    val /= 1024;
    while (val >= 1024 && n < (int)cardof(tab)-1) {
        if (val % 1024)
            return 0;
        val /= 1024;
        ++n;
    }
    data = qasprintf("%lld", val);
    altn = qasprintf("%lld", value);
    place_any(pref, name, tab[n], commify(data), commify(altn));
    return 1;
}


/*
 * Show a string.
 */
static void
view_strn(int type, char *pref, char *name, char *value)
{
    if (!verbose(type, value[0] != '\0'))
        return;
    place_any(pref, name, 0, strdup(value), 0);
}


/*
 * Show a time.
 */
static void
view_time(int type, char *pref, char *name, double value)
{
    int n = 0;
    static char *tab[] ={ "ns", "us", "ms", "sec" };

    value *= 1E9;
    if (!verbose(type, value))
        return;
    if (!UnifyUnits) {
        while (value >= 1000 && n < (int)cardof(tab)-1) {
            value /= 1000;
            ++n;
        }
    }
    place_val(pref, name, tab[n], value);
}


/*
 * Determine if we are verbose enough to show a value.
 */
static int
verbose(int type, double value)
{
    if (type == 'a')
        return 1;
    if (value <= 0)
        return 0;
    switch (type) {
    case 'd': return Debug;
    case 'c': return VerboseConf >= 1;
    case 's': return VerboseStat >= 1;
    case 't': return VerboseTime >= 1;
    case 'u': return VerboseUsed >= 1;
    case 'C': return VerboseConf >= 2;
    case 'S': return VerboseStat >= 2;
    case 'T': return VerboseTime >= 2;
    case 'U': return VerboseUsed >= 2;
    default:  bug_die("verbose: bad type: %c (%o)", type, type);
    }
    return 0;
}


/*
 * Place a value to be shown later.
 */
static void
place_val(char *pref, char *name, char *unit, double value)
{
    char *data = qasprintf("%.0f", value);
    char *p    = data;
    int   n    = Precision;

    if (*p == '-')
        ++p;
    while (isdigit(*p++))
        --n;
    if (n > 0) {
        free(data);
        data = qasprintf("%.*f", n, value);
        p = &data[strlen(data)];
        while (p > data && *--p == '0')
            ;
        if (p > data && *p == '.')
            --p;
        p[1] = '\0';
    }
    place_any(pref, name, unit, commify(data), 0);
}


/*
 * Place an entry in our show table.
 */
static void
place_any(char *pref, char *name, char *unit, char *data, char *altn)
{
    SHOW *show = &ShowTable[ShowIndex++];
    if (ShowIndex > cardof(ShowTable))
        bug_die("Need to increase size of ShowTable");
    show->pref = pref;
    show->name = name;
    show->unit = unit;
    show->data = data;
    show->altn = altn;
}


/*
 * Show all saved values.
 */
static void
place_show(void)
{
    int i;
    int nameLen = 0;
    int dataLen = 0;
    int unitLen = 0;

    /* First compute formating sizes */
    for (i = 0; i < ShowIndex; ++i) {
        int n;
        SHOW *show = &ShowTable[i];
        n = (show->pref ? strlen(show->pref) : 0) + strlen(show->name);
        if (n > nameLen)
            nameLen = n;
        n = strlen(show->data);
        if (show->unit) {
            if (n > dataLen)
                dataLen = n;
            n = strlen(show->unit);
            if (n > unitLen)
                unitLen = n;
        }
    }

    /* Then display results */
    for (i = 0; i < ShowIndex; ++i) {
        int n = 0;
        SHOW *show = &ShowTable[i];

        printf("    ");
        if (show->pref) {
            n = strlen(show->pref);
            printf("%s", show->pref);
        }
        printf("%-*s", nameLen-n, show->name);
        if (show->unit) {
            printf("  =  %*s", dataLen, show->data);
            printf(" %s", show->unit);
        } else
            printf("  =  %s", show->data);
        if (show->altn)
            printf(" (%s)", show->altn);
        printf("\n");
        free(show->data);
        free(show->altn);
    }
    ShowIndex = 0;
}


/*
 * Set the processor affinity.
 */
static void
set_affinity(void)
{
    cpu_set_t set;
    int a = Req.affinity;

    if (!a)
        return;
    CPU_ZERO(&set);
    CPU_SET(a-1, &set);
    if (sched_setaffinity(0, sizeof(set), &set) < 0)
        syserror_die("Cannot set processor affinity (cpu %d)", a-1);
}


/*
 * Encode a REQ structure into a data stream.
 */
static void
enc_req(REQ *host)
{
    enc_int(host->ver_maj,       sizeof(host->ver_maj));
    enc_int(host->ver_min,       sizeof(host->ver_min));
    enc_int(host->ver_inc,       sizeof(host->ver_inc));
    enc_int(host->req_index,     sizeof(host->req_index));
    enc_int(host->flip,          sizeof(host->flip));
    enc_int(host->access_recv,   sizeof(host->access_recv));
    enc_int(host->affinity,      sizeof(host->affinity));
    enc_int(host->poll_mode,     sizeof(host->poll_mode));
    enc_int(host->port,          sizeof(host->port));
    enc_int(host->rd_atomic,     sizeof(host->rd_atomic));
    enc_int(host->timeout,       sizeof(host->timeout));
    enc_int(host->msg_size,      sizeof(host->msg_size));
    enc_int(host->mtu_size,      sizeof(host->mtu_size));
    enc_int(host->no_msgs,       sizeof(host->no_msgs));
    enc_int(host->sock_buf_size, sizeof(host->sock_buf_size));
    enc_int(host->time,          sizeof(host->time));
    enc_str(host->id,            sizeof(host->id));
}


/*
 * Decode a REQ structure from a data stream.
 */
static void
dec_req(REQ *host)
{
    host->ver_maj       = dec_int(sizeof(host->ver_maj));
    host->ver_min       = dec_int(sizeof(host->ver_min));
    host->ver_inc       = dec_int(sizeof(host->ver_inc));
    host->req_index     = dec_int(sizeof(host->req_index));
    host->flip          = dec_int(sizeof(host->flip));
    host->access_recv   = dec_int(sizeof(host->access_recv));
    host->affinity      = dec_int(sizeof(host->affinity));
    host->poll_mode     = dec_int(sizeof(host->poll_mode));
    host->port          = dec_int(sizeof(host->port));
    host->rd_atomic     = dec_int(sizeof(host->rd_atomic));
    host->timeout       = dec_int(sizeof(host->timeout));
    host->msg_size      = dec_int(sizeof(host->msg_size));
    host->mtu_size      = dec_int(sizeof(host->mtu_size));
    host->no_msgs       = dec_int(sizeof(host->no_msgs));
    host->sock_buf_size = dec_int(sizeof(host->sock_buf_size));
    host->time          = dec_int(sizeof(host->time));
    dec_str(host->id, sizeof(host->id));
}


/*
 * Encode a STAT structure into a data stream.
 */
static void
enc_stat(STAT *host)
{
    int i;

    enc_int(host->no_cpus,  sizeof(host->no_cpus));
    enc_int(host->no_ticks, sizeof(host->no_ticks));
    enc_int(host->max_cqes, sizeof(host->max_cqes));
    for (i = 0; i < T_N; ++i)
        enc_int(host->time_s[i], sizeof(host->time_s[i]));
    for (i = 0; i < T_N; ++i)
        enc_int(host->time_e[i], sizeof(host->time_e[i]));
    enc_ustat(&host->s);
    enc_ustat(&host->r);
    enc_ustat(&host->rem_s);
    enc_ustat(&host->rem_r);
}


/*
 * Decode a STAT structure from a data stream.
 */
static void
dec_stat(STAT *host)
{
    int i;

    host->no_cpus  = dec_int(sizeof(host->no_cpus));
    host->no_ticks = dec_int(sizeof(host->no_ticks));
    host->max_cqes = dec_int(sizeof(host->max_cqes));
    for (i = 0; i < T_N; ++i)
        host->time_s[i] = dec_int(sizeof(host->time_s[i]));
    for (i = 0; i < T_N; ++i)
        host->time_e[i] = dec_int(sizeof(host->time_e[i]));
    dec_ustat(&host->s);
    dec_ustat(&host->r);
    dec_ustat(&host->rem_s);
    dec_ustat(&host->rem_r);
}


/*
 * Encode a USTAT structure into a data stream.
 */
static void
enc_ustat(USTAT *host)
{
    enc_int(host->no_bytes, sizeof(host->no_bytes));
    enc_int(host->no_msgs,  sizeof(host->no_msgs));
    enc_int(host->no_errs,  sizeof(host->no_errs));
}


/*
 * Decode a USTAT structure from a data stream.
 */
static void
dec_ustat(USTAT *host)
{
    host->no_bytes = dec_int(sizeof(host->no_bytes));
    host->no_msgs  = dec_int(sizeof(host->no_msgs));
    host->no_errs  = dec_int(sizeof(host->no_errs));
}


/*
 * Initialize encode pointer.
 */
void
enc_init(void *p)
{
    EncodePtr = p;
}


/*
 * Initialize decode pointer.
 */
void
dec_init(void *p)
{
    DecodePtr = p;
}


/*
 * Encode a string.
 */
void
enc_str(char *s, int n)
{
    memcpy(EncodePtr, s, n);
    EncodePtr += n;
}


/*
 * Decode a string.
 */
void
dec_str(char *s, int  n)
{
    memcpy(s, DecodePtr, n);
    DecodePtr += n;
}


/*
 * Encode an integer.
 */
void
enc_int(int64_t l, int n)
{
    while (n--) {
        *EncodePtr++ = l;
        l >>= 8;
    }
}


/*
 * Decode an integer.
 */
int64_t
dec_int(int n)
{
    uint64_t l = 0;
    uint8_t *p = (DecodePtr += n);
    while (n--)
        l = (l << 8) | (*--p & 0xFF);
    return l;
}


/*
 * Get various temporal parameters.
 */
static void
get_times(CLOCK timex[T_N])
{
    int n;
    char *p;
    char buf[BUFSIZE];
    struct tms tms;

    timex[0] = times(&tms);
    if (lseek(ProcStatFD, 0, 0) < 0)
        syserror_die("Failed to seek /proc/stat");
    n = read(ProcStatFD, buf, sizeof(buf)-1);
    buf[n] = '\0';
    if (strncmp(buf, "cpu ", 4))
        error_die("/proc/stat does not start with 'cpu '");
    p = &buf[3];
    for (n = 1; n < T_N; ++n) {
        while (*p == ' ')
            ++p;
        if (!isdigit(*p)) {
            if (*p != '\n' || n < T_N-1)
                error_die("/proc/stat has bad format");
            break;
        }
        timex[n] = strtoll(p, 0, 10);
        while (*p != ' ' && *p != '\n' && *p != '\0')
            ++p;
    }
    while (n < T_N)
        timex[n++] = 0;
}


/*
 * Get the time of day in seconds as a floating point number.
 */
static double
get_seconds(void)
{
    struct timeval timeval;

    if (gettimeofday(&timeval, 0) < 0)
        syserror_die("gettimeofday failed");
    return timeval.tv_sec + timeval.tv_usec/(1000.0*1000.0);
}


/*
 * Insert commas within a number for readability.
 */
static char *
commify(char *data)
{
    int s;
    int d;
    int seqS;
    int seqE;
    int dataLen;
    int noCommas;

    if (!data)
        return data;
    if (UnifyUnits)
        return data;
    dataLen = strlen(data);
    seqS = seqE = dataLen;
    while (--seqS >= 0)
        if (!isdigit(data[seqS]))
            break;
    if (seqS >= 0 && data[seqS] == '.') {
        seqE = seqS;
        while (--seqS >= 0)
            if (!isdigit(data[seqS]))
                break;
    }
    noCommas = (--seqE - ++seqS) / 3;
    if (noCommas == 0)
        return data;
    data = realloc(data, dataLen+noCommas+1);
    if (!data)
        error_die("Out of space");
    s = dataLen;
    d = dataLen + noCommas;
    for (;;) {
        int n;
        data[d--] = data[s--];
        n = seqE - s;
        if (n > 0 && n%3 == 0) {
            data[d--] = ',';
            if (--noCommas == 0)
                break;
        }
    }
    return data;
}


/*
 * Like strncpy but ensures the destination is null terminated.
 */
static void
strncopy(char *d, char *s, int n)
{
    strncpy(d, s, n);
    d[n-1] = '\0';
}


/*
 * Call malloc and panic on error.
 */
void *
qmalloc(long n)
{
    void *p = malloc(n);
    if (!p)
        error_die("Out of space");
    return p;
}


/*
 * Print out an error message and exit.
 */
static char *
qasprintf(char *fmt, ...)
{
    int stat;
    char *str;
    va_list alist;

    va_start(alist, fmt);
    stat = vasprintf(&str, fmt, alist);
    va_end(alist);
    if (stat < 0)
        error_die("Out of space");
    return str;
}


/*
 * Print out a debug message.
 */
void
debug(char *fmt, ...)
{
    va_list alist;

    if (!Debug)
        return;
    va_start(alist, fmt);
    vfprintf(stderr, fmt, alist);
    va_end(alist);
    fprintf(stderr, "\n");
}


/*
 * Print out an error message.
 */
int
error(char *fmt, ...)
{
    va_list alist;

    va_start(alist, fmt);
    vfprintf(stderr, fmt, alist);
    va_end(alist);
    fprintf(stderr, "\n");
    return 0;
}


/*
 * Print out an error message and exit.
 */
void
error_die(char *fmt, ...)
{
    va_list alist;

    va_start(alist, fmt);
    vfprintf(stderr, fmt, alist);
    va_end(alist);
    fprintf(stderr, "\n");
    die();
}


/*
 * Print out a system error message.
 */
int
syserror(char *fmt, ...)
{
    va_list alist;

    va_start(alist, fmt);
    vfprintf(stderr, fmt, alist);
    va_end(alist);
    if (errno)
        fprintf(stderr, ": %s", strerror(errno));
    fprintf(stderr, "\n");
    return 0;
}

/*
 * Print out a system error message and exit.
 */
void
syserror_die(char *fmt, ...)
{
    va_list alist;

    va_start(alist, fmt);
    vfprintf(stderr, fmt, alist);
    va_end(alist);
    if (errno)
        fprintf(stderr, ": %s", strerror(errno));
    fprintf(stderr, "\n");
    die();
}


/*
 * Print out an internal error and exit.
 */
static void
bug_die(char *fmt, ...)
{
    va_list alist;

    fprintf(stderr, "internal error: ");
    va_start(alist, fmt);
    vfprintf(stderr, fmt, alist);
    va_end(alist);
    fprintf(stderr, "\n");
    die();
}


/*
 * Exit unsuccessfully.
 */
void
die(void)
{
    exit(1);
}
