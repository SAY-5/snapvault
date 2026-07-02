package cluster

import (
	"bytes"
	"testing"
)

func TestNewValidation(t *testing.T) {
	if _, err := New(0, 1); err == nil {
		t.Fatal("expected error for zero nodes")
	}
	if _, err := New(3, 0); err == nil {
		t.Fatal("expected error for replication 0")
	}
	if _, err := New(3, 4); err == nil {
		t.Fatal("expected error for replication > nodes")
	}
	if _, err := New(5, 3); err != nil {
		t.Fatalf("valid config rejected: %v", err)
	}
}

func TestPlacementDeterministicAndDistinct(t *testing.T) {
	c, err := New(5, 3)
	if err != nil {
		t.Fatal(err)
	}
	hash := "abc123"
	first := c.Placement(hash)
	if len(first) != 3 {
		t.Fatalf("expected 3 replicas, got %d", len(first))
	}
	// Deterministic: same hash yields the same placement every call.
	for i := 0; i < 10; i++ {
		got := c.Placement(hash)
		if len(got) != len(first) {
			t.Fatalf("placement size changed: %v vs %v", got, first)
		}
		for j := range got {
			if got[j] != first[j] {
				t.Fatalf("placement not deterministic: %v vs %v", got, first)
			}
		}
	}
	// Replicas are distinct node IDs within range.
	seen := map[int]bool{}
	for _, id := range first {
		if id < 0 || id >= c.NodeCount() {
			t.Fatalf("node id out of range: %d", id)
		}
		if seen[id] {
			t.Fatalf("duplicate replica node id: %d", id)
		}
		seen[id] = true
	}
}

func TestPutReplicatesToRNodes(t *testing.T) {
	c, err := New(6, 3)
	if err != nil {
		t.Fatal(err)
	}
	data := []byte("payload bytes")
	hash := "deadbeef"
	c.Put(hash, data)

	held := 0
	for _, st := range c.Status() {
		if st.Chunks > 0 {
			held++
		}
	}
	if held != 3 {
		t.Fatalf("expected chunk on exactly 3 nodes, got %d", held)
	}

	got, node, err := c.Get(hash)
	if err != nil {
		t.Fatalf("get failed: %v", err)
	}
	if !bytes.Equal(got, data) {
		t.Fatalf("data mismatch from node %d", node)
	}
}

func TestGetSurvivesReplicaFailures(t *testing.T) {
	c, err := New(6, 3)
	if err != nil {
		t.Fatal(err)
	}
	hash := "chunkX"
	c.Put(hash, []byte("survives"))
	placement := c.Placement(hash)

	// Take down R-1 of the R replicas: the chunk must still be readable.
	for i := 0; i < len(placement)-1; i++ {
		if err := c.SetUp(placement[i], false); err != nil {
			t.Fatal(err)
		}
	}
	if !c.ChunkAvailable(hash) {
		t.Fatal("chunk should still be available with one replica up")
	}
	if _, _, err := c.Get(hash); err != nil {
		t.Fatalf("get should succeed from surviving replica: %v", err)
	}

	// Take down the last replica: now it must fail cleanly.
	if err := c.SetUp(placement[len(placement)-1], false); err != nil {
		t.Fatal(err)
	}
	if c.ChunkAvailable(hash) {
		t.Fatal("chunk should be unavailable with all replicas down")
	}
	if _, _, err := c.Get(hash); err != ErrChunkUnavailable {
		t.Fatalf("expected ErrChunkUnavailable, got %v", err)
	}

	// Recovery restores availability.
	c.RecoverAll()
	if _, _, err := c.Get(hash); err != nil {
		t.Fatalf("get should succeed after recovery: %v", err)
	}
}
