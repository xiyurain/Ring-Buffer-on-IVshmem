cmd_/home/iv_shmem-on-pcie/ringbuf/src/modules.order := {   echo /home/iv_shmem-on-pcie/ringbuf/src/ringbuf.ko; :; } | awk '!x[$$0]++' - > /home/iv_shmem-on-pcie/ringbuf/src/modules.order
