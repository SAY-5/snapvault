// Command snapvault is the distributed layer over the content store: it
// distributes a snapshot's chunks across simulated storage nodes with
// replication, restores them in parallel with per-chunk verification, and
// simulates node failure and recovery.
package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strconv"

	"github.com/SAY-5/snapvault/internal/cluster"
	"github.com/SAY-5/snapvault/internal/distribute"
	"github.com/SAY-5/snapvault/internal/store"
)

const usage = `snapvault - distributed backup and rapid-restore over a content store

usage:
  snapvault put <snapshot> [--store DIR] [--nodes N] [--replicas R]
  snapvault restore <snapshot> <dir> [--store DIR] [--parallel N]
  snapvault fail-node <id> [--store DIR]
  snapvault recover [--store DIR]
  snapvault repair [--store DIR]
  snapvault add-node [--store DIR]
  snapvault remove-node <id> [--store DIR]
  snapvault status [--store DIR]

The cluster topology is fixed at 'put' time and persisted alongside the store.
Chunk placement is deterministic (seeded by chunk hash), so failures and
restores are fully reproducible.
`

func statePath(storeRoot string) string {
	return filepath.Join(storeRoot, "cluster.json")
}

// reorder moves flag arguments (those starting with '-') ahead of positional
// arguments so the standard flag package parses them regardless of the order
// the user typed. Flags that take a separate value (e.g. "--store DIR") keep
// their value adjacent.
func reorder(args []string) []string {
	flagsWithValue := map[string]bool{
		"-store": true, "--store": true,
		"-nodes": true, "--nodes": true,
		"-replicas": true, "--replicas": true,
		"-parallel": true, "--parallel": true,
	}
	var flags, pos []string
	for i := 0; i < len(args); i++ {
		a := args[i]
		if len(a) > 0 && a[0] == '-' {
			flags = append(flags, a)
			// Consume a following value unless the flag used "=".
			if flagsWithValue[a] && i+1 < len(args) {
				flags = append(flags, args[i+1])
				i++
			}
		} else {
			pos = append(pos, a)
		}
	}
	return append(flags, pos...)
}

// rehydrate loads the persisted cluster and re-materializes chunk data from
// the on-disk store for every snapshot, using deterministic placement.
func rehydrate(s *store.Store, storeRoot string) (*cluster.Cluster, error) {
	c, err := cluster.LoadState(statePath(storeRoot))
	if err != nil {
		return nil, fmt.Errorf("no cluster: run 'snapvault put' first (%w)", err)
	}
	snapDir := filepath.Join(storeRoot, "snapshots")
	entries, err := os.ReadDir(snapDir)
	if err != nil {
		return nil, fmt.Errorf("read snapshots: %w", err)
	}
	for _, e := range entries {
		if e.IsDir() || filepath.Ext(e.Name()) != ".json" {
			continue
		}
		name := e.Name()[:len(e.Name())-len(".json")]
		m, err := s.LoadManifest(name)
		if err != nil {
			return nil, err
		}
		for _, h := range m.UniqueChunks() {
			data, err := s.Get(h)
			if err != nil {
				return nil, err
			}
			c.Rematerialize(h, data)
		}
	}
	return c, nil
}

func main() {
	if len(os.Args) < 2 {
		fmt.Fprint(os.Stderr, usage)
		os.Exit(2)
	}
	sub := os.Args[1]
	var code int
	switch sub {
	case "put":
		code = cmdPut(os.Args[2:])
	case "restore":
		code = cmdRestore(os.Args[2:])
	case "fail-node":
		code = cmdFailNode(os.Args[2:])
	case "recover":
		code = cmdRecover(os.Args[2:])
	case "repair":
		code = cmdRepair(os.Args[2:])
	case "add-node":
		code = cmdAddNode(os.Args[2:])
	case "remove-node":
		code = cmdRemoveNode(os.Args[2:])
	case "status":
		code = cmdStatus(os.Args[2:])
	case "-h", "--help", "help":
		fmt.Print(usage)
	default:
		fmt.Fprintf(os.Stderr, "unknown command: %s\n%s", sub, usage)
		code = 2
	}
	os.Exit(code)
}

func cmdPut(args []string) int {
	fs := flag.NewFlagSet("put", flag.ExitOnError)
	storeRoot := fs.String("store", "svstore", "content store root")
	nodes := fs.Int("nodes", 5, "number of simulated storage nodes")
	replicas := fs.Int("replicas", 3, "replication factor")
	_ = fs.Parse(reorder(args))
	if fs.NArg() < 1 {
		fmt.Fprintln(os.Stderr, "put requires <snapshot>")
		return 2
	}
	name := fs.Arg(0)
	s := store.Open(*storeRoot)
	m, err := s.LoadManifest(name)
	if err != nil {
		return fail(err)
	}
	c, err := cluster.New(*nodes, *replicas)
	if err != nil {
		return fail(err)
	}
	n, err := distribute.Distribute(s, c, m)
	if err != nil {
		return fail(err)
	}
	if err := c.SaveState(statePath(*storeRoot)); err != nil {
		return fail(err)
	}
	fmt.Printf("distributed snapshot '%s'\n", name)
	fmt.Printf("  nodes        : %d\n", *nodes)
	fmt.Printf("  replicas     : %d\n", *replicas)
	fmt.Printf("  unique chunks: %d\n", n)
	fmt.Printf("  chunk copies : %d\n", n*(*replicas))
	return 0
}

func cmdRestore(args []string) int {
	fs := flag.NewFlagSet("restore", flag.ExitOnError)
	storeRoot := fs.String("store", "svstore", "content store root")
	parallel := fs.Int("parallel", 4, "number of concurrent fetch workers")
	_ = fs.Parse(reorder(args))
	if fs.NArg() < 2 {
		fmt.Fprintln(os.Stderr, "restore requires <snapshot> <dir>")
		return 2
	}
	name, outDir := fs.Arg(0), fs.Arg(1)
	s := store.Open(*storeRoot)
	m, err := s.LoadManifest(name)
	if err != nil {
		return fail(err)
	}
	c, err := rehydrate(s, *storeRoot)
	if err != nil {
		return fail(err)
	}
	stats, err := distribute.Restore(c, m, outDir, *parallel)
	if err != nil {
		fmt.Fprintf(os.Stderr, "restore failed: %v\n", err)
		return 1
	}
	fmt.Printf("restored snapshot '%s' to %s\n", name, outDir)
	fmt.Printf("  files        : %d\n", stats.Files)
	fmt.Printf("  chunks       : %d\n", stats.Chunks)
	fmt.Printf("  verified     : %d\n", stats.Verified)
	fmt.Printf("  parallelism  : %d\n", *parallel)
	return 0
}

func cmdFailNode(args []string) int {
	fs := flag.NewFlagSet("fail-node", flag.ExitOnError)
	storeRoot := fs.String("store", "svstore", "content store root")
	_ = fs.Parse(reorder(args))
	if fs.NArg() < 1 {
		fmt.Fprintln(os.Stderr, "fail-node requires <id>")
		return 2
	}
	id, err := strconv.Atoi(fs.Arg(0))
	if err != nil {
		return fail(fmt.Errorf("invalid node id %q", fs.Arg(0)))
	}
	c, err := cluster.LoadState(statePath(*storeRoot))
	if err != nil {
		return fail(err)
	}
	if err := c.SetUp(id, false); err != nil {
		return fail(err)
	}
	if err := c.SaveState(statePath(*storeRoot)); err != nil {
		return fail(err)
	}
	fmt.Printf("node %d marked down\n", id)
	return 0
}

func cmdRecover(args []string) int {
	fs := flag.NewFlagSet("recover", flag.ExitOnError)
	storeRoot := fs.String("store", "svstore", "content store root")
	_ = fs.Parse(reorder(args))
	c, err := cluster.LoadState(statePath(*storeRoot))
	if err != nil {
		return fail(err)
	}
	c.RecoverAll()
	if err := c.SaveState(statePath(*storeRoot)); err != nil {
		return fail(err)
	}
	fmt.Println("all nodes recovered")
	return 0
}

func cmdRepair(args []string) int {
	fs := flag.NewFlagSet("repair", flag.ExitOnError)
	storeRoot := fs.String("store", "svstore", "content store root")
	_ = fs.Parse(reorder(args))
	s := store.Open(*storeRoot)
	c, err := rehydrate(s, *storeRoot)
	if err != nil {
		return fail(err)
	}
	st := c.Repair()
	if err := c.SaveState(statePath(*storeRoot)); err != nil {
		return fail(err)
	}
	fmt.Printf("repair\n")
	fmt.Printf("  chunks scanned   : %d\n", st.Scanned)
	fmt.Printf("  under-replicated : %d\n", st.UnderReplicated)
	fmt.Printf("  copies created   : %d\n", st.Copied)
	if len(st.Unrepairable) > 0 {
		for _, h := range st.Unrepairable {
			fmt.Printf("  UNREPAIRABLE %s\n", h)
		}
		fmt.Fprintln(os.Stderr, "repair incomplete: some chunks have no surviving replica")
		return 1
	}
	fmt.Printf("  under-replicated after repair: %d\n", len(c.UnderReplicatedChunks()))
	return 0
}

func cmdAddNode(args []string) int {
	fs := flag.NewFlagSet("add-node", flag.ExitOnError)
	storeRoot := fs.String("store", "svstore", "content store root")
	_ = fs.Parse(reorder(args))
	s := store.Open(*storeRoot)
	c, err := rehydrate(s, *storeRoot)
	if err != nil {
		return fail(err)
	}
	id, st := c.AddNode()
	if err := c.SaveState(statePath(*storeRoot)); err != nil {
		return fail(err)
	}
	fmt.Printf("added node %d (cluster now %d nodes)\n", id, c.NodeCount())
	printRebalance(st)
	return 0
}

func cmdRemoveNode(args []string) int {
	fs := flag.NewFlagSet("remove-node", flag.ExitOnError)
	storeRoot := fs.String("store", "svstore", "content store root")
	_ = fs.Parse(reorder(args))
	if fs.NArg() < 1 {
		fmt.Fprintln(os.Stderr, "remove-node requires <id>")
		return 2
	}
	id, err := strconv.Atoi(fs.Arg(0))
	if err != nil {
		return fail(fmt.Errorf("invalid node id %q", fs.Arg(0)))
	}
	s := store.Open(*storeRoot)
	c, err := rehydrate(s, *storeRoot)
	if err != nil {
		return fail(err)
	}
	st, err := c.RemoveNode(id)
	if err != nil {
		return fail(err)
	}
	if err := c.SaveState(statePath(*storeRoot)); err != nil {
		return fail(err)
	}
	fmt.Printf("removed node %d (cluster now %d nodes)\n", id, c.NodeCount())
	printRebalance(st)
	return 0
}

func printRebalance(st cluster.RebalanceStats) {
	fmt.Printf("  chunks moved   : %d\n", st.ChunksMoved)
	fmt.Printf("  copies added   : %d\n", st.CopiesAdded)
	fmt.Printf("  copies removed : %d\n", st.CopiesRemoved)
	for _, h := range st.Skipped {
		fmt.Printf("  SKIPPED %s (no up holder)\n", h)
	}
}

func cmdStatus(args []string) int {
	fs := flag.NewFlagSet("status", flag.ExitOnError)
	storeRoot := fs.String("store", "svstore", "content store root")
	_ = fs.Parse(reorder(args))
	s := store.Open(*storeRoot)
	c, err := rehydrate(s, *storeRoot)
	if err != nil {
		return fail(err)
	}
	fmt.Printf("cluster: %d nodes, replication %d\n", c.NodeCount(), c.Replication())
	upNodes := 0
	for _, st := range c.Status() {
		state := "up"
		if !st.Up {
			state = "DOWN"
		} else {
			upNodes++
		}
		fmt.Printf("  node %d: %-4s  chunks=%d\n", st.ID, state, st.Chunks)
	}
	fmt.Printf("replication health: %d/%d nodes up\n", upNodes, c.NodeCount())
	fmt.Printf("under-replicated chunks: %d\n", len(c.UnderReplicatedChunks()))
	return 0
}

func fail(err error) int {
	fmt.Fprintf(os.Stderr, "error: %v\n", err)
	return 1
}
