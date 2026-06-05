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
#include <stdarg.h>
#include <sys/ioctl.h>
#include <net/if.h>

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
    (void)arg;  // disregard args

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

// rootfs set up (assumes busy box basics exist)
// chroot strategy fallback 
static void setup_rootfs(void) {
    // Mount /proc inside the rootfs first
    char proc_path[256];
    snprintf(proc_path, sizeof(proc_path), "%s/proc", ROOTFS);
    if (mount("proc", proc_path, "proc", 0, NULL) < 0) {
        perror("mount proc"); exit(1);
    }

    if (chroot(ROOTFS) < 0) {
        perror("chroot"); exit(1);
    }

    chdir("/");
}

// pivot root strategy safer and more isolated
// static void setup_rootfs(void) {
//     // Make root private to stop propagation
//     if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
//         perror("mount private"); exit(1);
//     }
//
//     // Bind mount rootfs onto a known mount point under /tmp
//     // This gives it a distinct mount identity from /
//     const char *target = "/tmp/container_root";
//     mkdir(target, 0700);
//
//     if (mount(ROOTFS, target, NULL, MS_BIND | MS_REC, NULL) < 0) {
//         perror("mount bind"); exit(1);
//     }
//
//     // Mount /proc inside it
//     char proc_path[256];
//     snprintf(proc_path, sizeof(proc_path), "%s/proc", target);
//     if (mount("proc", proc_path, "proc", 0, NULL) < 0) {
//         perror("mount proc"); exit(1);
//     }
//
//     // pivot_root into the new mount
//     char old_root[256];
//     snprintf(old_root, sizeof(old_root), "%s/old_root", target);
//     mkdir(old_root, 0700);
//
//     if (chdir(target) < 0) {
//         perror("chdir target"); exit(1);
//     }
//
//     // pivot_root(".", "old_root") — dot form avoids path resolution issues
//     if (syscall(SYS_pivot_root, ".", "./old_root") < 0) {
//         perror("pivot_root"); exit(1);
//     }
//
//     chdir("/");
//
//     if (umount2("/old_root", MNT_DETACH) < 0) {
//         perror("umount old_root"); exit(1);
//     }
//     rmdir("/old_root");
// }

// network setup and helpers
static void if_up(const char *ifname) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    // get current flags
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) { perror("SIOCGIFFLAGS"); exit(1); }

    // set IFF_UP
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) { perror("SIOCSIFFLAGS"); exit(1); }

    close(sock);
}

static void if_set_addr(const char *ifname, const char *ip_str) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, ip_str, &addr->sin_addr);

    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) { perror("SIOCSIFADDR"); exit(1); }

    // set netmask
    inet_pton(AF_INET, NETMASK, &addr->sin_addr);
    if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0) { perror("SIOCSIFNETMASK"); exit(1); }

    close(sock);
}

static void setup_network_inside(void) {
    if_up("lo");  // loopback
    if_up(VETH_PEER); // bridge
    if_set_addr(VETH_PEER, CONT_IP); // se addr to enable coms
}

// shel out to system
static void run_netcmd(const char *fmt, ...) {
    char cmd[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "command failed (exit %d): %s\n", WEXITSTATUS(ret), cmd);
        exit(1);
    }
}
