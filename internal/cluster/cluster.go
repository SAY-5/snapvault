// Package cluster simulates a set of storage nodes that replicate chunks.
// Placement is deterministic: which nodes hold a chunk depends only on the
// chunk hash, the node count, and the replication factor. No wall-clock or
// random state affects correctness, so runs are reproducible.
package cluster

import (
	"errors"
	"fmt"
	"hash/fnv"
	"sort"
	"sync"
)

// ErrChunkUnavailable is returned when every replica of a chunk is on a
// node that is currently down.
var ErrChunkUnavailable = errors.New("chunk unavailable: all replicas down")

// Node is one simulated storage node. It holds a subset of chunks in memory.
type Node struct {
	ID   int
	up   bool
	data map[string][]byte
}

// Cluster is a fixed set of nodes with a replication factor.
type Cluster struct {
	mu     sync.RWMutex
	nodes  []*Node
	replic int
}

// New builds a cluster of n nodes with replication factor r. It requires
// n >= 1 and 1 <= r <= n.
func New(n, r int) (*Cluster, error) {
	if n < 1 {
		return nil, fmt.Errorf("node count must be >= 1, got %d", n)
	}
	if r < 1 || r > n {
		return nil, fmt.Errorf("replication factor must be in [1,%d], got %d", n, r)
	}
	nodes := make([]*Node, n)
	for i := range nodes {
		nodes[i] = &Node{ID: i, up: true, data: make(map[string][]byte)}
	}
	return &Cluster{nodes: nodes, replic: r}, nil
}

// NodeCount returns the number of nodes.
func (c *Cluster) NodeCount() int { return len(c.nodes) }

// Replication returns the replication factor.
func (c *Cluster) Replication() int { return c.replic }

// Placement returns the deterministic, sorted list of node IDs that should
// hold a replica of the chunk with the given hash. It is a pure function of
// the hash, node count, and replication factor.
func (c *Cluster) Placement(hash string) []int {
	n := len(c.nodes)
	// Seed a starting node from the hash, then walk consecutive nodes so the
	// r replicas are always distinct.
	h := fnv.New64a()
	_, _ = h.Write([]byte(hash))
	start := int(h.Sum64() % uint64(n))
	ids := make([]int, 0, c.replic)
	for i := 0; i < c.replic; i++ {
		ids = append(ids, (start+i)%n)
	}
	sort.Ints(ids)
	return ids
}

// Put stores a chunk on every node in its placement set. It writes to all
// replicas regardless of node up/down state, mirroring an initial upload.
func (c *Cluster) Put(hash string, data []byte) {
	c.mu.Lock()
	defer c.mu.Unlock()
	for _, id := range c.Placement(hash) {
		// Copy so callers cannot mutate stored bytes.
		buf := make([]byte, len(data))
		copy(buf, data)
		c.nodes[id].data[hash] = buf
	}
}

// Get fetches a chunk from the first available (up) replica that holds it.
// Returns ErrChunkUnavailable if no up node has the chunk.
func (c *Cluster) Get(hash string) ([]byte, int, error) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	for _, id := range c.Placement(hash) {
		node := c.nodes[id]
		if !node.up {
			continue
		}
		if buf, ok := node.data[hash]; ok {
			out := make([]byte, len(buf))
			copy(out, buf)
			return out, id, nil
		}
	}
	return nil, -1, ErrChunkUnavailable
}

// SetUp marks a node up or down. Returns an error for an unknown node ID.
func (c *Cluster) SetUp(id int, up bool) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if id < 0 || id >= len(c.nodes) {
		return fmt.Errorf("unknown node id %d", id)
	}
	c.nodes[id].up = up
	return nil
}

// RecoverAll marks every node up.
func (c *Cluster) RecoverAll() {
	c.mu.Lock()
	defer c.mu.Unlock()
	for _, n := range c.nodes {
		n.up = true
	}
}

// NodeStatus is a snapshot of a single node's health.
type NodeStatus struct {
	ID     int
	Up     bool
	Chunks int
}

// Status returns per-node status, ordered by node ID.
func (c *Cluster) Status() []NodeStatus {
	c.mu.RLock()
	defer c.mu.RUnlock()
	out := make([]NodeStatus, len(c.nodes))
	for i, n := range c.nodes {
		out[i] = NodeStatus{ID: n.ID, Up: n.up, Chunks: len(n.data)}
	}
	return out
}

// ChunkAvailable reports whether at least one up node holds the chunk.
func (c *Cluster) ChunkAvailable(hash string) bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	for _, id := range c.Placement(hash) {
		n := c.nodes[id]
		if n.up {
			if _, ok := n.data[hash]; ok {
				return true
			}
		}
	}
	return false
}
