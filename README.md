#dependencies

Download and install the vm to run and build HelgrindPlus:

```sh
$ sudo apt update
$ sudo apt install -y \
  qemu-kvm \
  qemu-utils \
  libvirt-daemon-system \
  libvirt-clients \
  bridge-utils \
  virtinst

$ wget http://old-releases.ubuntu.com/releases/8.04.4/ubuntu-8.04.4-server-amd64.iso

$ qemu-img create -f qcow2 helgrind-vm.qcow2 10G

$ mkdir mnt extract
$ sudo mount -o loop ubuntu-8.04.4-server-amd64.iso mnt
$ cp mnt/install/vmlinuz extract/vmlinuz
$ cp mnt/install/initrd.gz extract/initrd.gz
$ sudo umount mnt

$ sudo qemu-system-x86_64 \
  -enable-kvm \
  -m 2048 -smp 4 \
  -drive file=./helgrind-vm.qcow2,if=virtio,format=qcow2 \
  -drive file=./ubuntu-8.04.4-server-amd64.iso,media=cdrom,readonly \
  -kernel ./extract/vmlinuz \
  -initrd ./extract/initrd.gz \
  -append "console=ttyS0,115200n8" \
  -nographic

$ sudo qemu-system-x86_64 \
  -enable-kvm \
  -m 2048 -smp 4 \
  -drive file=helgrind-vm.qcow2,if=virtio,format=qcow2 \
  -nographic \
  -serial mon:stdio \
  -net nic,model=virtio \
  -net user
  -virtfs local,path=./trunk,mount_tag=hostshare,security_model=passthrough,id=hostshare

$ sudo qemu-system-x86_64 \
  -enable-kvm \
  -m 2048 -smp 4 \
  -drive file=helgrind-vm.qcow2,if=virtio,format=qcow2 \
  -nographic \
  -serial mon:stdio \
  -net nic,model=virtio \
  -net user,hostfwd=tcp::2222-:22
```

Inside the VM, update
```sh

$ sudo sed -i \
  -e 's|http://\([a-z]*\.\)\?archive\.ubuntu\.com/ubuntu|http://old-releases.ubuntu.com/ubuntu|g' \
  -e 's|http://security\.ubuntu\.com/ubuntu|http://old-releases.ubuntu.com/ubuntu|g' \
  /etc/apt/sources.list

$ sudo apt-get install -y \
  build-essential \
  valgrind \
  automake autoconf libtool m4 pkg-config

$ scp -P 2222 /path/to/localfile  helgrindplus@localhost:/home/helgrindplus/
```

to quit: 
```sh
$ sudo shutdown -h now
```