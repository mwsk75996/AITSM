import { batteryLevel, clampPercentage, formatValue } from '@/lib/format'
import { cn } from '@/lib/utils'

const FILL_STYLES = {
  critical: 'bg-red-500',
  low: 'bg-amber-500',
  ok: 'bg-emerald-500',
  unknown: 'bg-muted-foreground/40',
} as const

type BatteryIndicatorProps = {
  value: number | null
  className?: string
}

export function BatteryIndicator({ value, className }: BatteryIndicatorProps) {
  const level = batteryLevel(value)
  const label = formatValue(value, '%')

  return (
    <span
      className={cn('inline-flex items-center gap-2', className)}
      role="img"
      aria-label={`Batteri ${label}`}
    >
      <span className="relative h-3.5 w-8 rounded-[3px] border border-muted-foreground/50 p-[2px]">
        <span
          className={cn('block h-full rounded-[1px] transition-[width]', FILL_STYLES[level])}
          style={{ width: `${clampPercentage(value)}%` }}
        />
      </span>
      <span className="text-sm tabular-nums text-muted-foreground">{label}</span>
    </span>
  )
}
