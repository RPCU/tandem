// memsrv reads the memory of a running process through /proc/PID/mem and
// answers requests on stdin/stdout. It never writes to the process and never
// stops it (no ptrace): safe to use while the game is being played.
//
// Built static and copied to the game machine by tools/re/mem.py, which
// speaks this protocol over ssh. Requests (little endian):
//
//	'R' addr:u64 len:u32            -> ok:u8 data[len]        (ok=0: unreadable, no data)
//	'C' addr:u64 len:u32            -> ok:u8 data[len]        (two reads in a row that agree, up to
//	                                                            16 tries: a buffer the game is not rewriting)
//	'M'                             -> len:u32 text           (/proc/PID/maps)
//	'S' lo:u64 hi:u64 align:u32 max:u32 mode:u8 n:u16 pattern[n]
//	                                -> count:u32 addr:u64[count]
//	    scans readable private regions inside [lo,hi) for the byte pattern at
//	    addresses multiple of align; mode 1 = writable regions only.
//	'X' lo:u64 hi:u64 vlo:u64 vhi:u64 max:u32 mode:u8
//	                                -> count:u32 (addr:u64 value:u64)[count]
//	    finds 8-byte aligned values v with vlo <= v < vhi (pointers into a range).
package main

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
)

type region struct {
	lo, hi   uint64
	writable bool
}

var (
	pid string
	mem *os.File
)

func regions(writableOnly bool) []region {
	data, err := os.ReadFile("/proc/" + pid + "/maps")
	if err != nil {
		return nil
	}
	var out []region
	for _, line := range strings.Split(string(data), "\n") {
		f := strings.Fields(line)
		if len(f) < 5 || len(f[1]) < 4 || f[1][0] != 'r' {
			continue
		}
		// Devices and special mappings: reading them can fail or be slow.
		if len(f) >= 6 && (strings.HasPrefix(f[5], "/dev/") || strings.HasPrefix(f[5], "[v")) {
			continue
		}
		w := f[1][1] == 'w'
		if writableOnly && !w {
			continue
		}
		r := strings.SplitN(f[0], "-", 2)
		lo, _ := strconv.ParseUint(r[0], 16, 64)
		hi, _ := strconv.ParseUint(r[1], 16, 64)
		out = append(out, region{lo, hi, w})
	}
	return out
}

// walk calls fn with each readable chunk inside [lo,hi); chunks overlap by
// `overlap` bytes so patterns across chunk borders are found.
func walk(lo, hi uint64, writableOnly bool, overlap int, fn func(base uint64, b []byte) bool) {
	const chunk = 4 << 20
	buf := make([]byte, chunk+overlap)
	for _, r := range regions(writableOnly) {
		a, b := max(r.lo, lo), min(r.hi, hi)
		for a < b {
			n := min(uint64(len(buf)), b-a)
			got, err := mem.ReadAt(buf[:n], int64(a))
			if got <= 0 || (err != nil && got == 0) {
				a += 4096 // unreadable page: step over it
				a &^= 4095
				continue
			}
			if !fn(a, buf[:got]) {
				return
			}
			if uint64(got) < n || n < uint64(len(buf)) {
				a += uint64(got)
				if uint64(got) < n {
					a = (a + 4096) &^ 4095
				}
			} else {
				a += chunk
			}
		}
	}
}

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintln(os.Stderr, "usage: memsrv PID")
		os.Exit(2)
	}
	pid = os.Args[1]
	var err error
	mem, err = os.Open("/proc/" + pid + "/mem")
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	in := bufio.NewReaderSize(os.Stdin, 1<<16)
	out := bufio.NewWriterSize(os.Stdout, 1<<20)
	le := binary.LittleEndian
	for {
		op, err := in.ReadByte()
		if err != nil {
			return
		}
		switch op {
		case 'R':
			var h [12]byte
			if _, err := io.ReadFull(in, h[:]); err != nil {
				return
			}
			addr, n := le.Uint64(h[:8]), le.Uint32(h[8:])
			buf := make([]byte, n)
			if got, _ := mem.ReadAt(buf, int64(addr)); uint32(got) == n {
				out.WriteByte(1)
				out.Write(buf)
			} else {
				out.WriteByte(0)
			}
		case 'C':
			var h [12]byte
			if _, err := io.ReadFull(in, h[:]); err != nil {
				return
			}
			addr, n := le.Uint64(h[:8]), le.Uint32(h[8:])
			a, b := make([]byte, n), make([]byte, n)
			ok := false
			for try := 0; try < 16 && !ok; try++ {
				ga, _ := mem.ReadAt(a, int64(addr))
				gb, _ := mem.ReadAt(b, int64(addr))
				if uint32(ga) != n || uint32(gb) != n {
					break
				}
				ok = bytes.Equal(a, b)
			}
			if ok {
				out.WriteByte(1)
				out.Write(a)
			} else {
				out.WriteByte(0)
			}
		case 'M':
			data, _ := os.ReadFile("/proc/" + pid + "/maps")
			binary.Write(out, le, uint32(len(data)))
			out.Write(data)
		case 'S':
			var h [27]byte
			if _, err := io.ReadFull(in, h[:]); err != nil {
				return
			}
			lo, hi := le.Uint64(h[:8]), le.Uint64(h[8:16])
			align, limit, mode := uint64(le.Uint32(h[16:20])), le.Uint32(h[20:24]), h[24]
			pat := make([]byte, le.Uint16(h[25:27]))
			if _, err := io.ReadFull(in, pat); err != nil {
				return
			}
			var hits []uint64
			walk(lo, hi, mode == 1, len(pat)-1, func(base uint64, b []byte) bool {
				off := 0
				for {
					i := bytes.Index(b[off:], pat)
					if i < 0 {
						return true
					}
					a := base + uint64(off+i)
					if a%align == 0 && (len(hits) == 0 || hits[len(hits)-1] < a) {
						hits = append(hits, a)
						if uint32(len(hits)) >= limit {
							return false
						}
					}
					off += i + 1
				}
			})
			binary.Write(out, le, uint32(len(hits)))
			binary.Write(out, le, hits)
		case 'X':
			var h [37]byte
			if _, err := io.ReadFull(in, h[:]); err != nil {
				return
			}
			lo, hi := le.Uint64(h[:8]), le.Uint64(h[8:16])
			vlo, vhi := le.Uint64(h[16:24]), le.Uint64(h[24:32])
			limit, mode := le.Uint32(h[32:36]), h[36]
			var hits []uint64
			walk(lo, hi, mode == 1, 0, func(base uint64, b []byte) bool {
				start := int((8 - base%8) % 8)
				for i := start; i+8 <= len(b); i += 8 {
					if v := le.Uint64(b[i:]); v >= vlo && v < vhi {
						hits = append(hits, base+uint64(i), v)
						if uint32(len(hits)/2) >= limit {
							return false
						}
					}
				}
				return true
			})
			binary.Write(out, le, uint32(len(hits)/2))
			binary.Write(out, le, hits)
		default:
			return
		}
		out.Flush()
	}
}
