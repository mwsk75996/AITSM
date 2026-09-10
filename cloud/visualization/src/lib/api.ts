import type { ReadingsFilters, ReadingsResponse } from '@/lib/types'

type FetchReadingsParams = {
  pageSize: number
  cursor: string | null
  filters?: ReadingsFilters
}

export async function fetchReadings({
  pageSize,
  cursor,
  filters = {},
}: FetchReadingsParams): Promise<ReadingsResponse> {
  const params = new URLSearchParams({ page_size: String(pageSize) })

  if (cursor !== null) {
    params.set('cursor', cursor)
  }

  if (filters.date) {
    params.set('date', filters.date)
  }

  if (filters.deviceId) {
    params.set('device_id', filters.deviceId)
  }

  if (filters.temperatureMin !== undefined) {
    params.set('temperature_min', String(filters.temperatureMin))
  }

  if (filters.temperatureMax !== undefined) {
    params.set('temperature_max', String(filters.temperatureMax))
  }

  if (filters.batteryMin !== undefined) {
    params.set('battery_min', String(filters.batteryMin))
  }

  if (filters.batteryMax !== undefined) {
    params.set('battery_max', String(filters.batteryMax))
  }

  const response = await fetch(`api.php?${params.toString()}`, {
    cache: 'no-store',
    headers: { Accept: 'application/json' },
  })

  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`)
  }

  const data = (await response.json()) as Partial<ReadingsResponse> & {
    detail?: string
  }

  if (data.status !== 'ok' || !Array.isArray(data.readings)) {
    throw new Error(data.detail || 'Ugyldigt API-svar')
  }

  return data as ReadingsResponse
}
