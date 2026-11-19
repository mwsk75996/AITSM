export type Reading = {
  device_id: string | null
  timestamp: string | null
  temperature: number | null
  battery: number | null
}

export type ReadingsSummary = {
  avg_temperature: number | null
  min_battery: number | null
  /** Thingy:91 X-enheder (device_id thingy…) */
  avg_temperature_thingy: number | null
  /** ESP32 Modbus-enheder (device_id esp32…) */
  avg_temperature_modbus: number | null
}

export type ReadingsFilters = {
  date?: string
  deviceId?: string
  temperatureMin?: number
  temperatureMax?: number
  batteryMin?: number
  batteryMax?: number
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
