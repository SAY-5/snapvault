// Port of core/content_store.cpp: content-addressed storage. Writing a chunk
// whose hash already exists is a no-op, which gives deduplication for free.
// In the browser the "filesystem" is an in-memory map keyed by content-address.

export class ContentStore {
  private chunks = new Map<string, Uint8Array>();

  // True if a chunk with this hash is present.
  has(hash: string): boolean {
    return this.chunks.has(hash);
  }

  // Store a chunk. Returns true if newly written, false if the content-address
  // already existed (deduplicated).
  put(hash: string, data: Uint8Array): boolean {
    if (this.chunks.has(hash)) return false;
    this.chunks.set(hash, data.slice());
    return true;
  }

  // Read a chunk's bytes. Throws if missing (mirrors the C++ engine).
  get(hash: string): Uint8Array {
    const buf = this.chunks.get(hash);
    if (!buf) throw new Error("chunk not found in store: " + hash);
    return buf.slice();
  }

  // Number of unique chunks stored (distinct content-addresses).
  size(): number {
    return this.chunks.size;
  }

  // All stored content-addresses.
  hashes(): string[] {
    return [...this.chunks.keys()];
  }
}
