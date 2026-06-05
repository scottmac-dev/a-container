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

# Install all busybox applets
busybox --install -s ./rootfs/bin

# Basic etc setup
echo "root:x:0:0:root:/root:/bin/sh" > ./rootfs/etc/passwd
echo "nameserver 8.8.8.8" > ./rootfs/etc/resolv.conf
```

your rootfs path is now ./rootfs/ in this repo which the container.c will use


