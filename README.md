# mdelay (shortcut to measure the delay)

## 文件说明

- `rx_timestamping.c` 来自项目 [hw-timestamping](https://github.com/ArneVogel/hw-timestamping)
- `sender.c` 根据 [hw-timestamping](https://github.com/ArneVogel/hw-timestamping) 中的 rust-packets 用C语言重写
- `check_clock.c` 来自 https://tsn.readthedocs.io/timesync.html#checking-clocks-synchronization 提供的下载，用于检查PHC(网卡时钟)、CLOCK_REALTIME(UTC系统时钟)，CLOCK_TAI(原子系统时间)之间的offset情况