package main

import (
 "fmt"
 "os"
 "path/filepath"
 "syscall"
)
func storeLock(dir string) (func(), error) {
 fd,e:=syscall.Open(filepath.Join(dir,".lock"),syscall.O_CREAT|syscall.O_RDWR|syscall.O_CLOEXEC|syscall.O_NOFOLLOW,0600)
 if e!=nil{return nil,e}
 f:=os.NewFile(uintptr(fd),".lock");st,e:=f.Stat()
 if e!=nil||!st.Mode().IsRegular(){f.Close();return nil,fmt.Errorf("unsafe store lock")}
 if e=syscall.Flock(fd,syscall.LOCK_EX|syscall.LOCK_NB);e!=nil{f.Close();return nil,e}
 return func(){syscall.Flock(fd,syscall.LOCK_UN);f.Close()},nil
}
