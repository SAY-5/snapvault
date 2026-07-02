// Package distribute puts snapshot chunks onto a simulated cluster and
// restores them in parallel, verifying each chunk's content-address on
// arrival.
package distribute

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"sync"

	"github.com/SAY-5/snapvault/internal/cluster"
	"github.com/SAY-5/snapvault/internal/store"
)

// Distribute uploads every unique chunk of the manifest to the cluster,
// replicating per the cluster's placement policy. Returns the number of
// unique chunks distributed.
func Distribute(s *store.Store, c *cluster.Cluster, m *store.Manifest) (int, error) {
	chunks := m.UniqueChunks()
	for _, h := range chunks {
		data, err := s.Get(h)
		if err != nil {
			return 0, fmt.Errorf("read chunk %s: %w", h, err)
		}
		c.Put(h, data)
	}
	return len(chunks), nil
}

// RestoreStats summarizes a parallel restore.
type RestoreStats struct {
	Files    int
	Chunks   int // unique chunks fetched
	Verified int // chunks that passed hash verification
}

// fetchResult carries a fetched, verified chunk back to the caller.
type fetchResult struct {
	hash string
	data []byte
	err  error
}

// Restore rebuilds every file of the manifest under outDir, fetching the
// unique chunks concurrently with `parallel` workers. Each chunk is
// hash-verified against its content-address as it arrives; a mismatch or an
// unavailable chunk aborts the restore.
func Restore(c *cluster.Cluster, m *store.Manifest, outDir string, parallel int) (RestoreStats, error) {
	var stats RestoreStats
	if parallel < 1 {
		parallel = 1
	}

	unique := m.UniqueChunks()
	stats.Chunks = len(unique)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	jobs := make(chan string)
	results := make(chan fetchResult)

	var wg sync.WaitGroup
	for i := 0; i < parallel; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for {
				select {
				case <-ctx.Done():
					return
				case h, ok := <-jobs:
					if !ok {
						return
					}
					data, _, err := c.Get(h)
					if err == nil {
						// Verify the content-address on arrival.
						if got := store.HashBytes(data); got != h {
							err = fmt.Errorf("chunk %s failed integrity check (got %s)", h, got)
						}
					}
					select {
					case results <- fetchResult{hash: h, data: data, err: err}:
					case <-ctx.Done():
						return
					}
				}
			}
		}()
	}

	// Feed jobs.
	go func() {
		defer close(jobs)
		for _, h := range unique {
			select {
			case jobs <- h:
			case <-ctx.Done():
				return
			}
		}
	}()

	// Close results once all workers finish.
	go func() {
		wg.Wait()
		close(results)
	}()

	fetched := make(map[string][]byte, len(unique))
	var firstErr error
	for r := range results {
		if r.err != nil {
			if firstErr == nil {
				firstErr = r.err
				cancel()
			}
			continue
		}
		fetched[r.hash] = r.data
		stats.Verified++
	}
	if firstErr != nil {
		return stats, firstErr
	}

	// Reassemble files from verified chunks, ordered deterministically.
	files := make([]store.FileEntry, len(m.Files))
	copy(files, m.Files)
	sort.Slice(files, func(i, j int) bool { return files[i].Path < files[j].Path })

	for _, f := range files {
		out := make([]byte, 0, f.Size)
		for _, h := range f.Chunks {
			data, ok := fetched[h]
			if !ok {
				return stats, fmt.Errorf("missing chunk %s for file %s", h, f.Path)
			}
			out = append(out, data...)
		}
		dst := filepath.Join(outDir, filepath.FromSlash(f.Path))
		if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
			return stats, err
		}
		if err := os.WriteFile(dst, out, 0o644); err != nil {
			return stats, err
		}
		stats.Files++
	}
	return stats, nil
}
