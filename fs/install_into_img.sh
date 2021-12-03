mount -o loop rfs.img rootfs
rm -r rootfs/bin/ringbuf/
cp -r ../ringbuf/ rootfs/bin
umount rootfs
cp rfs.img rfs2.img

