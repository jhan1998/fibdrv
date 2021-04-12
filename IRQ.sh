#!/bin/bash

CPUID=7
ORIG_ASLR=`cat /proc/sys/kernel/randomize_va_space`
ORIG_GOV=`cat /sys/devices/system/cpu/cpu$CPUID/cpufreq/scaling_governor`
ORIG_TURBO=`cat /sys/devices/system/cpu/intel_pstate/no_turbo`

sudo bash -c "echo 0 > /proc/sys/kernel/randomize_va_space"
sudo bash -c "echo performance > /sys/devices/system/cpu/cpu$CPUID/cpufreq/scaling_governor"
sudo bash -c "echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo"

make unload
make load
rm -f plot.txt
sudo taskset -c 7 ./client_plot > plot.txt
gnuplot before.gp

itr=0
for file in `find /proc/irq -name "smp_affinity"`
do
    var=0x`cat ${file}`
    arr[${itr}]=${var}
    var="$(( $var & 0x7f ))"
    var=`printf '%.2x' ${var}`
    sudo bash -c "echo ${var} > ${file}"
    (( itr++ ))
done
sudo bash -c "echo 7f > /proc/irq/default_smp_affinity"

rm -f plot.txt
sudo taskset -c 7 ./client_plot > plot.txt
gnuplot after.gp
make unload

# restore the original system settings
sudo bash -c "echo $ORIG_ASLR >  /proc/sys/kernel/randomize_va_space"
sudo bash -c "echo $ORIG_GOV > /sys/devices/system/cpu/cpu$CPUID/cpufreq/scaling_governor"
sudo bash -c "echo $ORIG_TURBO > /sys/devices/system/cpu/intel_pstate/no_turbo"

itr=0
for file in `find /proc/irq -name "smp_affinity"`
do
    var=${arr[${itr}]}
    var=`printf '%.2x' ${var}`
    sudo bash -c "echo ${var} > ${file}"
    (( itr++ ))
done
sudo bash -c "echo ff > /proc/irq/default_smp_affinity"
