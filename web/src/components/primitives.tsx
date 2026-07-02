import { ReactNode } from "react";

// A short content-address chip, monospace, in the vault accent.
export function HashChip({
  hash,
  n = 7,
  tone = "accent",
  title,
}: {
  hash: string;
  n?: number;
  tone?: "accent" | "steel" | "dim" | "danger";
  title?: string;
}) {
  const color =
    tone === "steel"
      ? "var(--steel-bright)"
      : tone === "dim"
        ? "var(--ink-faint)"
        : tone === "danger"
          ? "var(--danger)"
          : "var(--accent)";
  return (
    <span
      className="mono"
      title={title ?? hash}
      style={{
        color,
        fontSize: "0.72rem",
        letterSpacing: "0.02em",
        padding: "1px 5px",
        borderRadius: 5,
        background: "rgba(53,208,181,0.06)",
        border: "1px solid rgba(53,208,181,0.14)",
        whiteSpace: "nowrap",
      }}
    >
      {hash.slice(0, n)}
    </span>
  );
}

// A labeled readout stat.
export function Stat({
  label,
  value,
  sub,
  tone,
}: {
  label: string;
  value: ReactNode;
  sub?: string;
  tone?: "accent" | "steel" | "danger" | "warn";
}) {
  const color =
    tone === "steel"
      ? "var(--steel-bright)"
      : tone === "danger"
        ? "var(--danger)"
        : tone === "warn"
          ? "var(--warn)"
          : "var(--accent-bright)";
  return (
    <div>
      <div
        style={{
          fontSize: "0.68rem",
          textTransform: "uppercase",
          letterSpacing: "0.14em",
          color: "var(--ink-faint)",
        }}
      >
        {label}
      </div>
      <div
        className="mono"
        style={{ fontSize: "1.5rem", fontWeight: 700, color, lineHeight: 1.2 }}
      >
        {value}
      </div>
      {sub && (
        <div style={{ fontSize: "0.75rem", color: "var(--ink-dim)" }}>{sub}</div>
      )}
    </div>
  );
}

// A section wrapper with an eyebrow, heading, and lede.
export function Section({
  id,
  eyebrow,
  title,
  lede,
  children,
}: {
  id: string;
  eyebrow: string;
  title: string;
  lede?: ReactNode;
  children: ReactNode;
}) {
  return (
    <section id={id} className="sv-section" aria-labelledby={`${id}-h`}>
      <div className="sv-section-head">
        <span className="sv-eyebrow mono">{eyebrow}</span>
        <h2 id={`${id}-h`}>{title}</h2>
        {lede && <p className="sv-lede">{lede}</p>}
      </div>
      {children}
    </section>
  );
}

// A primary or secondary control button.
export function Button({
  children,
  onClick,
  variant = "primary",
  disabled,
  ariaLabel,
}: {
  children: ReactNode;
  onClick?: () => void;
  variant?: "primary" | "ghost" | "danger";
  disabled?: boolean;
  ariaLabel?: string;
}) {
  return (
    <button
      className={`sv-btn sv-btn-${variant}`}
      onClick={onClick}
      disabled={disabled}
      aria-label={ariaLabel}
    >
      {children}
    </button>
  );
}
