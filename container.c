#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>

// config 

#define ROOTFS "./rootfs"
#define STACK_SIZE    (1024 * 1024) // ~1MB
#define VETH_HOST     "veth0"
#define VETH_PEER     "veth1"
#define HOST_IP       "10.0.0.1"
#define CONT_IP       "10.0.0.2"
#define NETMASK       "255.255.255.0"

// forward declare

static int container_main(void *arg);
static void setup_rootfs(void);
static void setup_network_inside(void);
static void run_netcmd(const char *fmt, ...);
