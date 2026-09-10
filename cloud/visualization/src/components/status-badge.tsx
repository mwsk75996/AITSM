import { Badge } from '@/components/ui/badge'
import { cn } from '@/lib/utils'
import type { ConnectionStatus } from '@/lib/types'

const STATUS_STYLES: Record<ConnectionStatus, { label: string; dot: string }> = {
  pending: { label: 'Forbinder', dot: 'bg-muted-foreground animate-pulse' },
  online: { label: 'Online', dot: 'bg-emerald-500' },
  offline: { label: 'Offline', dot: 'bg-red-500' },
}

type StatusBadgeProps = {
  status: ConnectionStatus
}

export function StatusBadge({ status }: StatusBadgeProps) {
  const { label, dot } = STATUS_STYLES[status]

  return (
    <Badge
      variant="outline"
      role="status"
      aria-live="polite"
      className="gap-2 py-1"
    >
      <span className={cn('size-2 rounded-full', dot)} aria-hidden="true" />
      {label}
    </Badge>
  )
}
