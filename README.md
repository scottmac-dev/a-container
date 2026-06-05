# a-container
Exploring the linux syscalls that make containers possible

## Rootfs
for simplicity this example is using busybox to set up the rootfs that the
container will use, I wont commit this, instead just provide the commands it
takes to set up.

```
# Install busybox static
sudo apt install busybox-static

# Create the rootfs layout
mkdir -p rootfs/{bin,proc,sys,dev,tmp,etc,net}

# Copy the static busybox binaries, dont symlink to host
# as they will not be accessible in container env
cp /usr/bin/busybox ./rootfs/bin/busybox
chmod +x ./rootfs/bin/busybox

# Create relative symlinks manually as busybox --install seems
# to ignore target path and create symlinks back to host binary
cd ./rootfs/bin
find . -type l -delete
for cmd in sh ash ls cat echo ps mount umount mkdir rm cp mv pwd env id hostname ping; do
    ln -s busybox $cmd
done
cd -

# Verify symlinks are relative to ./rootfs 
ls -la ./rootfs/bin/sh   # must show: sh -> busybox not /usr/bin/busybox

# Basic etc setup
echo "root:x:0:0:root:/root:/bin/sh" > ./rootfs/etc/passwd
echo "nameserver 8.8.8.8" > ./rootfs/etc/resolv.conf
```

your rootfs path is now ./rootfs/ in this repo which the container.c will use

## Running 
1. Do above busybox set up to have your ./rootfs/ set up with primitive utils 
2. gcc -o container container.c
3. sudo ./container

This should pop you into th container where you see the shell prompt 
```
[container]#
```

From here you can call the busybox primitives and do basic shell stuff

Use exit to exit container back to host

## Core concepts
### Name spaces
- used as isolation primitives 
- each CLONE_NEW* flag passed to clone() syscall creates a fresh kernel
namespace, the child process sees a new instance of that resource 
- in this basic example we covered
    * NEWPID = container gets its own pid tree, cannot see/signal host 
    * NEWNS = container gets its own mount table, mounts dont affect host 
    * NEWUTS = isolated hostname and domain name
    * NEWIPC = isolated IPC/POSIX msg queue, shared memory not leaked to host 
    * NEWNET = seperate network stack for interfaces, routing, sockets etc.

### Rootfs setup
- chroot() changes what the process sees as /, any path resolution will now
originate from ./rootfs as / within the container, the process cannot traverse
above it.
- the chroot method is theoretically weaker safety guarantees than the pivot
root strategy and come escape in some circumstances, but it was much simpler to
implement for this demo use case

### clone() vs fork()
- fork() copies a process 
- clone() is lower level and lets you control what resources the child will
share or get a copy of from the host.
- fork uses clone at a lower level 

### Veth pairs 
- a veth is a virtual ethernet cable abstraction provided by the kernel 
- a veth pair is therefore two kernel connected interfaces that pipe
communications to eachother
- in this example veth0 is kept on the host and veth1 in the container allowing
  the container to ping and communicate with the host, this is done via `ip link
set veth1 netns <pid>` and gives the container a real interface 
- ioctl on the DGRAM socket is the trad unix way to configure interfaces without
  shelling out with system(), its used here because I was getting bugs trying to
  shell out.

### usleep hack 
- usleep is used as a deliberate hacky way to allow the container time to
synchronise, after clone() returns the host and child process run concurrently
and the parent needs time to set up the veth pairs before the child container
uses them
- apparantly this should use pipe() to be more correct 

### not covered 
- cgroups for resource limits 
- seccomp for syscall filtering 
- user name spaces 
- pivot root (attempted but opted for chroot)


