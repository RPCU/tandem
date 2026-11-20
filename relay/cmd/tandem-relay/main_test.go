package main

import "testing"

func TestShardAndPeers(t *testing.T) {
	for text, want := range map[string]int{"0": 0, "3": 3, "tandem-relay-1": 1, "prod-tandem-relay-12": 12} {
		if got, err := parseShard(text); err != nil || got != want {
			t.Errorf("%q: %d %v", text, got, err)
		}
	}
	for _, bad := range []string{"", "x", "relay-", "-1x"} {
		if _, err := parseShard(bad); err == nil {
			t.Errorf("%q accepted", bad)
		}
	}
	peers, err := parsePeers(" 192.0.2.1:27460, [2001:db8::1]:27460 ,")
	if err != nil || len(peers) != 2 || peers[1].Port() != 27460 {
		t.Fatalf("%v %v", peers, err)
	}
	if _, err := parsePeers("relay.example.org:27460"); err == nil {
		t.Fatal("a name was accepted (the relay makes no DNS query)")
	}
}
