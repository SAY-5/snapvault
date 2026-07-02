package cluster

import (
	"bytes"
	"testing"
)

func TestVerifyDeepCleanCluster(t *testing.T) {
	c, err := New(5, 3)
	if err != nil {
		t.Fatal(err)
	}
	seedChunks(t, c, 20)

	total := 0
	for _, nv := range c.VerifyDeep() {
		if len(nv.Bad) != 0 {
			t.Fatalf("node %d reports corruption on a clean cluster: %v", nv.ID, nv.Bad)
		}
		total += nv.Checked
	}
	if total != 20*3 {
		t.Fatalf("checked %d replicas, want %d", total, 20*3)
	}
}

func TestVerifyDeepFindsInjectedCorruptionAndRepairFixesIt(t *testing.T) {
	c, err := New(5, 3)
	if err != nil {
		t.Fatal(err)
	}
	hashes := seedChunks(t, c, 10)

	// Flip bytes of one replica of one chunk, on a single node.
	victim := hashes[4]
	victimNode := c.Placement(victim)[1]
	c.mu.Lock()
	c.nodes[victimNode].data[victim] = []byte("flipped bits on disk")
	c.mu.Unlock()

	// Deep verify pins the corruption to exactly that node and hash.
	found := false
	for _, nv := range c.VerifyDeep() {
		if nv.ID == victimNode {
			if len(nv.Bad) != 1 || nv.Bad[0] != victim {
				t.Fatalf("node %d bad list %v, want [%s]", nv.ID, nv.Bad, victim)
			}
			found = true
		} else if len(nv.Bad) != 0 {
			t.Fatalf("node %d unexpectedly reports corruption: %v", nv.ID, nv.Bad)
		}
	}
	if !found {
		t.Fatalf("node %d missing from deep verify report", victimNode)
	}

	// Repair overwrites the corrupt replica from a good copy.
	st := c.Repair()
	if st.CorruptFixed != 1 {
		t.Fatalf("repair fixed %d corrupt replicas, want 1", st.CorruptFixed)
	}
	if len(st.Unrepairable) != 0 {
		t.Fatalf("unexpected unrepairable chunks: %v", st.Unrepairable)
	}
	for _, nv := range c.VerifyDeep() {
		if len(nv.Bad) != 0 {
			t.Fatalf("node %d still corrupt after repair: %v", nv.ID, nv.Bad)
		}
	}
	c.mu.RLock()
	fixed := c.nodes[victimNode].data[victim]
	c.mu.RUnlock()
	if !bytes.Equal(fixed, testPayload(4)) {
		t.Fatal("repaired replica does not match the original payload")
	}
}

func TestVerifyDeepSkipsDownNodes(t *testing.T) {
	c, err := New(4, 2)
	if err != nil {
		t.Fatal(err)
	}
	seedChunks(t, c, 8)
	if err := c.SetUp(1, false); err != nil {
		t.Fatal(err)
	}
	for _, nv := range c.VerifyDeep() {
		if nv.ID == 1 {
			if nv.Up || nv.Checked != 0 {
				t.Fatalf("down node should not be checked, got %+v", nv)
			}
		}
	}
}

func TestRepairRefusesWhenAllReplicasCorrupt(t *testing.T) {
	c, err := New(5, 3)
	if err != nil {
		t.Fatal(err)
	}
	hashes := seedChunks(t, c, 3)
	victim := hashes[0]
	c.mu.Lock()
	for _, id := range c.Placement(victim) {
		c.nodes[id].data[victim] = []byte("every copy is bad")
	}
	c.mu.Unlock()

	st := c.Repair()
	if len(st.Unrepairable) != 1 || st.Unrepairable[0] != victim {
		t.Fatalf("expected %q unrepairable, got %v", victim, st.Unrepairable)
	}
}
