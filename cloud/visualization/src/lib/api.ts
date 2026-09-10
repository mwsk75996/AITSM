import type { ReadingsResponse } from '@/lib/types'

type FetchReadingsParams = {
  pageSize: number
  cursor: string | null
}

export async function fetchReadings({
  pageSize,
  cursor,
}: FetchReadingsParams): Promise<ReadingsResponse> {
  const params = new URLSearchParams({ page_size: String(pageSize) })

  if (cursor !== null) {
    params.set('cursor', cursor)
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
