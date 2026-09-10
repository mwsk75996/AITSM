export function toNumber(value: unknown): number | null {
  if (value === null || value === undefined || value === '') {
    return null
  }

  const number = Number(value)

  return Number.isFinite(number) ? number : null
}

export function formatNumber(value: unknown): string | null {
  const number = toNumber(value)

  if (number === null) {
    return null
  }

  return number.toLocaleString('da-DK', { maximumFractionDigits: 1 })
}

export function formatValue(value: unknown, unit: string): string {
  const formatted = formatNumber(value)

  return formatted === null ? '—' : `${formatted} ${unit}`
}

export function formatTimestamp(timestamp: string | null): string {
  if (!timestamp) {
    return '—'
  }

  const date = new Date(timestamp)

  if (Number.isNaN(date.getTime())) {
    return timestamp
  }

  return new Intl.DateTimeFormat('da-DK', {
    dateStyle: 'short',
    timeStyle: 'short',
  }).format(date)
}

export function formatClock(date: Date): string {
  return new Intl.DateTimeFormat('da-DK', {
    hour: '2-digit',
    minute: '2-digit',
  }).format(date)
}

export type BatteryLevel = 'critical' | 'low' | 'ok' | 'unknown'

export function batteryLevel(value: unknown): BatteryLevel {
  const number = toNumber(value)

  if (number === null) {
    return 'unknown'
  }

  if (number <= 10) {
    return 'critical'
  }

  if (number <= 30) {
    return 'low'
  }

  return 'ok'
}

export function clampPercentage(value: unknown): number {
  const number = toNumber(value)

  return number === null ? 0 : Math.min(100, Math.max(0, number))
}
