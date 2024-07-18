# How to
```bash
insmod proxy_target.ko
dmsetup create myproxy --table "0 1024 proxy /dev/loop0"
dmsetup remove myproxy
rmmod proxy_target
```
