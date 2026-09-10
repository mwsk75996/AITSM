import { StatusBadge } from '@/components/status-badge'
import { ThemeToggle } from '@/components/theme-toggle'
import type { ConnectionStatus } from '@/lib/types'

type AppHeaderProps = {
  status: ConnectionStatus
  theme: 'light' | 'dark'
  onToggleTheme: () => void
}

export function AppHeader({ status, theme, onToggleTheme }: AppHeaderProps) {
  return (
    <header className="sticky top-0 z-20 border-b bg-background/80 backdrop-blur">
      <div className="mx-auto flex w-full max-w-5xl items-center justify-between gap-4 px-4 py-4 sm:px-6">
        <a href="./" className="flex items-center gap-3">
          <span className="flex size-9 items-center justify-center rounded-lg bg-primary font-semibold text-primary-foreground">
            A
          </span>
          <span className="flex flex-col leading-none">
            <span className="font-semibold tracking-tight">AITSM</span>
            <span className="text-xs text-muted-foreground">Engineering</span>
          </span>
        </a>

        <div className="flex items-center gap-2">
          <StatusBadge status={status} />
          <ThemeToggle theme={theme} onToggle={onToggleTheme} />
        </div>
      </div>
    </header>
  )
}
