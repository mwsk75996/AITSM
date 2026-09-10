export type Reading = {
  device_id: string | null
  timestamp: string | null
  temperature: number | null
  battery: number | null
}

export type ReadingsSummary = {
  avg_temperature: number | null
  min_battery: number | null
}

export type ReadingsResponse = {
  status: 'ok'
  page_size: number
  total: number
  has_more: boolean
  next_cursor: string | null
  summary: ReadingsSummary
  readings: Reading[]
}

export type ConnectionStatus = 'pending' | 'online' | 'offline'
