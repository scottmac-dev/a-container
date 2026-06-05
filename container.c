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


// host entry point 
int main(void) {
    char *stack = malloc(STACK_SIZE); // create stack
    if (!stack) { perror("malloc"); exit(1); }

    // create veth pair on the host
    // veth0 stays on host 
    // veth1 gets moved into the new netns
    run_netcmd("ip link add %s type veth peer name %s", VETH_HOST, VETH_PEER);
    run_netcmd("ip addr add %s/24 dev %s", HOST_IP, VETH_HOST);
    run_netcmd("ip link set %s up", VETH_HOST);

    int flags = CLONE_NEWPID   // isolated PID tree, container gets PID 1
              | CLONE_NEWNS    // isolated mount namespace
              | CLONE_NEWUTS   // isolated hostname
              | CLONE_NEWIPC   // isolated IPC 
              | CLONE_NEWNET   // isolated network stack
              | SIGCHLD;

    // create child process
    pid_t pid = clone(container_main, stack + STACK_SIZE, flags, NULL);
    if (pid < 0) { perror("clone"); exit(1); }

    // move veth1 into the container's network namespace
    // /proc/<pid>/ns/net is the handle to its netns
    run_netcmd("ip link set %s netns %d", VETH_PEER, pid);

    // clean up when container exits
    waitpid(pid, NULL, 0);
    run_netcmd("ip link del %s", VETH_HOST);

    free(stack);  // free da stack
    return 0;
}

// container entry point
static int container_main(void *arg) {
    (void)arg;

    // delay to allow host a moment to move veth1 into container netns  
    usleep(200000);

    // name ourself
    sethostname("container", 9);

    setup_rootfs(); // root config and setup
    setup_network_inside(); // network config and setup
    
    // enter container shell
    char *argv[] = { "/bin/sh", NULL };
    char *envp[] = {
        "PATH=/bin:/sbin",
        "TERM=xterm",
        "PS1=[container]\\$ ",
        NULL
    };

    execve("/bin/sh", argv, envp);
    perror("execve failed miserably"); // something went wrong
    return 1;
}
