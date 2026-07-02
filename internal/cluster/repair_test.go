package cluster

import (
	"bytes"
	"fmt"
	"sync"
	"testing"
)

// seedChunks puts count distinct chunks and returns their hashes.
func seedChunks(t *testing.T, c *Cluster, count int) []string {
	t.Helper()
	hashes := make([]string, 0, count)
	for i := 0; i < count; i++ {
		h := fmt.Sprintf("chunk-%03d", i)
		c.Put(h, []byte("payload for "+h))
		hashes = append(hashes, h)
	}
	return hashes
}

func TestRepairRestoresFullReplication(t *testing.T) {
	c, err := New(6, 3)
	if err != nil {
		t.Fatal(err)
	}
	hashes := seedChunks(t, c, 40)

	if err := c.SetUp(2, false); err != nil {
		t.Fatal(err)
	}
	if n := len(c.UnderReplicatedChunks()); n == 0 {
		t.Fatal("expected under-replicated chunks after node failure")
	}

	st := c.Repair()
	if st.Scanned != 40 {
		t.Fatalf("scanned %d chunks, want 40", st.Scanned)
	}
	if st.UnderReplicated == 0 || st.Copied != st.UnderReplicated {
		t.Fatalf("expected one copy per under-replicated chunk, got %d copies for %d chunks",
			st.Copied, st.UnderReplicated)
	}
	if len(st.Unrepairable) != 0 {
		t.Fatalf("unexpected unrepairable chunks: %v", st.Unrepairable)
	}

	// Every chunk is back to R live replicas and still readable.
	for _, h := range hashes {
		if live := c.LiveReplicas(h); live != 3 {
			t.Fatalf("chunk %s has %d live replicas after repair, want 3", h, live)
		}
		got, _, err := c.Get(h)
		if err != nil {
			t.Fatalf("get %s after repair: %v", h, err)
		}
		if !bytes.Equal(got, []byte("payload for "+h)) {
			t.Fatalf("chunk %s corrupted by repair", h)
		}
	}
	if n := len(c.UnderReplicatedChunks()); n != 0 {
		t.Fatalf("%d chunks still under-replicated after repair", n)
	}
}

func TestRepairReportsUnrepairableChunks(t *testing.T) {
	c, err := New(5, 3)
	if err != nil {
		t.Fatal(err)
	}
	hash := "doomed"
	c.Put(hash, []byte("no survivors"))
	for _, id := range c.Placement(hash) {
		if err := c.SetUp(id, false); err != nil {
			t.Fatal(err)
		}
	}

	st := c.Repair()
	if len(st.Unrepairable) != 1 || st.Unrepairable[0] != hash {
		t.Fatalf("expected %q unrepairable, got %v", hash, st.Unrepairable)
	}
}

func TestRepairIsIdempotent(t *testing.T) {
	c, err := New(6, 3)
	if err != nil {
		t.Fatal(err)
	}
	seedChunks(t, c, 20)
	if err := c.SetUp(1, false); err != nil {
		t.Fatal(err)
	}
	first := c.Repair()
	second := c.Repair()
	if first.Copied == 0 {
		t.Fatal("first repair should have copied something")
	}
	if second.Copied != 0 || second.UnderReplicated != 0 {
		t.Fatalf("second repair should be a no-op, copied %d", second.Copied)
	}
}

func TestAddNodeRebalancesDeterministically(t *testing.T) {
	build := func() *Cluster {
		c, err := New(5, 3)
		if err != nil {
			t.Fatal(err)
		}
		seedChunks(t, c, 40)
		return c
	}

	c1, c2 := build(), build()
	id1, st1 := c1.AddNode()
	id2, st2 := c2.AddNode()
	if id1 != 5 || id2 != 5 {
		t.Fatalf("new node ids: %d, %d, want 5", id1, id2)
	}
	if st1.ChunksMoved == 0 {
		t.Fatal("adding a node should move a nonzero set of chunks")
	}
	if fmt.Sprint(st1) != fmt.Sprint(st2) {
		t.Fatalf("rebalance not deterministic: %v vs %v", st1, st2)
	}

	// Post-rebalance invariant: every chunk sits exactly on its derived
	// placement set for the new topology.
	for i := 0; i < 40; i++ {
		h := fmt.Sprintf("chunk-%03d", i)
		holders := c1.Holders(h)
		want := c1.Placement(h)
		if fmt.Sprint(holders) != fmt.Sprint(want) {
			t.Fatalf("chunk %s on nodes %v, placement wants %v", h, holders, want)
		}
	}
}

func TestRemoveNodeRebalancesAndPreservesData(t *testing.T) {
	c, err := New(6, 3)
	if err != nil {
		t.Fatal(err)
	}
	hashes := seedChunks(t, c, 40)

	st, err := c.RemoveNode(3)
	if err != nil {
		t.Fatal(err)
	}
	if c.NodeCount() != 5 {
		t.Fatalf("node count %d after removal, want 5", c.NodeCount())
	}
	if st.ChunksMoved == 0 {
		t.Fatal("removing a node should move a nonzero set of chunks")
	}
	for _, h := range hashes {
		holders := c.Holders(h)
		want := c.Placement(h)
		if fmt.Sprint(holders) != fmt.Sprint(want) {
			t.Fatalf("chunk %s on nodes %v, placement wants %v", h, holders, want)
		}
		got, _, err := c.Get(h)
		if err != nil {
			t.Fatalf("get %s after removal: %v", h, err)
		}
		if !bytes.Equal(got, []byte("payload for "+h)) {
			t.Fatalf("chunk %s lost or corrupted by removal", h)
		}
	}
}

func TestRemoveNodeRejectsBreakingReplication(t *testing.T) {
	c, err := New(3, 3)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := c.RemoveNode(0); err == nil {
		t.Fatal("expected error removing a node below the replication factor")
	}
	if _, err := c.RemoveNode(9); err == nil {
		t.Fatal("expected error for unknown node id")
	}
}

func TestRepairConcurrentWithReads(t *testing.T) {
	c, err := New(6, 3)
	if err != nil {
		t.Fatal(err)
	}
	hashes := seedChunks(t, c, 30)
	if err := c.SetUp(4, false); err != nil {
		t.Fatal(err)
	}

	var wg sync.WaitGroup
	for i := 0; i < 4; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for _, h := range hashes {
				if _, _, err := c.Get(h); err != nil {
					t.Errorf("get %s: %v", h, err)
					return
				}
			}
		}()
	}
	c.Repair()
	wg.Wait()

	for _, h := range hashes {
		if live := c.LiveReplicas(h); live != 3 {
			t.Fatalf("chunk %s has %d live replicas, want 3", h, live)
		}
	}
}
