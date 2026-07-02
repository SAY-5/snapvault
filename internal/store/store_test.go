package store

import (
	"os"
	"path/filepath"
	"testing"
)

// writeStore builds a minimal content store on disk in the FORMAT.md layout.
func writeStore(t *testing.T) (*Store, string) {
	t.Helper()
	root := t.TempDir()
	chunks := filepath.Join(root, "chunks")
	snaps := filepath.Join(root, "snapshots")
	if err := os.MkdirAll(chunks, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(snaps, 0o755); err != nil {
		t.Fatal(err)
	}
	return Open(root), root
}

func TestHashBytesMatchesKnownVector(t *testing.T) {
	// SHA-256("abc") from FIPS 180-4.
	got := HashBytes([]byte("abc"))
	want := "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
	if got != want {
		t.Fatalf("HashBytes(abc) = %s, want %s", got, want)
	}
}

func TestPutGetAndManifestRoundTrip(t *testing.T) {
	s, root := writeStore(t)

	// Store two chunks by content address.
	c0 := []byte("first chunk of data ")
	c1 := []byte("second chunk of data")
	h0 := HashBytes(c0)
	h1 := HashBytes(c1)
	if err := os.WriteFile(s.ChunkPath(h0), c0, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(s.ChunkPath(h1), c1, 0o644); err != nil {
		t.Fatal(err)
	}

	if !s.Has(h0) || !s.Has(h1) {
		t.Fatal("stored chunks should be present")
	}
	if got, err := s.Get(h0); err != nil || string(got) != string(c0) {
		t.Fatalf("get c0: %v %q", err, got)
	}

	// Write a manifest in the shared JSON format and read it back.
	manifestJSON := `{
  "name": "t1",
  "chunk_size": 4096,
  "files": [
    { "path": "a/b.txt", "size": 40, "chunks": ["` + h0 + `", "` + h1 + `"] }
  ]
}`
	if err := os.WriteFile(s.ManifestPath("t1"), []byte(manifestJSON), 0o644); err != nil {
		t.Fatal(err)
	}

	m, err := s.LoadManifest("t1")
	if err != nil {
		t.Fatal(err)
	}
	if m.Name != "t1" || m.ChunkSize != 4096 {
		t.Fatalf("unexpected manifest header: %+v", m)
	}
	if len(m.Files) != 1 || m.Files[0].Path != "a/b.txt" {
		t.Fatalf("unexpected files: %+v", m.Files)
	}
	if len(m.Files[0].Chunks) != 2 {
		t.Fatalf("expected 2 chunk refs, got %d", len(m.Files[0].Chunks))
	}

	uniq := m.UniqueChunks()
	if len(uniq) != 2 {
		t.Fatalf("expected 2 unique chunks, got %d", len(uniq))
	}

	// Root reported correctly.
	if s.Root() != root {
		t.Fatalf("root mismatch: %s vs %s", s.Root(), root)
	}
}

func TestUniqueChunksDeduplicates(t *testing.T) {
	m := &Manifest{
		Files: []FileEntry{
			{Path: "x", Chunks: []string{"a", "b", "a"}},
			{Path: "y", Chunks: []string{"b", "c"}},
		},
	}
	uniq := m.UniqueChunks()
	if len(uniq) != 3 {
		t.Fatalf("expected 3 unique chunks, got %d: %v", len(uniq), uniq)
	}
}
