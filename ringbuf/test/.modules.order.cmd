cmd_/home/iv_shmem-on-pcie/ringbuf/test/modules.order := {   echo /home/iv_shmem-on-pcie/ringbuf/test/send_msg.ko; :; } | awk '!x[$$0]++' - > /home/iv_shmem-on-pcie/ringbuf/test/modules.order
