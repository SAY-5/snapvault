// Port of core/chunker.cpp: fixed-size chunking, each chunk content-addressed
// by the hex SHA-256 of its bytes. The final chunk of a buffer may be smaller
// than the chunk size.
import { sha256Hex } from "./sha256";

// The C++ engine defaults to 4096 bytes. The browser demo uses a much smaller
// default so a handful of small mock files split into a legible number of
// chunks on screen; the algorithm is identical.
export const DEFAULT_CHUNK_SIZE = 64;

export interface Chunk {
  hash: string;
  data: Uint8Array;
}

// Split a buffer into fixed-size chunks, each hashed by content.
export function chunkBuffer(
  buffer: Uint8Array,
  chunkSize: number = DEFAULT_CHUNK_SIZE
): Chunk[] {
  if (chunkSize <= 0) chunkSize = DEFAULT_CHUNK_SIZE;
  const chunks: Chunk[] = [];
  let offset = 0;
  while (offset < buffer.length) {
    const len = Math.min(chunkSize, buffer.length - offset);
    const data = buffer.subarray(offset, offset + len);
    chunks.push({ hash: sha256Hex(data), data });
    offset += len;
  }
  return chunks;
}
