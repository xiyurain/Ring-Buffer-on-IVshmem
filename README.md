# Ring-Buffer-on-IVshmem
a ring buffer device driver with PCIe based on QEMU Inter-VM shared memory

### how to run the demo

#### dependency
You need __qemu-system-x86_64__ and package __qemuutils__ to run the demo.

First, start an ivshmem-server on the Host Machine.

``` ivshmem-server -l 4M -M fg-doorbell -n 4 -F -v ```

Then, open a new terminal, cd into the repo and run the script to start the Qemu VMs.

``` cd Ring-Buffer-on-IVshmem && ./run_demo.sh ```

In the VM for reading the message,

``` sh bin/ringbuf/read.sh ```

In the VM for writing messages, (You can open multiple VM for writers as you want)

``` sh bin/ringbuf/write.sh ```
