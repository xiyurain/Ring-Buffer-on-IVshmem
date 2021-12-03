insmod /bin/ringbuf/src/ringbuf.ko
mknod /dev/ringbuf c 248 0
insmod /bin/ringbuf/test/send_msg.ko