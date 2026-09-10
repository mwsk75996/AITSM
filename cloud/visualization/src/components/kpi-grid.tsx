import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from '@/components/ui/card'
import { formatClock, formatValue, toNumber } from '@/lib/format'
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
}

function KpiCard({ label, value, hint }: KpiCardProps) {
  return (
    <Card>
      <CardHeader>
        <CardDescription>{label}</CardDescription>
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
      />
      <KpiCard
        label="Gns. temperatur"
        value={formatValue(avgTemperature, '°C')}
        hint="På tværs af målinger"
      />
      <KpiCard
        label="Laveste batteri"
        value={formatValue(minBattery, '%')}
        hint={batteryHint}
      />
      <KpiCard
        label="Senest opdateret"
        value={lastUpdated ? formatClock(lastUpdated) : '—'}
        hint="Hvert 30. sekund"
      />
    </section>
  )
}
