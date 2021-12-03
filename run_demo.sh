#!/bin/bash

cd ./ringbuf/src

cd ../../fs/

rm -r ./rootfs/
mkdir ./rootfs
mount -o loop rfs.img ./rootfs
rm -r rootfs/bin/ringbuf/
cp -rp ../ringbuf/ rootfs/bin
umount rootfs
rm -r ./rootfs/
cp rfs.img rfs2.img
cp rfs.img rfs3.img
cp rfs.img rfs4.img


# gdb --args /home/qemu/build/qemu-system-x86_64 -m 1024M \
# /home/qemu/build/qemu-system-x86_64 -m 1024M \
nohup qemu-system-x86_64 -m 1024M \
    -drive format=raw,file=rfs.img \
    -kernel ../images/bzImage \
    -append "root=/dev/sda init=/bin/ash" \
    -chardev socket,path=/tmp/ivshmem_socket,id=fg-doorbell \
    -device ivshmem-doorbell,chardev=fg-doorbell,vectors=4 \
    > qemu.log 2>&1 &

nohup qemu-system-x86_64 -m 1024M \
    -drive format=raw,file=rfs2.img \
    -kernel ../images/bzImage \
    -append "root=/dev/sda init=/bin/ash" \
    -chardev socket,path=/tmp/ivshmem_socket,id=fg-doorbell \
    -device ivshmem-doorbell,chardev=fg-doorbell,vectors=4 \
    > qemu.log2 2>&1 &

nohup qemu-system-x86_64 -m 1024M \
    -drive format=raw,file=rfs3.img \
    -kernel ../images/bzImage \
    -append "root=/dev/sda init=/bin/ash" \
    -chardev socket,path=/tmp/ivshmem_socket,id=fg-doorbell \
    -device ivshmem-doorbell,chardev=fg-doorbell,vectors=4 \
    > qemu.log3 2>&1 &

nohup qemu-system-x86_64 -m 1024M \
    -drive format=raw,file=rfs4.img \
    -kernel ../images/bzImage \
    -append "root=/dev/sda init=/bin/ash" \
    -chardev socket,path=/tmp/ivshmem_socket,id=fg-doorbell \
    -device ivshmem-doorbell,chardev=fg-doorbell,vectors=4 \
    > qemu.log4 2>&1 &

ivshmem-client
