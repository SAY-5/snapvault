package cluster

import (
	"encoding/json"
	"fmt"
	"os"
	"sort"
)

// State is the persistable configuration of a simulated cluster: its size,
// replication factor, and which nodes are currently down. Chunk placement is
// derived deterministically from the hash, so it is not stored.
type State struct {
	Nodes       int   `json:"nodes"`
	Replication int   `json:"replication"`
	Down        []int `json:"down"`
	// Extra records replicas living outside the derived placement (created
	// by repair while nodes were down), keyed by chunk hash.
	Extra map[string][]int `json:"extra,omitempty"`
}

// SaveState writes the cluster's configuration, down-node set, and any
// off-placement replica locations to path.
func (c *Cluster) SaveState(path string) error {
	c.mu.RLock()
	st := State{Nodes: len(c.nodes), Replication: c.replic}
	for _, n := range c.nodes {
		if !n.up {
			st.Down = append(st.Down, n.ID)
		}
	}
	held := 0
	for _, n := range c.nodes {
		held += len(n.data)
		for h := range n.data {
			placed := false
			for _, id := range c.Placement(h) {
				if id == n.ID {
					placed = true
					break
				}
			}
			if !placed {
				if st.Extra == nil {
					st.Extra = make(map[string][]int)
				}
				st.Extra[h] = append(st.Extra[h], n.ID)
			}
		}
	}
	if held == 0 {
		// A cluster loaded without re-materialized chunk data (fail-node,
		// recover) must not lose previously recorded extra replicas.
		st.Extra = c.extra
	}
	c.mu.RUnlock()
	sort.Ints(st.Down)
	for _, ids := range st.Extra {
		sort.Ints(ids)
	}
	data, err := json.MarshalIndent(st, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, data, 0o644)
}

// LoadState rebuilds a cluster from a saved State file, restoring the
// down-node set. Chunk data is re-materialized by re-running Put.
func LoadState(path string) (*Cluster, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var st State
	if err := json.Unmarshal(data, &st); err != nil {
		return nil, fmt.Errorf("parse cluster state: %w", err)
	}
	c, err := New(st.Nodes, st.Replication)
	if err != nil {
		return nil, err
	}
	for _, id := range st.Down {
		if err := c.SetUp(id, false); err != nil {
			return nil, err
		}
	}
	c.extra = st.Extra
	return c, nil
}
