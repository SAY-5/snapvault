// Package store reads and writes the content-addressed store shared with the
// C++ engine. The on-disk format is defined in FORMAT.md.
package store

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
)

// FileEntry is one file within a snapshot manifest.
type FileEntry struct {
	Path   string   `json:"path"`
	Size   uint64   `json:"size"`
	Chunks []string `json:"chunks"`
}

// Manifest is a snapshot: a named, ordered list of files.
type Manifest struct {
	Name      string      `json:"name"`
	ChunkSize uint32      `json:"chunk_size"`
	Files     []FileEntry `json:"files"`
}

// Store is a content-addressed store rooted at a directory.
type Store struct {
	root string
}

// Open returns a Store rooted at the given directory. It does not require the
// directory to exist yet.
func Open(root string) *Store {
	return &Store{root: root}
}

// Root returns the store root directory.
func (s *Store) Root() string { return s.root }

// ChunkPath is the absolute path where a chunk with the given hash lives.
func (s *Store) ChunkPath(hash string) string {
	return filepath.Join(s.root, "chunks", hash)
}

// ManifestPath is the path of a snapshot manifest.
func (s *Store) ManifestPath(name string) string {
	return filepath.Join(s.root, "snapshots", name+".json")
}

// Has reports whether a chunk with the given hash is present.
func (s *Store) Has(hash string) bool {
	_, err := os.Stat(s.ChunkPath(hash))
	return err == nil
}

// Get reads a chunk's raw bytes.
func (s *Store) Get(hash string) ([]byte, error) {
	return os.ReadFile(s.ChunkPath(hash))
}

// LoadManifest reads and parses a snapshot manifest by name.
func (s *Store) LoadManifest(name string) (*Manifest, error) {
	data, err := os.ReadFile(s.ManifestPath(name))
	if err != nil {
		return nil, fmt.Errorf("load manifest %q: %w", name, err)
	}
	var m Manifest
	if err := json.Unmarshal(data, &m); err != nil {
		return nil, fmt.Errorf("parse manifest %q: %w", name, err)
	}
	return &m, nil
}

// UniqueChunks returns the sorted, de-duplicated set of chunk hashes
// referenced by the manifest.
func (m *Manifest) UniqueChunks() []string {
	seen := make(map[string]struct{})
	for _, f := range m.Files {
		for _, h := range f.Chunks {
			seen[h] = struct{}{}
		}
	}
	out := make([]string, 0, len(seen))
	for h := range seen {
		out = append(out, h)
	}
	sort.Strings(out)
	return out
}

// HashBytes returns the lowercase hex SHA-256 of b, matching the store's
// content-address scheme.
func HashBytes(b []byte) string {
	sum := sha256.Sum256(b)
	return hex.EncodeToString(sum[:])
}
