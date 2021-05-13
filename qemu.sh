#!/bin/bash
echo "Welcome to HardCore."
echo "HardCore is a kernel defined by users, you can assign the process scheduling algotirhm, the memory allocation algorithm and the page replacement algorithm yourself and we will brew the kernel"
echo "Choose the process scheduling algorithm. Type 1 to choose Complete Fair Scheduling. Type 2 to choose Stride Scheduling."
read scheduling_choice
if [ "$scheduling_choice" == "1" ]; then 
    echo "Complete Fair Scheduling choosed."
    sed -i '48s/default_sched_class/cfs_sched_class/' kern/schedule/sched.c
elif [ "$scheduling_choice" == "2" ]; then 
    echo "Stride Scheduling choosed"
    sed -i '48s/cfs_sched_class/default_sched_class/' kern/schedule/sched.c
else
    echo "Input Error, choose Complete Fair Scheduling by default."
    sed -i '48s/default_sched_class/cfs_sched_class/' kern/schedule/sched.c
fi

echo "Choose the process scheduling algorithm. Type 1 to choose first-fit. Type 2 to choose best-fit. Type 3 to choose worst-fit."
read pmm_choice
if [ "$pmm_choice" == "1" ]; then 
    echo "first-fit choosed."
    sed -i '380c \\t.alloc_pages = default_alloc_pages,' kern/mm/default_pmm.c
    sed -i '383c \\t.check = default_check,' kern/mm/default_pmm.c
elif [ "$pmm_choice" == "2" ]; then 
    echo "best-fit choosed"
    sed -i '380c \\t.alloc_pages = default_alloc_pages_best_fit,' kern/mm/default_pmm.c
    sed -i '383c \\t.check = default_check,' kern/mm/default_pmm.c
elif [ "$pmm_choice" == "3" ]; then 
    echo "best-fit choosed"
    sed -i '380c \\t.alloc_pages = default_alloc_pages_worst_fit,' kern/mm/default_pmm.c
    sed -i '383c \\t.check = default_worst_fit_checker,' kern/mm/default_pmm.c
else
    echo "Input Error, choose first-fit by default."
    sed -i '380c \\t.alloc_pages = default_alloc_pages,' kern/mm/default_pmm.c
    sed -i '383c \\t.check = default_check,' kern/mm/default_pmm.c
fi

echo "Choose the swap algorithm. Type 1 to choose fifo. Type 2 to choose clock. Type 3 to choose clock with dirty bit."
read swap_choice
if [ "$swap_choice" == "1" ]; then 
    echo "fifo choosed."
    sed -i '40c \\tsm = &swap_manager_fifo;' kern/mm/swap.c
elif [ "$swap_choice" == "2" ]; then 
    echo "clock choosed"
    sed -i '40c \\tsm = &swap_manager_clock;' kern/mm/swap.c
elif [ "$swap_choice" == "3" ]; then 
    echo "clock with dirty bit choosed"
    sed -i '40c \\tsm = &swap_manager_extended_clock;' kern/mm/swap.c
else
    echo "Input Error, choose fifo by default."
    sed -i '40c \\tsm = &swap_manager_fifo;' kern/mm/swap.c
fi

make clean

make

pid=$(lsof -i:1234 | grep :1234 | awk 'NR==1{print}'| awk '{print $2}')
#杀掉对lsof -i:1234 | grep :1234 | awk 'NR==1{print}'| awk '{print $2}'应的进程，如果pid不存在，则不执行
if [ -n "$pid" ]; then
    echo "pid get is "$pid
    kill -9 $pid
    echo "qemu be terminate successfully！"
else
    echo "qemu not start at first"
fi

# qemu-system-i386 -S -s -parallel stdio -hda bin/ucore.img,format=raw \
#                                        -drive file=bin/swap.img,format=raw,media=disk,cache=writeback \
#                                        -drive file=bin/sfs.img,format=raw,media=disk,cache=writeback  \
#                                        -serial null &

# -S              freeze CPU at startup (use 'c' to start execution)
# -s              shorthand for -gdb tcp::1234
# -parallel dev   redirect the parallel port to char device 'dev'
# -hda/-hdb file  use 'file' as IDE hard disk 0/1 image           qemu默认从硬盘启动所以定位到ucore.img

if [ "$1" == "d" ] ; then
	qemu-system-i386 -S -s -drive file=bin/ucore.img,format=raw,index=0,media=disk -drive file=bin/swap.img,format=raw,media=disk,cache=writeback -drive file=bin/sfs.img,format=raw,media=disk,cache=writeback -nographic
else
	qemu-system-i386 -drive file=bin/ucore.img,format=raw,index=0,media=disk -drive file=bin/swap.img,format=raw,media=disk,cache=writeback -drive file=bin/sfs.img,format=raw,media=disk,cache=writeback -nographic
fi


# qemu-system-i386 -S -s -drive file=bin/ucore.img,format=raw,index=0,media=disk -drive file=bin/swap.img,format=raw,media=disk,cache=writeback -drive file=bin/sfs.img,format=raw,media=disk,cache=writeback -nographic

# qemu -drive file=bin/ucore.img,format=raw,index=0,media=disk -drive file=bin/swap.img,format=raw,media=disk,cache=writeback -drive file=bin/sfs.img,format=raw,media=disk,cache=writeback -nographic

# gdb -q -x tools/gdbinit

# sleep 2