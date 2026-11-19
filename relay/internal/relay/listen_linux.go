//go:build linux && !mips && !mipsle && !mips64 && !mips64le

package relay

import "syscall"

const reusePortSupported = true

// SO_REUSEPORT, which the frozen syscall package does not name: 15 on every
// Linux architecture Go runs on but MIPS (left out above).
const soReusePort = 0xf

func setReusePort(fd uintptr) error {
	return syscall.SetsockoptInt(int(fd), syscall.SOL_SOCKET, soReusePort, 1)
}
