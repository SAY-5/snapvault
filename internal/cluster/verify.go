package cluster

import (
	"crypto/sha256"
	"encoding/hex"
	"sort"
)

// hashHex returns the lowercase hex SHA-256 of b, matching the store's
// content-address scheme (FORMAT.md).
func hashHex(b []byte) string {
	sum := sha256.Sum256(b)
	return hex.EncodeToString(sum[:])
}

// NodeVerify is one node's result from a deep verify pass.
type NodeVerify struct {
	ID      int
	Up      bool
	Checked int      // replicas re-hashed on this node
	Bad     []string // hashes whose replica bytes fail their content-address
}

// VerifyDeep re-hashes every replica on every up node against its
// content-address and reports per-node results. Down nodes are reported but
// not checked: they cannot serve reads. A replica is corrupt when its bytes
// no longer reproduce the chunk hash.
func (c *Cluster) VerifyDeep() []NodeVerify {
	c.mu.RLock()
	defer c.mu.RUnlock()
	out := make([]NodeVerify, len(c.nodes))
	for i, n := range c.nodes {
		nv := NodeVerify{ID: n.ID, Up: n.up}
		if n.up {
			for h, buf := range n.data {
				nv.Checked++
				if hashHex(buf) != h {
					nv.Bad = append(nv.Bad, h)
				}
			}
			sort.Strings(nv.Bad)
		}
		out[i] = nv
	}
	return out
}
