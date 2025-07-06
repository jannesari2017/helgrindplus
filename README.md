**Published Papers**
```
@INPROCEEDINGS{5160998,
  author={Jannesari, Ali and Kaibin Bao and Pankratius, Victor and Tichy, Walter F.},
  booktitle={2009 IEEE International Symposium on Parallel & Distributed Processing}, 
  title={Helgrind+: An efficient dynamic race detector}, 
  year={2009},
  volume={},
  number={},
  pages={1-13},
  keywords={Yarn;Detectors;Dynamic programming;Switches;Testing;Debugging;Parallel programming;Parallel processing;System recovery;Event detection},
  doi={10.1109/IPDPS.2009.5160998}}

@INPROCEEDINGS{5470343,
  author={Jannesari, Ali and Tichy, Walter F.},
  booktitle={2010 IEEE International Symposium on Parallel & Distributed Processing (IPDPS)}, 
  title={Identifying ad-hoc synchronization for enhanced race detection}, 
  year={2010},
  volume={},
  number={},
  pages={1-10},
  keywords={Programming profession;Libraries;Switches;Debugging;Runtime;data race detection;race conditions;debugging;parallel programs;ad-hoc synchronization;synchronization primitives;dynamic analysis},
  doi={10.1109/IPDPS.2010.5470343}}

@ARTICLE{6583165,
  author={Jannesari, Ali and Tichy, Walter F.},
  journal={IEEE Transactions on Parallel and Distributed Systems}, 
  title={Library-Independent Data Race Detection}, 
  year={2014},
  volume={25},
  number={10},
  pages={2606-2616},
  keywords={Synchronization;Spinning;Detectors;Libraries;Pipelines;Concurrent computing;Instruction sets;Parallel programming;parallelization libraries;ad hoc synchronization;synchronization primitives;dynamic analysis;data race detection;debugging;multicore},
  doi={10.1109/TPDS.2013.209}}
```

**Installation Instruction**

Helgrind+ requires a linux machine with kernel 2.4 or 2.6. Hence, to run Helgrind+ on a modern system, users should download a vm to run an os with the supported kernel.

**This installation guide will build Helgrind+ on ubuntu 8.04 running on a VM.**

First, clone the repository.
```sh
git clone https://github.com/jannesari2017/helgrindplus.git
```
Then, install the VM to run and build HelgrindPlus:

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
```

Launch the VM with the OS
```sh
$ sudo qemu-system-x86_64 \
  -enable-kvm \
  -m 2048 -smp 4 \
  -drive file=./helgrind-vm.qcow2,if=virtio,format=qcow2 \
  -drive file=./ubuntu-8.04.4-server-amd64.iso,media=cdrom,readonly \
  -kernel ./extract/vmlinuz \
  -initrd ./extract/initrd.gz \
  -append "console=ttyS0,115200n8" \
  -nographic
```  

Once, the VM is set up, exit using  
```sh
$ sudo shutdown -h now
```


Then, launch the VM again
```sh
To start the VM again
$ sudo qemu-system-x86_64 \
  -enable-kvm \
  -m 2048 -smp 4 \
  -drive file=helgrind-vm.qcow2,if=virtio,format=qcow2 \
  -nographic \
  -serial mon:stdio \
  -net nic,model=virtio \
  -net user,hostfwd=tcp::2222-:22
```

Inside the VM, issue the following commands
```sh
$ sudo sed -i \
  -e 's|http://\([a-z]*\.\)\?archive\.ubuntu\.com/ubuntu|http://old-releases.ubuntu.com/ubuntu|g' \
  -e 's|http://security\.ubuntu\.com/ubuntu|http://old-releases.ubuntu.com/ubuntu|g' \
  /etc/apt/sources.list

$ sudo apt-get install -y \
  build-essential \
  automake autoconf libtool m4 pkg-config libgomp
```

Now, back in the host machine (outside the VM)

Download valgrind 3.4.1 from https://sourceware.org/pub/valgrind/

Unpack, then send it to the vm.

```sh
$ tar -xjf valgrind-3.4.1.tar.bz2

$ scp -P 2222 -r ./valgrind-3.4.1  helgrindplus@localhost:<path to your home in VM>/
```

Send the trunk folder as well

```sh
  $ scp -P 2222 -r ./valgrind-3.4.1  helgrindplus@localhost:<path to your home in VM>/
```

Then, copy the contents of the trunk to the valgrind source folder.

Then, follow the build procedure for valgrind.


Usually, it's:

```sh
#Assuming you are inside the valgrind source directory

$sudo bash ./autogen.sh

$./configure --prefix=<your desired build directory>

$ make

$ sudo make install
```


Please refer to USAGE.md on how to use Helgrind+ as Valgrind's plugin


