//go:build !linux

package main

import "errors"

func storeLock(string) (func(), error) {
 return nil, errors.New("device install/rollback requires Linux; pack/keygen are portable")
}
