import { BatteryIndicator } from '@/components/battery-indicator'
import { Skeleton } from '@/components/ui/skeleton'
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from '@/components/ui/table'
import { formatTimestamp, formatValue } from '@/lib/format'
import { cn } from '@/lib/utils'
import type { ConnectionStatus, Reading } from '@/lib/types'

const SKELETON_ROWS = 6

type ReadingsTableProps = {
  readings: Reading[]
  status: ConnectionStatus
  loading: boolean
}

type StateCellProps = {
  title: string
  text: string
  tone: 'empty' | 'error'
}

function StateCell({ title, text, tone }: StateCellProps) {
  return (
    <TableRow>
      <TableCell colSpan={4} className="py-12">
        <div className="flex flex-col items-center gap-1 text-center">
          <span className={cn('font-medium', tone === 'error' && 'text-destructive')}>
            {title}
          </span>
          <span className="text-sm text-muted-foreground">{text}</span>
        </div>
      </TableCell>
    </TableRow>
  )
}

export function ReadingsTable({ readings, status, loading }: ReadingsTableProps) {
  const showSkeleton = loading && readings.length === 0 && status !== 'offline'

  return (
    <Table aria-busy={loading}>
      <TableHeader>
        <TableRow>
          <TableHead scope="col">Enhed</TableHead>
          <TableHead scope="col">Tidspunkt</TableHead>
          <TableHead scope="col" className="text-right">
            Enhedstemperatur
          </TableHead>
          <TableHead scope="col" className="text-right">
            Batteri
          </TableHead>
        </TableRow>
      </TableHeader>
      <TableBody>
        {status === 'offline' && readings.length === 0 ? (
          <StateCell
            title="QuestDB-data er midlertidigt utilgængelige!"
            text="Vi prøver igen om lidt – siden opdaterer sig selv."
            tone="error"
          />
        ) : showSkeleton ? (
          Array.from({ length: SKELETON_ROWS }, (_, index) => (
            <TableRow key={index} aria-hidden="true">
              <TableCell>
                <Skeleton className="h-4 w-28" />
              </TableCell>
              <TableCell>
                <Skeleton className="h-4 w-32" />
              </TableCell>
              <TableCell>
                <Skeleton className="ml-auto h-4 w-16" />
              </TableCell>
              <TableCell>
                <Skeleton className="ml-auto h-4 w-20" />
              </TableCell>
            </TableRow>
          ))
        ) : readings.length === 0 ? (
          <StateCell
            title="Ingen målinger fundet!"
            text="Der er endnu ikke registreret data fra nogen enhed."
            tone="empty"
          />
        ) : (
          readings.map((reading, index) => (
            <TableRow key={`${reading.device_id}-${reading.timestamp}-${index}`}>
              <TableHead scope="row" className="font-medium">
                {reading.device_id || 'Ukendt enhed'}
              </TableHead>
              <TableCell className="text-muted-foreground">
                {formatTimestamp(reading.timestamp)}
              </TableCell>
              <TableCell className="text-right tabular-nums">
                {formatValue(reading.temperature, '°C')}
              </TableCell>
              <TableCell className="text-right">
                <BatteryIndicator value={reading.battery} />
              </TableCell>
            </TableRow>
          ))
        )}
      </TableBody>
    </Table>
  )
}
