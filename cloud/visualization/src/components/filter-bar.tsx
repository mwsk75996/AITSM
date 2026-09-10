import { useEffect, useState, type FormEvent } from 'react'

import { Button } from '@/components/ui/button'
import { Card, CardContent } from '@/components/ui/card'
import { Input } from '@/components/ui/input'
import type { ReadingsFilters } from '@/lib/types'

type FilterBarProps = {
  filters: ReadingsFilters
  loading: boolean
  onApply: (filters: ReadingsFilters) => void
}

function toInput(value: number | undefined): string {
  return value === undefined ? '' : String(value)
}

function parseNumber(value: string): number | undefined {
  if (value.trim() === '') {
    return undefined
  }

  const parsed = Number(value)

  return Number.isFinite(parsed) ? parsed : undefined
}

export function FilterBar({ filters, loading, onApply }: FilterBarProps) {
  const [date, setDate] = useState(filters.date ?? '')
  const [deviceId, setDeviceId] = useState(filters.deviceId ?? '')
  const [temperatureMin, setTemperatureMin] = useState(toInput(filters.temperatureMin))
  const [temperatureMax, setTemperatureMax] = useState(toInput(filters.temperatureMax))
  const [batteryMin, setBatteryMin] = useState(toInput(filters.batteryMin))
  const [batteryMax, setBatteryMax] = useState(toInput(filters.batteryMax))

  useEffect(() => {
    setDate(filters.date ?? '')
    setDeviceId(filters.deviceId ?? '')
    setTemperatureMin(toInput(filters.temperatureMin))
    setTemperatureMax(toInput(filters.temperatureMax))
    setBatteryMin(toInput(filters.batteryMin))
    setBatteryMax(toInput(filters.batteryMax))
  }, [filters])

  const hasFilters = Object.values(filters).some((value) => value !== undefined)

  function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault()

    onApply({
      date: date || undefined,
      deviceId: deviceId.trim() || undefined,
      temperatureMin: parseNumber(temperatureMin),
      temperatureMax: parseNumber(temperatureMax),
      batteryMin: parseNumber(batteryMin),
      batteryMax: parseNumber(batteryMax),
    })
  }

  function handleReset() {
    onApply({})
  }

  return (
    <Card>
      <CardContent>
        <form className="flex flex-col gap-4" onSubmit={handleSubmit}>
          <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 lg:grid-cols-3">
            <label className="flex flex-col gap-1.5 text-sm">
              <span className="text-muted-foreground">Dato</span>
              <Input
                type="date"
                value={date}
                onChange={(event) => setDate(event.target.value)}
              />
            </label>

            <label className="flex flex-col gap-1.5 text-sm">
              <span className="text-muted-foreground">Enhed</span>
              <Input
                type="text"
                placeholder="Alle enheder"
                value={deviceId}
                onChange={(event) => setDeviceId(event.target.value)}
              />
            </label>

            <label className="flex flex-col gap-1.5 text-sm">
              <span className="text-muted-foreground">Temperatur min (°C)</span>
              <Input
                type="number"
                inputMode="decimal"
                step="0.1"
                placeholder="Ingen nedre grænse"
                value={temperatureMin}
                onChange={(event) => setTemperatureMin(event.target.value)}
              />
            </label>

            <label className="flex flex-col gap-1.5 text-sm">
              <span className="text-muted-foreground">Temperatur max (°C)</span>
              <Input
                type="number"
                inputMode="decimal"
                step="0.1"
                placeholder="Ingen øvre grænse"
                value={temperatureMax}
                onChange={(event) => setTemperatureMax(event.target.value)}
              />
            </label>

            <label className="flex flex-col gap-1.5 text-sm">
              <span className="text-muted-foreground">Batteri min (%)</span>
              <Input
                type="number"
                inputMode="decimal"
                step="0.1"
                placeholder="Ingen nedre grænse"
                value={batteryMin}
                onChange={(event) => setBatteryMin(event.target.value)}
              />
            </label>

            <label className="flex flex-col gap-1.5 text-sm">
              <span className="text-muted-foreground">Batteri max (%)</span>
              <Input
                type="number"
                inputMode="decimal"
                step="0.1"
                placeholder="Ingen øvre grænse"
                value={batteryMax}
                onChange={(event) => setBatteryMax(event.target.value)}
              />
            </label>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            <Button type="submit" size="sm" disabled={loading}>
              Vis målinger
            </Button>
            {hasFilters && (
              <Button type="button" size="sm" variant="ghost" onClick={handleReset}>
                Ryd filtre
              </Button>
            )}
          </div>
        </form>
      </CardContent>
    </Card>
  )
}
