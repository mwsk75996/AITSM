import { ChevronLeft, ChevronRight } from 'lucide-react'

import { Button } from '@/components/ui/button'
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select'

const PAGE_SIZES = [10, 25, 50, 100]

type PaginationProps = {
  pageIndex: number
  pageSize: number
  total: number
  hasMore: boolean
  loading: boolean
  onPrevious: () => void
  onNext: () => void
  onPageSizeChange: (pageSize: number) => void
}

export function Pagination({
  pageIndex,
  pageSize,
  total,
  hasMore,
  loading,
  onPrevious,
  onNext,
  onPageSizeChange,
}: PaginationProps) {
  const from = total === 0 ? 0 : pageIndex * pageSize + 1
  const to = Math.min((pageIndex + 1) * pageSize, total)
  const info =
    total === 0
      ? 'Ingen målinger'
      : `Viser ${from.toLocaleString('da-DK')}–${to.toLocaleString('da-DK')} af ${total.toLocaleString('da-DK')}`

  return (
    <nav
      className="flex flex-col items-center justify-between gap-3 sm:flex-row"
      aria-label="Målingshistorik sider"
    >
      <p className="text-sm text-muted-foreground" aria-live="polite">
        {info}
      </p>

      <div className="flex items-center gap-2">
        <Button
          variant="outline"
          size="sm"
          onClick={onPrevious}
          disabled={pageIndex <= 0 || loading}
          aria-label="Forrige side"
        >
          <ChevronLeft />
          Forrige
        </Button>
        <Button
          variant="outline"
          size="sm"
          onClick={onNext}
          disabled={!hasMore || loading}
          aria-label="Næste side"
        >
          Næste
          <ChevronRight />
        </Button>
        <Select
          value={String(pageSize)}
          onValueChange={(value) => onPageSizeChange(Number(value))}
        >
          <SelectTrigger className="w-[120px]" aria-label="Antal målinger pr. side">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            {PAGE_SIZES.map((size) => (
              <SelectItem key={size} value={String(size)}>
                {size} pr. side
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
      </div>
    </nav>
  )
}
