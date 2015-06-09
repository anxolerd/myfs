# myfs

This is a filesystem in userspace for RAM I wrote as a part of my university assignment.

## Requirements:
- Unix-like OS
- FUSE installed

## Compile and run:
In this and following steps I assume you mount myfs to `test/mnt` directory. If you use another directory for testing replace test/mnt with your path
```[bash]
$ git clone git@github.com:OlexandrKovalchuk/myfs.git
$ cd myfs
$ make all
$ bin/myfs test/mnt
```

Now you can test this filesystem mounted in `test/mnt`

## Unmount
To unmount myfs filesystem use the following command: `fusermount -u test/mnt`. If you have troubles with myfs use `fusermount -uz test/mnt` to unmount.

## Known bugs
Do not use VIM for editing files in myfs. NO WAY! I do not know why, but fs freezes when you try to save file edited in VIM.

