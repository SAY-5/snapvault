package cluster

import (
	"fmt"
	"sort"
)

// RepairStats summarizes a repair pass.
type RepairStats struct {
	Scanned         int      // unique chunks examined
	UnderReplicated int      // chunks below the replication factor before repair
	Copied          int      // replica copies created
	CorruptFixed    int      // corrupted replicas overwritten from a good copy
	Unrepairable    []string // chunks with no intact surviving replica, sorted
}

// RebalanceStats summarizes chunk movement during a topology change.
type RebalanceStats struct {
	ChunksMoved   int      // chunks whose holder set changed
	CopiesAdded   int      // replica copies created on newly-placed nodes
	CopiesRemoved int      // replica copies deleted from vacated nodes
	Skipped       []string // chunks with no up holder to copy from, sorted
}

// allChunksLocked returns the sorted union of chunk hashes held anywhere in
// the cluster. Caller must hold c.mu.
func (c *Cluster) allChunksLocked() []string {
	seen := make(map[string]struct{})
	for _, n := range c.nodes {
		for h := range n.data {
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

// UnderReplicatedChunks returns the sorted hashes whose live replica count
// (copies on up nodes) is below the replication factor.
func (c *Cluster) UnderReplicatedChunks() []string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	var out []string
	for _, h := range c.allChunksLocked() {
		if c.liveReplicasLocked(h) < c.replic {
			out = append(out, h)
		}
	}
	return out
}

// Repair restores every chunk to full health. A replica only counts as live
// if its node is up and its bytes still reproduce the chunk's
// content-address; corrupted replicas are overwritten from an intact copy,
// and chunks whose live count fell below the replication factor are copied
// to up nodes that do not yet hold them. Target nodes are chosen
// deterministically by walking from the chunk's placement start. A chunk
// with no intact surviving replica is reported as unrepairable.
func (c *Cluster) Repair() RepairStats {
	c.mu.Lock()
	defer c.mu.Unlock()

	var st RepairStats
	n := len(c.nodes)
	for _, hash := range c.allChunksLocked() {
		st.Scanned++
		var src []byte
		var corrupt []*Node
		live := 0
		for _, node := range c.nodes {
			if !node.up {
				continue
			}
			buf, ok := node.data[hash]
			if !ok {
				continue
			}
			if hashHex(buf) == hash {
				live++
				if src == nil {
					src = buf
				}
			} else {
				corrupt = append(corrupt, node)
			}
		}
		if live >= c.replic && len(corrupt) == 0 {
			continue
		}
		if live < c.replic {
			st.UnderReplicated++
		}
		if src == nil {
			st.Unrepairable = append(st.Unrepairable, hash)
			continue
		}
		for _, node := range corrupt {
			buf := make([]byte, len(src))
			copy(buf, src)
			node.data[hash] = buf
			st.CorruptFixed++
			live++
		}
		start := placementStart(hash, n)
		for i := 0; i < n && live < c.replic; i++ {
			node := c.nodes[(start+i)%n]
			if !node.up {
				continue
			}
			if _, ok := node.data[hash]; ok {
				continue
			}
			buf := make([]byte, len(src))
			copy(buf, src)
			node.data[hash] = buf
			st.Copied++
			live++
		}
	}
	return st
}

// AddNode appends a new (up, empty) node and rebalances chunk placement for
// the grown topology. Returns the new node's ID and the movement stats.
func (c *Cluster) AddNode() (int, RebalanceStats) {
	c.mu.Lock()
	defer c.mu.Unlock()
	id := len(c.nodes)
	c.nodes = append(c.nodes, &Node{ID: id, up: true, data: make(map[string][]byte)})
	return id, c.rebalanceLocked()
}

// RemoveNode drains a node and removes it, renumbering the remaining nodes
// and rebalancing chunk placement for the shrunk topology. Fails if removal
// would leave fewer nodes than the replication factor.
func (c *Cluster) RemoveNode(id int) (RebalanceStats, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if id < 0 || id >= len(c.nodes) {
		return RebalanceStats{}, fmt.Errorf("unknown node id %d", id)
	}
	if len(c.nodes)-1 < c.replic {
		return RebalanceStats{}, fmt.Errorf(
			"cannot remove node %d: %d nodes would not satisfy replication %d",
			id, len(c.nodes)-1, c.replic)
	}

	// Drain: any chunk held only by the victim must survive somewhere before
	// the node disappears.
	var st RebalanceStats
	victim := c.nodes[id]
	for h, buf := range victim.data {
		held := false
		for _, n := range c.nodes {
			if n.ID != id {
				if _, ok := n.data[h]; ok {
					held = true
					break
				}
			}
		}
		if held {
			continue
		}
		for _, n := range c.nodes {
			if n.ID == id || !n.up {
				continue
			}
			cp := make([]byte, len(buf))
			copy(cp, buf)
			n.data[h] = cp
			st.CopiesAdded++
			break
		}
	}

	c.nodes = append(c.nodes[:id], c.nodes[id+1:]...)
	for i, n := range c.nodes {
		n.ID = i
	}
	reb := c.rebalanceLocked()
	st.ChunksMoved = reb.ChunksMoved
	st.CopiesAdded += reb.CopiesAdded
	st.CopiesRemoved = reb.CopiesRemoved
	st.Skipped = reb.Skipped
	return st, nil
}

// rebalanceLocked moves chunk replicas so every up node holds exactly the
// chunks the derived placement assigns it. Down nodes are not touched: they
// can neither receive nor serve copies. Caller must hold c.mu.
func (c *Cluster) rebalanceLocked() RebalanceStats {
	var st RebalanceStats
	for _, hash := range c.allChunksLocked() {
		desired := make(map[int]bool, c.replic)
		for _, id := range c.Placement(hash) {
			desired[id] = true
		}

		var src []byte
		for _, n := range c.nodes {
			if !n.up {
				continue
			}
			if buf, ok := n.data[hash]; ok {
				src = buf
				break
			}
		}
		if src == nil {
			st.Skipped = append(st.Skipped, hash)
			continue
		}

		moved := false
		for _, id := range c.Placement(hash) {
			n := c.nodes[id]
			if !n.up {
				continue
			}
			if _, ok := n.data[hash]; !ok {
				buf := make([]byte, len(src))
				copy(buf, src)
				n.data[hash] = buf
				st.CopiesAdded++
				moved = true
			}
		}
		for _, n := range c.nodes {
			if !n.up || desired[n.ID] {
				continue
			}
			if _, ok := n.data[hash]; ok {
				delete(n.data, hash)
				st.CopiesRemoved++
				moved = true
			}
		}
		if moved {
			st.ChunksMoved++
		}
	}
	return st
}
