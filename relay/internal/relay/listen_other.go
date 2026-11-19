//go:build !linux || mips || mipsle || mips64 || mips64le

package relay

const reusePortSupported = false

func setReusePort(uintptr) error { return nil }
