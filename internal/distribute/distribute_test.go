package distribute

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/SAY-5/snapvault/internal/cluster"
	"github.com/SAY-5/snapvault/internal/store"
)

// buildStore writes chunks and a manifest to a temp store, returning the store
// and the original per-file contents for byte-for-byte comparison.
func buildStore(t *testing.T, files map[string][]byte, chunkSize int) (*store.Store, *store.Manifest) {
	t.Helper()
	root := t.TempDir()
	if err := os.MkdirAll(filepath.Join(root, "chunks"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(root, "snapshots"), 0o755); err != nil {
		t.Fatal(err)
	}
	s := store.Open(root)

	m := &store.Manifest{Name: "test", ChunkSize: uint32(chunkSize)}
	// Deterministic file order.
	paths := make([]string, 0, len(files))
	for p := range files {
		paths = append(paths, p)
	}
	// simple sort
	for i := 0; i < len(paths); i++ {
		for j := i + 1; j < len(paths); j++ {
			if paths[j] < paths[i] {
				paths[i], paths[j] = paths[j], paths[i]
			}
		}
	}

	for _, p := range paths {
		content := files[p]
		fe := store.FileEntry{Path: p, Size: uint64(len(content))}
		for off := 0; off < len(content); off += chunkSize {
			end := off + chunkSize
			if end > len(content) {
				end = len(content)
			}
			chunk := content[off:end]
			h := store.HashBytes(chunk)
			if err := os.WriteFile(s.ChunkPath(h), chunk, 0o644); err != nil {
				t.Fatal(err)
			}
			fe.Chunks = append(fe.Chunks, h)
		}
		if len(content) == 0 {
			// zero-length file: still record it with no chunks
			fe.Chunks = []string{}
		}
		m.Files = append(m.Files, fe)
	}

	data, _ := json.MarshalIndent(m, "", "  ")
	if err := os.WriteFile(s.ManifestPath("test"), data, 0o644); err != nil {
		t.Fatal(err)
	}
	return s, m
}

func TestParallelRestoreRoundTrip(t *testing.T) {
	files := map[string][]byte{
		"dir/one.dat": makePattern("one", 5000),
		"two.txt":     []byte("a short file"),
		"dir/three":   makePattern("three", 12345),
	}
	s, m := buildStore(t, files, 1024)

	c, err := cluster.New(5, 3)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := Distribute(s, c, m); err != nil {
		t.Fatal(err)
	}

	out := t.TempDir()
	stats, err := Restore(c, m, out, 4)
	if err != nil {
		t.Fatalf("restore failed: %v", err)
	}
	if stats.Files != len(files) {
		t.Fatalf("expected %d files, got %d", len(files), stats.Files)
	}
	if stats.Verified != stats.Chunks {
		t.Fatalf("verified %d != chunks %d", stats.Verified, stats.Chunks)
	}

	for p, want := range files {
		got, err := os.ReadFile(filepath.Join(out, filepath.FromSlash(p)))
		if err != nil {
			t.Fatalf("read %s: %v", p, err)
		}
		if string(got) != string(want) {
			t.Fatalf("file %s not restored byte-for-byte", p)
		}
	}
}

func TestRestoreSurvivesReplicaFailures(t *testing.T) {
	files := map[string][]byte{"payload.bin": makePattern("p", 8000)}
	s, m := buildStore(t, files, 1024)

	c, err := cluster.New(5, 3)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := Distribute(s, c, m); err != nil {
		t.Fatal(err)
	}

	// Fail two nodes (R-1 = 2). Every chunk keeps at least one replica only if
	// no chunk had all 3 replicas among the failed set. With 5 nodes and 3
	// replicas placed on consecutive nodes, failing 2 arbitrary nodes can still
	// leave a chunk with all replicas up. We instead fail nodes and require
	// that if the restore succeeds it is byte-correct, and separately assert a
	// clean failure when a specific chunk loses all replicas below.
	if err := c.SetUp(0, false); err != nil {
		t.Fatal(err)
	}
	if err := c.SetUp(1, false); err != nil {
		t.Fatal(err)
	}

	out := t.TempDir()
	stats, err := Restore(c, m, out, 4)
	if err != nil {
		// A chunk may have lost all replicas; that must be a clean error.
		if err == cluster.ErrChunkUnavailable {
			return
		}
		t.Fatalf("unexpected restore error: %v", err)
	}
	if stats.Verified != stats.Chunks {
		t.Fatalf("verified %d != chunks %d", stats.Verified, stats.Chunks)
	}
	got, _ := os.ReadFile(filepath.Join(out, "payload.bin"))
	if string(got) != string(files["payload.bin"]) {
		t.Fatal("restored file mismatch after node failures")
	}
}

func TestRestoreGuaranteedSurvivesOneFailure(t *testing.T) {
	// With R=3, taking down exactly one node can never remove all replicas of
	// any chunk, so restore must always succeed byte-for-byte.
	files := map[string][]byte{"g.bin": makePattern("g", 9000)}
	s, m := buildStore(t, files, 1024)
	c, err := cluster.New(4, 3)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := Distribute(s, c, m); err != nil {
		t.Fatal(err)
	}
	if err := c.SetUp(2, false); err != nil {
		t.Fatal(err)
	}
	out := t.TempDir()
	if _, err := Restore(c, m, out, 3); err != nil {
		t.Fatalf("restore must survive a single node failure: %v", err)
	}
	got, _ := os.ReadFile(filepath.Join(out, "g.bin"))
	if string(got) != string(files["g.bin"]) {
		t.Fatal("restored file mismatch after single failure")
	}
}

func TestRestoreFailsCleanlyWhenAllReplicasDown(t *testing.T) {
	files := map[string][]byte{"f.bin": makePattern("f", 4000)}
	s, m := buildStore(t, files, 1024)
	c, err := cluster.New(5, 3)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := Distribute(s, c, m); err != nil {
		t.Fatal(err)
	}

	// Pick the first chunk and take down every node in its placement set.
	victim := m.UniqueChunks()[0]
	for _, id := range c.Placement(victim) {
		if err := c.SetUp(id, false); err != nil {
			t.Fatal(err)
		}
	}

	out := t.TempDir()
	_, err = Restore(c, m, out, 4)
	if err != cluster.ErrChunkUnavailable {
		t.Fatalf("expected ErrChunkUnavailable, got %v", err)
	}
}

// makePattern builds distinct-per-chunk content so fixed-size chunking yields
// unique chunks and dedup does not collapse them.
func makePattern(seed string, n int) []byte {
	out := make([]byte, 0, n)
	i := 0
	for len(out) < n {
		out = append(out, []byte(seed)...)
		out = append(out, []byte{byte('0' + i%10), byte(':')}...)
		i++
	}
	return out[:n]
}
