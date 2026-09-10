import { useCallback, useEffect, useRef, useState } from 'react'

import { fetchReadings } from '@/lib/api'
import type {
  ConnectionStatus,
  Reading,
  ReadingsSummary,
} from '@/lib/types'

const REFRESH_INTERVAL_MS = 30_000
const DEFAULT_PAGE_SIZE = 50

export type ReadingsState = {
  readings: Reading[]
  summary: ReadingsSummary | null
  total: number
  hasMore: boolean
  pageSize: number
  pageIndex: number
  status: ConnectionStatus
  loading: boolean
  lastUpdated: Date | null
}

const initialState: ReadingsState = {
  readings: [],
  summary: null,
  total: 0,
  hasMore: false,
  pageSize: DEFAULT_PAGE_SIZE,
  pageIndex: 0,
  status: 'pending',
  loading: true,
  lastUpdated: null,
}

export function useReadings() {
  const [state, setState] = useState<ReadingsState>(initialState)
  // cursors[i] er cursoren der skal bruges for at hente side i. cursors[0] er altid null.
  const cursors = useRef<(string | null)[]>([null])
  const current = useRef({ pageIndex: 0, pageSize: DEFAULT_PAGE_SIZE })

  const load = useCallback(
    async (requestedIndex: number, requestedPageSize?: number) => {
      const pageSize = requestedPageSize ?? current.current.pageSize
      const pageIndex = Math.max(0, requestedIndex)
      const cursor = cursors.current[pageIndex] ?? null

      setState((previous) => ({ ...previous, loading: true }))

      try {
        const data = await fetchReadings({ pageSize, cursor })

        current.current = { pageIndex, pageSize: data.page_size }

        if (data.has_more && data.next_cursor) {
          cursors.current[pageIndex + 1] = data.next_cursor
        } else {
          cursors.current.length = pageIndex + 1
        }

        setState({
          readings: data.readings,
          summary: data.summary ?? null,
          total: data.total,
          hasMore: data.has_more === true,
          pageSize: data.page_size,
          pageIndex,
          status: 'online',
          loading: false,
          lastUpdated: new Date(),
        })
      } catch (error) {
        console.error('Kunne ikke hente sensordata:', error)
        setState((previous) => ({
          ...previous,
          readings: [],
          status: 'offline',
          loading: false,
        }))
      }
    },
    []
  )

  useEffect(() => {
    void load(0)

    const timer = window.setInterval(() => {
      void load(current.current.pageIndex)
    }, REFRESH_INTERVAL_MS)

    return () => window.clearInterval(timer)
  }, [load])

  const goToPage = useCallback(
    (index: number) => {
      void load(index)
    },
    [load]
  )

  const changePageSize = useCallback(
    (pageSize: number) => {
      cursors.current = [null]
      void load(0, pageSize)
    },
    [load]
  )

  const refresh = useCallback(() => {
    void load(current.current.pageIndex)
  }, [load])

  return { ...state, goToPage, changePageSize, refresh }
}
