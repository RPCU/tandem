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

func TestInternalPrefixes(t *testing.T) {
	got, err := parsePrefixes(" 10.42.0.1/16, fd00::/8 ,")
	if err != nil || len(got) != 2 || got[0].String() != "10.42.0.0/16" {
		t.Fatalf("%v %v", got, err)
	}
	if _, err := parsePrefixes("10.42.0.0"); err == nil {
		t.Fatal("an address without a length was accepted")
	}
}
