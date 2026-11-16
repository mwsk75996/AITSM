import { useMemo } from 'react'

import { AppHeader } from '@/components/app-header'
import { FilterBar } from '@/components/filter-bar'
import { KpiGrid } from '@/components/kpi-grid'
import { Pagination } from '@/components/pagination'
import { ReadingsTable } from '@/components/readings-table'
import { useReadings } from '@/hooks/use-readings'
import { useTheme } from '@/hooks/use-theme'
import { pickRandomEyebrowMessage } from '@/lib/messages'

function App() {
  const { theme, toggleTheme } = useTheme()

  const {
    readings,
    summary,
    total,
    hasMore,
    pageSize,
    pageIndex,
    filters,
    status,
    loading,
    lastUpdated,
    goToPage,
    changePageSize,
    applyFilters,
  } = useReadings()

  const eyebrow = useMemo(() => pickRandomEyebrowMessage(), [])
  const hasFilters = Object.values(filters).some((value) => value !== undefined)

  return (
    <div className="min-h-dvh bg-background">
      <a
        href="#indhold"
        className="sr-only focus:not-sr-only focus:absolute focus:left-4 focus:top-4 focus:z-50 focus:rounded-md focus:bg-primary focus:px-3 focus:py-2 focus:text-primary-foreground"
      >
        Spring til indhold
      </a>

      <AppHeader status={status} theme={theme} onToggleTheme={toggleTheme} />

      <main id="indhold" className="mx-auto w-full max-w-5xl px-4 pb-16 pt-10 sm:px-6">
        <section className="mb-10 max-w-2xl">
          <p className="text-sm font-medium text-primary">{eyebrow}</p>
          <h1 className="mt-2 text-4xl font-semibold tracking-tight sm:text-5xl">
            Alle målinger
          </h1>
          <p className="mt-3 text-muted-foreground">
            Friske tal fra vores Nordic Thingy:91 X-enheder. Batch-data vises som
            individuelle målinger, så historikken bliver bevaret.
          </p>
        </section>

        <KpiGrid
          total={total}
          summary={summary}
          lastUpdated={lastUpdated}
          loading={loading}
        />

        <section className="mt-6" aria-label="Filtre">
          <FilterBar filters={filters} loading={loading} onApply={applyFilters} />
        </section>

        <section className="mt-10" aria-labelledby="readings-title">
          <div className="mb-4">
            <h2 id="readings-title" className="text-xl font-semibold">
              Målingshistorik
            </h2>
            <p className="text-sm text-muted-foreground">
              Alle registreringer, nyeste først. Batch-målinger vises enkeltvis.
            </p>
          </div>

          <div className="rounded-xl border bg-card p-2 shadow-sm">
            <ReadingsTable
              readings={readings}
              status={status}
              loading={loading}
              hasFilters={hasFilters}
            />
          </div>

          <div className="mt-4">
            <Pagination
              pageIndex={pageIndex}
              pageSize={pageSize}
              total={total}
              hasMore={hasMore}
              loading={loading}
              onPrevious={() => goToPage(pageIndex - 1)}
              onNext={() => goToPage(pageIndex + 1)}
              onPageSizeChange={changePageSize}
            />
          </div>
        </section>
      </main>

      <footer className="border-t">
        <div className="mx-auto flex w-full max-w-5xl flex-col items-center justify-between gap-2 px-4 py-6 text-sm text-muted-foreground sm:flex-row sm:px-6">
          <span>AITSM Engineering ApS</span>
          <span>
            QuestDB ·{' '}
            <code className="rounded bg-muted px-1.5 py-0.5 text-xs">sensor_readings</code>
          </span>
        </div>
      </footer>
    </div>
  )
}

export default App
