import type { ComponentType } from 'react'
import { Activity, Battery, Clock, Thermometer } from 'lucide-react'

import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from '@/components/ui/card'
import { formatClock, formatValue, toNumber } from '@/lib/format'
import { cn } from '@/lib/utils'
import type { ReadingsSummary } from '@/lib/types'

type KpiGridProps = {
  total: number
  summary: ReadingsSummary | null
  lastUpdated: Date | null
  loading: boolean
}

type KpiCardProps = {
  label: string
  value: string
  hint: string
  accent: string
  icon: ComponentType<{ className?: string }>
}

function KpiCard({ label, value, hint, accent, icon: Icon }: KpiCardProps) {
  return (
    <Card>
      <CardHeader>
        <div className="flex items-center justify-between gap-2">
          <CardDescription>{label}</CardDescription>
          <span className={cn('flex size-8 items-center justify-center rounded-md', accent)}>
            <Icon className="size-4" />
          </span>
        </div>
        <CardTitle className="text-3xl tabular-nums">{value}</CardTitle>
      </CardHeader>
      <CardContent>
        <p className="text-xs text-muted-foreground">{hint}</p>
      </CardContent>
    </Card>
  )
}

export function KpiGrid({ total, summary, lastUpdated, loading }: KpiGridProps) {
  const avgTemperature = toNumber(summary?.avg_temperature)
  const minBattery = toNumber(summary?.min_battery)
  const hasData = !loading || total > 0

  const countHint = total === 1 ? 'måling i historikken' : 'målinger i historikken'
  const batteryHint =
    minBattery === null
      ? 'Ingen batteridata'
      : minBattery <= 30
        ? 'Enhed bør oplades'
        : 'Alle enheder har strøm nok'

  return (
    <section
      className="grid grid-cols-1 gap-4 sm:grid-cols-2 lg:grid-cols-4"
      aria-label="Nøgletal"
    >
      <KpiCard
        label="Målinger"
        value={hasData ? total.toLocaleString('da-DK') : '—'}
        hint={countHint}
        accent="bg-sky-500/15 text-sky-600 dark:text-sky-400"
        icon={Activity}
      />
      <KpiCard
        label="Gns. temperatur"
        value={formatValue(avgTemperature, '°C')}
        hint="På tværs af målinger"
        accent="bg-amber-500/15 text-amber-600 dark:text-amber-400"
        icon={Thermometer}
      />
      <KpiCard
        label="Laveste batteri"
        value={formatValue(minBattery, '%')}
        hint={batteryHint}
        accent="bg-emerald-500/15 text-emerald-600 dark:text-emerald-400"
        icon={Battery}
      />
      <KpiCard
        label="Senest opdateret"
        value={lastUpdated ? formatClock(lastUpdated) : '—'}
        hint="Hvert 30. sekund"
        accent="bg-violet-500/15 text-violet-600 dark:text-violet-400"
        icon={Clock}
      />
    </section>
  )
}
