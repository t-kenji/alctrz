Alcatraz (Chroot jail provider)
===============================

Introduction
------------

Alcatraz is Chroot jail provider.

Dependencies
------------

- [jansson](http://www.digip.org/jansson)

Usage
-----

```
$ cat env.json
{
    "directory": [
        "/bin",
        "/usr/bin",
        "/var"
    ],
    "device": [
        "/dev/null,char,1,3,0666",
        "/dev/zero,char,1,5,0666",
        "/dev/urandom,char,1,9,0444"
    ],
    "bind": [
        "/lib",
        "/usr/lib"
    ],
    "keep_capability": [
        "CAP_SYS_ADMIN"
    ]
}
$ sudo ./alctrz -c env.json -u prisoner -g prisoner-group -- /bin/ls -laR
.:
total 4
drwx------    8 1000     1000           160 Jan  4 22:51 .
drwx------    8 1000     1000           160 Jan  4 22:51 ..
drwx------    2 1000     1000            60 Jan  4 22:51 bin
drwxr-xr-x    2 1000     1000           100 Jan  4 22:51 dev
drwxrwxr-x    5 0        0             4096 May 10  2018 lib
drwxr-xr-x    2 1000     1000            60 Jan  4 22:51 root
drwxr-xr-x    4 1000     1000            80 Jan  4 22:51 usr
drwx------    2 1000     1000            40 Jan  4 22:51 var

./bin:
total 636
drwx------    2 1000     1000            60 Jan  4 22:51 .
drwx------    8 1000     1000           160 Jan  4 22:51 ..
-rwxrwxr-x    1 0        0           650620 Apr 23  2018 ls

./dev:
total 0
drwxr-xr-x    2 1000     1000           100 Jan  4 22:51 .
drwx------    8 1000     1000           160 Jan  4 22:51 ..
crw-r--r--    1 1000     1000        1,   3 Jan  4 22:51 null
cr--r--r--    1 1000     1000        1,   9 Jan  4 22:51 urandom
crw-r--r--    1 1000     1000        1,   5 Jan  4 22:51 zero

./lib:
total 136
drwxrwxr-x    5 0        0             4096 May 10  2018 .
drwx------    8 1000     1000           160 Jan  4 22:51 ..
drwxr-xr-x    2 0        0             4096 Apr 23  2018 arm-linux-gnueabihf
lrwxrwxrwx    1 0        0               38 Sep 11  2014 ld-linux-armhf.so.3 -> arm-linux-gnueabihf/ld-2.19-2014.08.so
lrwxrwxrwx    1 0        0               17 Apr 23  2018 libip4tc.so -> libip4tc.so.0.1.0
lrwxrwxrwx    1 0        0               17 Apr 23  2018 libip4tc.so.0 -> libip4tc.so.0.1.0
-rwxrwxr-x    1 0        0            23347 Apr 23  2018 libip4tc.so.0.1.0
...
```

How to test
-----------

```
$ make test
```

Generate doxygen documents
--------------------------

```
$ make doc
```
